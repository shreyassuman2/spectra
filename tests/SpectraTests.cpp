// Offline checks for Spectra's DSP. Build: cmake --build build --target SpectraTests
#include "../Source/PluginProcessor.h"
#include <chrono>
#include <cstdio>

namespace
{
    constexpr double sr = 48000.0;
    constexpr int block = 512;
    int failures = 0;

    void check (bool ok, const char* what, double value = 0.0)
    {
        std::printf ("  [%s] %s (%.4f)\n", ok ? "PASS" : "FAIL", what, value);
        if (! ok) ++failures;
    }

    void setParam (SpectraProcessor& p, const char* id, float plain)
    {
        auto* param = p.apvts.getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (plain));
    }

    struct Rig
    {
        SpectraProcessor proc;
        juce::MidiBuffer midi;
        int inputs;

        explicit Rig (int numInputs) : inputs (numInputs)
        {
            proc.setPlayConfigDetails (numInputs, 2, sr, block);
            proc.enableAllBuses();
            auto layout = proc.getBusesLayout();
            layout.inputBuses.getReference (0) = numInputs == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
            proc.setBusesLayout (layout);
            proc.prepareToPlay (sr, block);
        }

        /** Runs `seconds` of a generator; returns the output (2 channels). */
        juce::AudioBuffer<float> run (double seconds, std::function<float (int ch, long n)> gen, long& t)
        {
            const int total = (int) (seconds * sr);
            juce::AudioBuffer<float> out (2, total);
            juce::AudioBuffer<float> buf (2, block);
            for (int pos = 0; pos < total; pos += block)
            {
                const int n = std::min (block, total - pos);
                buf.setSize (2, n, false, false, true);
                for (int i = 0; i < n; ++i)
                    for (int ch = 0; ch < 2; ++ch)
                        buf.setSample (ch, i, ch < inputs ? gen (ch, t + i) : 0.0f);
                proc.processBlock (buf, midi);
                for (int ch = 0; ch < 2; ++ch)
                    out.copyFrom (ch, pos, buf, ch, 0, n);
                t += n;
            }
            return out;
        }
    };

    float sine (double hz, long n, float amp = 0.5f) { return amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * (double) n / sr); }

    double estimateFrequency (const float* x, int n)
    {
        // Linear-interpolated rising zero crossings
        double first = -1, last = -1; int count = 0;
        for (int i = 1; i < n; ++i)
            if (x[i - 1] < 0.0f && x[i] >= 0.0f)
            {
                const double pos = (i - 1) + x[i - 1] / (double) (x[i - 1] - x[i]);
                if (first < 0) first = pos; else ++count;
                last = pos;
            }
        return count > 0 ? sr * count / (last - first) : 0.0;
    }

    // Coefficient of variation of short-window RMS: measures flutter / amplitude wobble.
    double rmsWobble (const float* x, int n, int win = 2048)
    {
        std::vector<double> rms;
        for (int i = 0; i + win <= n; i += win)
        {
            double s = 0; for (int k = 0; k < win; ++k) s += x[i + k] * x[i + k];
            rms.push_back (std::sqrt (s / win));
        }
        double m = 0; for (double r : rms) m += r; m /= (double) rms.size();
        double v = 0; for (double r : rms) v += (r - m) * (r - m); v /= (double) rms.size();
        return std::sqrt (v) / std::max (m, 1e-12);
    }

    double rms (const float* x, int n) { double s = 0; for (int i = 0; i < n; ++i) s += x[i] * x[i]; return std::sqrt (s / n); }

    double correlation (const float* a, const float* b, int n)
    {
        double ab = 0, aa = 0, bb = 0;
        for (int i = 0; i < n; ++i) { ab += a[i] * b[i]; aa += a[i] * a[i]; bb += b[i] * b[i]; }
        return ab / std::sqrt (aa * bb + 1e-20);
    }

    void freezeSettings (SpectraProcessor& p)
    {
        setParam (p, "mix", 1.0f);  setParam (p, "level", 0.0f);
        setParam (p, "attack", 10.0f); setParam (p, "release", 10.0f);
        setParam (p, "diffuse", 0.0f); setParam (p, "drift", 0.0f); setParam (p, "spread", 0.0f);
        setParam (p, "blur", 0.0f); setParam (p, "tilt", 0.0f); setParam (p, "shimmer", 0.0f);
        setParam (p, "shift", 0.0f); setParam (p, "size", 20.0f); setParam (p, "width", 0.0f);
    }
}

static int snapshot (const char* path)
{
    Rig rig (2);
    setParam (rig.proc, "size", 1200.0f);
    setParam (rig.proc, "spread", 0.25f);
    setParam (rig.proc, "drift", 0.4f);
    setParam (rig.proc, "diffuse", 0.4f);
    setParam (rig.proc, "attack", 100.0f);
    juce::Random r (5);
    long t = 0;
    auto gen = [&] (int, long n)
    {
        const double env = 0.6 + 0.4 * std::sin ((double) n / sr * 3.0);
        return (float) env * (sine (196.0, n, 0.25f) + sine (246.9, n, 0.2f) + sine (293.7, n, 0.18f)
                              + sine (392.0 * (1.0 + 0.002 * std::sin ((double) n / sr * 5.0)), n, 0.1f)
                              + sine (1568.0, n, 0.02f)) + (r.nextFloat() - 0.5f) * 0.01f;
    };
    rig.run (1.5, gen, t);
    setParam (rig.proc, "freeze", 1.0f);

    if (auto* scaleEnv = std::getenv ("SPECTRA_SCALE"))
        rig.proc.setUiScale ((float) std::atof (scaleEnv));
    std::unique_ptr<juce::AudioProcessorEditor> editor (rig.proc.createEditorAndMakeActive());
    editor->setVisible (true);
    for (int i = 0; i < 60; ++i)
    {
        rig.run (1.0 / 30.0, gen, t);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (34);
    }
    auto save = [&] (const juce::String& suffix)
    {
        auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        juce::File base (juce::File::getCurrentWorkingDirectory().getChildFile (path));
        auto file = base.getSiblingFile (base.getFileNameWithoutExtension() + suffix + ".png");
        file.deleteFile();
        juce::FileOutputStream os (file);
        juce::PNGImageFormat().writeImageToStream (image, os);
        std::printf ("wrote %s (%dx%d)\n", file.getFullPathName().toRawUTF8(), editor->getWidth(), editor->getHeight());
    };
    save ("");

    auto settle = [&]
    {
        for (int i = 0; i < 30; ++i)
        {
            rig.run (1.0 / 30.0, gen, t);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (34);
        }
    };
    std::function<juce::Button* (juce::Component&, const juce::String&)> findButton =
        [&] (juce::Component& c, const juce::String& name) -> juce::Button*
    {
        for (auto* child : c.getChildren())
        {
            if (auto* b = dynamic_cast<juce::Button*> (child); b != nullptr && b->getName() == name) return b;
            if (auto* found = findButton (*child, name)) return found;
        }
        return nullptr;
    };
    for (auto* tab : { "settings", "presets" })
        if (auto* b = findButton (*editor, tab))
        {
            b->triggerClick();
            settle();
            save (juce::String ("-") + tab);
        }

    // Tooltips for the freeze button and the four knobs, stacked into one image
    {
        auto& lnf = editor->getLookAndFeel();
        const juce::StringArray tips { "Hold the last moment as a pad. Click to release.",
                                       "Length of the captured moment, up to 2 s.",
                                       "Tonal and pitched at 0, an airy wash at 100%.",
                                       "Pitch in semitones. Hold Shift to drag finely.",
                                       "How much the pad replaces the dry sound.",
                                       "Roll new values for scan, drift, spread, diffuse, blur, tone, width and shimmer." };
        juce::Image img (juce::Image::ARGB, 380, 480, true);
        juce::Graphics g (img);
        g.fillAll (juce::Colour (0xff100d1e));
        int y = 10;
        for (auto& tip : tips)
        {
            const auto r = lnf.getTooltipBounds (tip, { 10, 0 }, { 0, 0, 380, 2000 });
            juce::Graphics::ScopedSaveState state (g);
            g.setOrigin (10, y);
            lnf.drawTooltip (g, tip, r.getWidth(), r.getHeight());
            y += r.getHeight() + 10;
        }
        {
            // The window-size menu, drawn the same way the popup draws it
            juce::Graphics::ScopedSaveState state (g);
            g.setOrigin (280, y + 4);
            const juce::StringArray items { "75%", "90%", "100%", "125%", "150%" };
            const int border = lnf.getPopupMenuBorderSize(), w = 84;
            lnf.drawPopupMenuBackground (g, w, items.size() * 28 + border * 2);
            for (int i = 0; i < items.size(); ++i)
                lnf.drawPopupMenuItem (g, { border, border + i * 28, w - border * 2, 28 }, false, true, i == 3, i == 2,
                                       false, items[i], {}, nullptr, nullptr);
        }
        juce::File base (juce::File::getCurrentWorkingDirectory().getChildFile (path));
        auto file = base.getSiblingFile (base.getFileNameWithoutExtension() + "-tooltips.png");
        file.deleteFile();
        juce::FileOutputStream os (file);
        juce::PNGImageFormat().writeImageToStream (img, os);
    }
    editor = nullptr;
    return 0;
}

/** Scripted session for the README GIF: renders numbered PNG frames (15 fps) into `dir`. */
static int demoFrames (const char* dir)
{
    using P = juce::Point<float>;
    Rig rig (2);
    juce::Random r (9);
    long t = 0;

    // Chord A before the freeze, chord B after, so you can see the pad hold while the input changes.
    double now = 0.0;
    auto gen = [&] (int, long n)
    {
        const bool chordB = now > 2.6;
        const double env = 0.65 + 0.35 * std::sin ((double) n / sr * 2.3);
        const float a = chordB ? (sine (293.7, n, 0.22f) + sine (370.0, n, 0.18f) + sine (440.0, n, 0.16f) + sine (2349.0, n, 0.02f))
                               : (sine (196.0, n, 0.25f) + sine (246.9, n, 0.2f) + sine (293.7, n, 0.18f) + sine (1568.0, n, 0.03f));
        return (float) env * a + (r.nextFloat() - 0.5f) * 0.01f;
    };

    std::unique_ptr<juce::AudioProcessorEditor> editor (rig.proc.createEditorAndMakeActive());
    editor->setVisible (true);

    std::function<juce::Button* (juce::Component&, const juce::String&)> findButton =
        [&] (juce::Component& c, const juce::String& name) -> juce::Button*
    {
        for (auto* child : c.getChildren())
        {
            if (auto* b = dynamic_cast<juce::Button*> (child); b != nullptr && b->getName() == name) return b;
            if (auto* found = findButton (*child, name)) return found;
        }
        return nullptr;
    };

    // Layout anchors (base coordinates, see Content::resized / SettingsPage::resized)
    const P freezeAt { 712, 150 }, sizeAt { 652, 282 }, diffuseAt { 772, 282 }, shiftAt { 652, 396 };
    const P settingsTab { 168, 91 }, spectrumTab { 88, 91 };
    auto sliderX = [] (float v) { return 61.0f + v * 210.0f; };
    const float spreadY = 253.0f, shimmerY = 403.0f;

    struct Key { double time; P pos; bool down; };
    std::vector<Key> path;
    P cursor { 430, 300 };
    bool cursorDown = false;

    auto ease = [] (double x) { x = juce::jlimit (0.0, 1.0, x); return x * x * (3.0 - 2.0 * x); };
    auto lerpP = [] (P a, P b, float k) { return a + (b - a) * k; };

    juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (dir));
    out.deleteRecursively();
    out.createDirectory();

    const double fps = 30.0, total = 14.2;
    int frame = 0;
    P moveFrom = cursor;
    double moveStart = 0.0, moveEnd = 0.0;
    P moveTo = cursor;
    auto moveCursor = [&] (P to, double start, double duration) { moveFrom = cursor; moveTo = to; moveStart = start; moveEnd = start + duration; };
    int lastEvent = -1;

    for (int step = 0; now < total; ++step, now = step / fps)
    {
        // Timeline -----------------------------------------------------------
        auto at = [&] (int id, double when) { if (now >= when && lastEvent < id) { lastEvent = id; return true; } return false; };

        if (at (0, 1.2))  moveCursor (freezeAt, now, 0.7);
        if (at (1, 2.0))  { setParam (rig.proc, "freeze", 1.0f); cursorDown = true; }
        if (at (2, 2.2))  cursorDown = false;
        if (at (3, 3.0))  moveCursor (diffuseAt, now, 0.6);
        if (at (4, 4.9))  moveCursor (shiftAt, now, 0.6);
        if (at (5, 6.6))  moveCursor (sizeAt, now, 0.5);
        if (at (6, 7.7))  moveCursor (settingsTab, now, 0.6);
        if (at (7, 8.3))  { if (auto* b = findButton (*editor, "settings")) b->triggerClick(); cursorDown = true; }
        if (at (8, 8.45)) { cursorDown = false; moveCursor ({ sliderX (0.1f), spreadY }, now, 0.5); }
        if (at (9, 9.9))  moveCursor ({ sliderX (0.0f), shimmerY }, now, 0.5);
        if (at (10, 11.2)) moveCursor (spectrumTab, now, 0.5);
        if (at (11, 11.7)) { if (auto* b = findButton (*editor, "spectrum")) b->triggerClick(); cursorDown = true; }
        if (at (12, 11.85)) { cursorDown = false; moveCursor (freezeAt, now, 0.6); }
        if (at (13, 12.6)) { setParam (rig.proc, "freeze", 0.0f); cursorDown = true; }
        if (at (14, 12.8)) { cursorDown = false; moveCursor ({ 430, 300 }, now, 1.0); }

        // Continuous gestures (cursor rides along with the value)
        auto sweep = [&] (double t0, double t1) { return (float) ease ((now - t0) / (t1 - t0)); };
        if (now >= 3.6 && now <= 4.7)  { const float k = sweep (3.6, 4.7);  setParam (rig.proc, "diffuse", 0.3f + 0.55f * k); cursor = diffuseAt.translated (0, -30.0f * k); cursorDown = true; }
        if (now > 4.7 && now < 4.75) cursorDown = false;
        if (now >= 5.5 && now <= 6.4)  { const float k = sweep (5.5, 6.4);  setParam (rig.proc, "shift", 7.0f * k); cursor = shiftAt.translated (0, -26.0f * k); cursorDown = true; }
        if (now > 6.4 && now < 6.45) cursorDown = false;
        if (now >= 7.1 && now <= 7.6)  { const float k = sweep (7.1, 7.6);  setParam (rig.proc, "size", 400.0f + 1100.0f * k); cursor = sizeAt.translated (0, -24.0f * k); cursorDown = true; }
        if (now > 7.6 && now < 7.65) cursorDown = false;
        if (now >= 9.0 && now <= 9.8)  { const float k = sweep (9.0, 9.8);  const float v = 0.1f + 0.5f * k; setParam (rig.proc, "spread", v); cursor = { sliderX (v), spreadY }; cursorDown = true; }
        if (now > 9.8 && now < 9.85) cursorDown = false;
        if (now >= 10.4 && now <= 11.1) { const float k = sweep (10.4, 11.1); const float v = 0.55f * k; setParam (rig.proc, "shimmer", v); cursor = { sliderX (v), shimmerY }; cursorDown = true; }
        if (now > 11.1 && now < 11.15) cursorDown = false;

        if (now >= moveStart && now <= moveEnd + 1.0 / fps && moveEnd > moveStart)
            cursor = lerpP (moveFrom, moveTo, (float) ease ((now - moveStart) / (moveEnd - moveStart)));

        // Audio + UI time ------------------------------------------------------
        rig.run (1.0 / fps, gen, t);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (32);

        if (step % 2 != 0) continue;   // 15 fps capture

        auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        {
            juce::Graphics g (image);
            juce::Path arrow;   // classic pointer
            arrow.startNewSubPath (0, 0);
            arrow.lineTo (0, 17); arrow.lineTo (4.5f, 13); arrow.lineTo (7.5f, 19.5f);
            arrow.lineTo (10, 18.5f); arrow.lineTo (7, 12); arrow.lineTo (12.5f, 12);
            arrow.closeSubPath();
            arrow.applyTransform (juce::AffineTransform::scale (cursorDown ? 0.92f : 1.0f).translated (cursor));
            if (cursorDown)
            {
                g.setColour (juce::Colour (0xfff0c27a).withAlpha (0.35f));
                g.fillEllipse (juce::Rectangle<float> (26, 26).withCentre (cursor));
            }
            g.setColour (juce::Colours::white);
            g.fillPath (arrow);
            g.setColour (juce::Colours::black);
            g.strokePath (arrow, juce::PathStrokeType (1.2f));
        }
        auto file = out.getChildFile (juce::String (frame++).paddedLeft ('0', 4) + ".png");
        juce::FileOutputStream os (file);
        juce::PNGImageFormat().writeImageToStream (image, os);
    }

    std::printf ("wrote %d frames to %s\n", frame, out.getFullPathName().toRawUTF8());
    editor = nullptr;
    return 0;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    if (argc > 2 && juce::String (argv[1]) == "--snapshot")
        return snapshot (argv[2]);
    if (argc > 2 && juce::String (argv[1]) == "--demo-frames")
        return demoFrames (argv[2]);

    std::puts ("1. Unfrozen output is bit-identical to the input");
    {
        Rig rig (2);
        long t = 0;
        juce::Random r (1);
        std::vector<float> noise ((size_t) (sr * 3) * 2);
        for (auto& v : noise) v = r.nextFloat() * 2.0f - 1.0f;
        auto out = rig.run (3.0, [&] (int ch, long n) { return noise[(size_t) (n * 2 + ch)]; }, t);
        bool identical = true;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < out.getNumSamples(); ++i)
                identical &= out.getSample (ch, i) == noise[(size_t) (i * 2 + ch)];
        check (identical, "dry passthrough identical", 0.0);
    }

    std::puts ("2. Freeze a 440 Hz sine, Diffuse 0: stable, pitched, right level");
    {
        Rig rig (2);
        freezeSettings (rig.proc);
        long t = 0;
        auto gen = [] (int, long n) { return sine (440.0, n); };
        rig.run (1.0, gen, t);
        setParam (rig.proc, "freeze", 1.0f);
        auto out = rig.run (3.0, [] (int, long) { return 0.0f; }, t);   // input goes silent: pad must hold
        const int skip = (int) (0.5 * sr), n = out.getNumSamples() - skip;
        const float* x = out.getReadPointer (0, skip);
        const double f = estimateFrequency (x, n), wob = rmsWobble (x, n), level = rms (x, n);
        check (std::abs (f - 440.0) < 1.0, "pad frequency ~440 Hz", f);
        check (wob < 0.02, "amplitude wobble < 2%", wob);
        check (std::abs (level - 0.5 / std::sqrt (2.0)) < 0.05, "pad RMS ~ input RMS (0.354)", level);
    }

    std::puts ("3. Shift +12 st doubles the frequency, -12 halves it");
    for (float semis : { 12.0f, -12.0f })
    {
        Rig rig (2);
        freezeSettings (rig.proc);
        setParam (rig.proc, "shift", semis);
        long t = 0;
        rig.run (1.0, [] (int, long n) { return sine (440.0, n); }, t);
        setParam (rig.proc, "freeze", 1.0f);
        auto out = rig.run (2.0, [] (int, long) { return 0.0f; }, t);
        const int skip = (int) (0.5 * sr);
        const double f = estimateFrequency (out.getReadPointer (0, skip), out.getNumSamples() - skip);
        const double expected = 440.0 * std::pow (2.0, semis / 12.0);
        check (std::abs (f - expected) < expected * 0.01, semis > 0 ? "+12 st -> 880 Hz" : "-12 st -> 220 Hz", f);
    }

    std::puts ("4. Mono -> stereo: Width 0 is mono, Width 100% is wide");
    for (float w : { 0.0f, 1.0f })
    {
        Rig rig (1);
        freezeSettings (rig.proc);
        setParam (rig.proc, "width", w);
        setParam (rig.proc, "diffuse", 0.6f);
        setParam (rig.proc, "size", 500.0f);
        setParam (rig.proc, "spread", 0.3f);
        juce::Random r (7);
        long t = 0;
        rig.run (1.0, [&] (int, long n) { return sine (220.0, n, 0.3f) + sine (660.0, n, 0.2f) + (r.nextFloat() - 0.5f) * 0.2f; }, t);
        setParam (rig.proc, "freeze", 1.0f);
        auto out = rig.run (2.0, [] (int, long) { return 0.0f; }, t);
        const int skip = (int) (0.5 * sr), n = out.getNumSamples() - skip;
        const double c = correlation (out.getReadPointer (0, skip), out.getReadPointer (1, skip), n);
        if (w == 0.0f) check (c > 0.999, "width 0: L/R correlation ~1", c);
        else           check (c < 0.6,   "width 100%: L/R correlation < 0.6", c);
    }

    std::puts ("5. Re-freeze while frozen crossfades without clicks");
    {
        Rig rig (2);
        freezeSettings (rig.proc);
        setParam (rig.proc, "attack", 300.0f);
        setParam (rig.proc, "release", 300.0f);
        long t = 0;
        auto gen = [] (int, long n) { return n < (long) (1.5 * sr) ? sine (300.0, n) : sine (500.0, n); };
        rig.run (1.0, gen, t);
        setParam (rig.proc, "freeze", 1.0f);
        rig.run (1.0, gen, t);
        setParam (rig.proc, "freeze", 0.0f);
        rig.run (0.05, gen, t);
        setParam (rig.proc, "mix", 1.0f);
        setParam (rig.proc, "freeze", 1.0f);   // re-freeze 50 ms later
        auto out = rig.run (1.5, gen, t);
        // Largest jump in the wet signal relative to what a pure sine at these amplitudes can do
        float maxStep = 0.0f;
        for (int i = 1; i < out.getNumSamples(); ++i)
            maxStep = std::max (maxStep, std::abs (out.getSample (0, i) - out.getSample (0, i - 1)));
        const float sineStep = 2.0f * juce::MathConstants<float>::pi * 500.0f / (float) sr * 0.5f * 2.0f;  // 2x headroom
        check (maxStep < sineStep, "max sample step below 2x sine slope", maxStep);
    }

    std::puts ("6. CPU: stereo, 2 voices, all features on");
    {
        Rig rig (2);
        setParam (rig.proc, "diffuse", 0.5f); setParam (rig.proc, "spread", 0.4f);
        setParam (rig.proc, "blur", 0.5f);    setParam (rig.proc, "shift", 5.0f);
        setParam (rig.proc, "shimmer", 0.5f); setParam (rig.proc, "tilt", 0.3f);
        setParam (rig.proc, "absorb", 0.5f);  setParam (rig.proc, "size", 2000.0f);
        setParam (rig.proc, "release", 10000.0f);
        juce::Random r (3);
        long t = 0;
        auto gen = [&] (int, long) { return (r.nextFloat() - 0.5f) * 0.5f; };
        rig.run (2.1, gen, t);
        setParam (rig.proc, "freeze", 1.0f); rig.run (0.5, gen, t);
        setParam (rig.proc, "freeze", 0.0f); rig.run (0.1, gen, t);
        setParam (rig.proc, "freeze", 1.0f);   // second voice: both now active
        const double seconds = 10.0;
        const auto start = std::chrono::steady_clock::now();
        rig.run (seconds, gen, t);
        const double elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
        const double percent = 100.0 * elapsed / seconds;
        check (percent < 3.0, "CPU % of one core (target < 3%)", percent);
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILURES", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
