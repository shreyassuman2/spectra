// Offline checks for Spectra's DSP. Build: cmake --build build --target SpectraTests
#include "../Source/PluginProcessor.h"
#include <juce_audio_formats/juce_audio_formats.h>
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

    /** A host transport that plays from 0 at a fixed tempo in 4/4, following the rig's sample counter. */
    struct Transport final : juce::AudioPlayHead
    {
        double bpm = 120.0;
        bool playing = false;
        long samples = 0;

        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            const double ppq = (double) samples / sr * bpm / 60.0;
            info.setIsPlaying (playing);
            info.setBpm (bpm);
            info.setTimeSignature (TimeSignature { 4, 4 });
            info.setPpqPosition (ppq);
            info.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
            info.setTimeInSamples ((int64_t) samples);
            return info;
        }
    };

    struct Rig
    {
        SpectraProcessor proc;
        juce::MidiBuffer midi;
        Transport transport;
        int inputs;

        explicit Rig (int numInputs) : inputs (numInputs)
        {
            proc.setPlayHead (&transport);
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
                transport.samples = t;
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

    // Presets page with two user presets and the save box open
    rig.proc.saveUserPreset ("Late Night Wash");
    rig.proc.saveUserPreset ("Bell Drone");
    if (auto* b = findButton (*editor, "presets")) b->triggerClick();
    if (auto* b = findButton (*editor, "save as...")) b->triggerClick();
    settle();
    save ("-presets-saving");
    std::function<juce::Viewport* (juce::Component&)> findViewport = [&] (juce::Component& c) -> juce::Viewport*
    {
        for (auto* child : c.getChildren())
        {
            if (auto* v = dynamic_cast<juce::Viewport*> (child)) return v;
            if (auto* found = findViewport (*child)) return found;
        }
        return nullptr;
    };
    if (auto* b = findButton (*editor, "cancel")) b->triggerClick();
    if (auto* v = findViewport (*editor)) v->setViewPosition (0, 10000);
    settle();
    save ("-presets-user");

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

/** Soft electric-piano notes: a few harmonics, quick attack, upper harmonics fading first. */
struct Keys
{
    struct Note { double freq, start, gain; };
    std::vector<Note> notes;

    void chord (std::initializer_list<double> freqs, double start, double gain, double strum)
    {
        double at = start;
        for (auto f : freqs) { notes.push_back ({ f, at, gain }); at += strum; }
    }

    float operator() (long n) const
    {
        const double time = (double) n / sr;
        double sum = 0.0;
        for (const auto& note : notes)
        {
            const double tt = time - note.start;
            if (tt < 0.0 || tt > 6.0) continue;
            const double env = (1.0 - std::exp (-tt / 0.004)) * std::exp (-tt * 0.9);
            for (int k = 1; k <= 6; ++k)
                sum += note.gain * env * std::exp (-tt * 0.7 * (k - 1)) / std::pow ((double) k, 1.6)
                       * std::sin (juce::MathConstants<double>::twoPi * note.freq * k * tt + k * 0.7);
        }
        return (float) sum;
    }
};

/** Scripted session for the README demo. Renders numbered PNG frames into `dir`: 15 fps at 1x
    for the GIF, or with `video` 30 fps at 2x plus the plugin's audio output as `dir/audio.wav`. */
static int demoFrames (const char* dir, bool video)
{
    using P = juce::Point<float>;
    Rig rig (2);
    long t = 0;

    // Chord A (Gmaj7) rings into the freeze; afterwards the input keeps changing (Em9, Dmaj7)
    // while the pad holds chord A. No new notes after 11 s, so the release is heard on its own.
    double now = 0.0;
    Keys keys;
    const double g2 = 98.0, g3 = 196.0, b3 = 246.94, d4 = 293.66, fs4 = 369.99;
    keys.chord ({ g2, g3, b3, d4, fs4 }, 0.10, 0.20, 0.11);
    keys.chord ({ g2, g3, b3, d4, fs4 }, 1.30, 0.20, 0.02);
    for (int i = 0; i < 4; ++i)
    {
        const double at = 2.7 + i * 2.0;
        if (i % 2 == 0) keys.chord ({ 82.41, 164.81, 246.94, 293.66, 369.99 }, at, 0.13, 0.09);   // Em9
        else            keys.chord ({ 73.42, 146.83, 220.0, 277.18, 369.99 }, at, 0.13, 0.09);    // Dmaj7
    }
    auto gen = [&] (int, long n) { return keys (n); };
    juce::AudioBuffer<float> recording (2, 0);

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

    const double fps = 30.0, total = video ? 15.0 : 14.2;
    const float captureScale = video ? 2.0f : 1.0f;
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
        const auto block = rig.run (1.0 / fps, gen, t);
        if (video)
        {
            const int start = recording.getNumSamples();
            recording.setSize (2, start + block.getNumSamples(), true, true);
            for (int ch = 0; ch < 2; ++ch)
                recording.copyFrom (ch, start, block, ch, 0, block.getNumSamples());
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil (32);

        if (! video && step % 2 != 0) continue;   // 15 fps capture for the GIF

        auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, captureScale);
        {
            juce::Graphics g (image);
            g.addTransform (juce::AffineTransform::scale (captureScale));
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

    if (video)
    {
        const float peak = recording.getMagnitude (0, recording.getNumSamples());
        if (peak > 0.0f) recording.applyGain (0.89f / peak);   // normalise to -1 dBFS
        auto wavFile = out.getChildFile ("audio.wav");
        std::unique_ptr<juce::OutputStream> stream (wavFile.createOutputStream().release());
        juce::WavAudioFormat wav;
        if (auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (sr)
                                                                                      .withNumChannels (2)
                                                                                      .withBitsPerSample (24)))
            writer->writeFromAudioSampleBuffer (recording, 0, recording.getNumSamples());
        std::printf ("wrote %s (%.1f s, peak was %.2f)\n", wavFile.getFullPathName().toRawUTF8(),
                     recording.getNumSamples() / sr, peak);
    }
    editor = nullptr;
    return 0;
}

/** About a minute of Spectra in use, for a showcase video: 30 fps frames at 2x plus audio.wav.
    Same electric-piano input as the short demo, playing a G-Em-C-D loop throughout. */
static int demoShowcase (const char* dir)
{
    using P = juce::Point<float>;
    Rig rig (2);
    long t = 0;
    double now = 0.0;
    const double fps = 30.0, total = 57.0;
    const float captureScale = 2.0f;

    // Input ------------------------------------------------------------------
    Keys keys;
    const std::vector<std::vector<double>> progression {
        { 98.0, 196.0, 246.94, 293.66, 369.99 },    // Gmaj7
        { 82.41, 164.81, 246.94, 293.66, 369.99 },  // Em9
        { 65.41, 130.81, 196.0, 246.94, 329.63 },   // Cmaj7
        { 73.42, 146.83, 220.0, 277.18, 369.99 },   // Dmaj7
    };
    auto strike = [&] (int chordIndex, double at, double gain, double strum)
    {
        double when = at;
        for (auto f : progression[(size_t) chordIndex]) { keys.notes.push_back ({ f, when, gain }); when += strum; }
    };
    strike (0, 0.10, 0.20, 0.11);
    strike (0, 1.30, 0.20, 0.02);
    for (int i = 0; 2.7 + i * 2.0 < 49.0; ++i)
        strike ((i + 1) % 4, 2.7 + i * 2.0, 0.14, 0.09);
    auto gen = [&] (int, long n) { return keys (n); };

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
    auto clickButton = [&] (const char* name) { if (auto* b = findButton (*editor, name)) b->triggerClick(); };
    auto value = [&] (const char* id) { auto* prm = rig.proc.apvts.getParameter (id); return prm->convertFrom0to1 (prm->getValue()); };
    auto norm  = [&] (const char* id, float plain) { return rig.proc.apvts.getParameter (id)->convertTo0to1 (plain); };

    // Layout anchors in base coordinates (see Content / Screen / SettingsPage / PresetsPage)
    const P freezeAt { 712, 150 }, sizeAt { 652, 282 }, diffuseAt { 772, 282 }, shiftAt { 652, 396 }, mixAt { 772, 396 };
    const P spectrumTab { 88, 91 }, settingsTab { 168, 91 }, presetsTab { 248, 91 };
    auto presetRow = [] (int i) { return P { 130.0f, 120.0f + (float) i * 36.0f + 18.0f }; };
    auto sliderAt = [] (int col, int row, float v01) { return P { (col == 0 ? 61.0f : 325.0f) + v01 * 210.0f, 153.0f + (float) row * 50.0f }; };
    const P randomizeAt { 390, 393 };

    // Timeline ---------------------------------------------------------------
    P cursor { 430, 300 };
    bool cursorDown = false;
    struct Event { double at; std::function<void()> fn; bool done = false; };
    struct Gesture { double t0, t1; std::function<void (float)> fn; bool ended = false; };
    std::vector<Event> events;
    std::vector<Gesture> gestures;
    P moveFrom = cursor, moveTo = cursor;
    double moveStart = 0.0, moveEnd = 0.0;
    auto ease = [] (double x) { x = juce::jlimit (0.0, 1.0, x); return x * x * (3.0 - 2.0 * x); };

    auto moveAt = [&] (double at, P to, double duration)
    {
        events.push_back ({ at, [&, to, at, duration] { moveFrom = cursor; moveTo = to; moveStart = at; moveEnd = at + duration; } });
    };
    auto click = [&] (double at, P pos, std::function<void()> fn)
    {
        moveAt (at - 0.6, pos, 0.5);
        events.push_back ({ at, [&, fn] { cursorDown = true; fn(); } });
        events.push_back ({ at + 0.15, [&] { cursorDown = false; } });
    };
    auto knob = [&] (double t0, double t1, const char* id, P at, float from, float to)
    {
        moveAt (t0 - 0.6, at, 0.5);
        const float dy = to >= from ? -28.0f : 28.0f;   // drag up to increase, down to decrease
        gestures.push_back ({ t0, t1, [&, id, at, from, to, dy] (float k)
        {
            setParam (rig.proc, id, from + (to - from) * k);
            cursor = at.translated (0, dy * k);
        } });
    };
    auto slider = [&] (double t0, double t1, const char* id, int col, int row, float from, float to)
    {
        moveAt (t0 - 0.6, sliderAt (col, row, norm (id, from)), 0.5);
        gestures.push_back ({ t0, t1, [&, id, col, row, from, to] (float k)
        {
            const float v = from + (to - from) * k;
            setParam (rig.proc, id, v);
            cursor = sliderAt (col, row, norm (id, v));
        } });
    };
    auto freezeClick = [&] (double at, bool on) { click (at, freezeAt, [&, on] { setParam (rig.proc, "freeze", on ? 1.0f : 0.0f); }); };

    // 1. Freeze and the four main knobs
    freezeClick (2.0, true);
    knob (3.2, 4.8, "diffuse", diffuseAt, 0.30f, 0.90f);
    knob (5.6, 6.6, "diffuse", diffuseAt, 0.90f, 0.45f);
    knob (7.6, 8.8, "shift", shiftAt, 0.0f, 12.0f);
    knob (10.2, 11.0, "shift", shiftAt, 12.0f, 0.0f);
    knob (12.0, 13.2, "size", sizeAt, 400.0f, 1500.0f);
    // 2. Release, then re-freeze on a new chord (D, struck at 14.7)
    freezeClick (14.2, false);
    freezeClick (15.3, true);
    // 3. Presets (they never touch freeze, so the pad keeps going)
    click (16.8, presetsTab, [&] { clickButton ("presets"); });
    click (18.0, presetRow (1), [&] { rig.proc.setCurrentProgram (1); });   // glass
    click (21.0, presetRow (2), [&] { rig.proc.setCurrentProgram (2); });   // choir wash
    click (24.0, presetRow (6), [&] { rig.proc.setCurrentProgram (6); });   // shimmer hall
    click (27.0, presetRow (3), [&] { rig.proc.setCurrentProgram (3); });   // sub drone
    click (30.0, presetRow (5), [&] { rig.proc.setCurrentProgram (5); });   // grain cloud
    // 4. Settings page
    click (32.6, settingsTab, [&] { clickButton ("settings"); });
    events.push_back ({ 33.0, [&] {
        slider (33.4, 34.4, "spread",  0, 2, value ("spread"), 0.85f);
        slider (35.2, 36.2, "shimmer", 0, 5, value ("shimmer"), 0.6f);
        slider (37.0, 38.2, "tilt",    1, 0, value ("tilt"), 0.6f);
        slider (39.0, 40.2, "width",   1, 1, 0.0f, 1.0f);
    } });
    click (41.4, randomizeAt, [&] { rig.proc.randomizeTexture(); });
    click (43.2, randomizeAt, [&] { rig.proc.randomizeTexture(); });
    // 5. Back to the spectrum: re-freeze on a new chord (Em, struck at 44.7), mix up, release
    click (44.4, spectrumTab, [&] { clickButton ("spectrum"); });
    freezeClick (45.0, false);
    freezeClick (45.6, true);
    events.push_back ({ 46.6, [&] { knob (47.2, 48.2, "mix", mixAt, value ("mix"), 1.0f); } });
    freezeClick (52.0, false);
    moveAt (52.6, { 430, 330 }, 1.2);

    std::sort (events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.at < b.at; });

    juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (dir));
    out.deleteRecursively();
    out.createDirectory();
    juce::AudioBuffer<float> recording (2, 0);
    int frame = 0;

    for (int step = 0; now < total; ++step, now = step / fps)
    {
        for (size_t i = 0; i < events.size(); ++i)   // index loop: events may add more events
            if (! events[i].done && now >= events[i].at) { events[i].done = true; auto fn = events[i].fn; fn(); }

        for (auto& g : gestures)
        {
            if (g.ended || now < g.t0) continue;
            g.fn ((float) ease ((now - g.t0) / (g.t1 - g.t0)));
            cursorDown = now < g.t1;
            if (now >= g.t1) g.ended = true;
        }

        if (moveEnd > moveStart && now >= moveStart && now <= moveEnd + 1.0 / fps)
            cursor = moveFrom + (moveTo - moveFrom) * (float) ease ((now - moveStart) / (moveEnd - moveStart));

        const auto block = rig.run (1.0 / fps, gen, t);
        const int start = recording.getNumSamples();
        recording.setSize (2, start + block.getNumSamples(), true, true);
        for (int ch = 0; ch < 2; ++ch)
            recording.copyFrom (ch, start, block, ch, 0, block.getNumSamples());
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

        auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, captureScale);
        {
            juce::Graphics g (image);
            g.addTransform (juce::AffineTransform::scale (captureScale));
            juce::Path arrow;
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
        juce::FileOutputStream os (out.getChildFile (juce::String (frame++).paddedLeft ('0', 4) + ".png"));
        juce::PNGImageFormat().writeImageToStream (image, os);
    }

    const float peak = recording.getMagnitude (0, recording.getNumSamples());
    if (peak > 0.0f) recording.applyGain (0.89f / peak);   // -1 dBFS
    auto wavFile = out.getChildFile ("audio.wav");
    std::unique_ptr<juce::OutputStream> stream (wavFile.createOutputStream().release());
    juce::WavAudioFormat wav;
    if (auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (2).withBitsPerSample (24)))
        writer->writeFromAudioSampleBuffer (recording, 0, recording.getNumSamples());
    std::printf ("wrote %d frames and audio.wav (%.1f s, peak was %.2f) to %s\n", frame,
                 recording.getNumSamples() / sr, peak, out.getFullPathName().toRawUTF8());
    editor = nullptr;
    return 0;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // Keep the tests away from the real user preset folder.
    const juce::TemporaryFile presetDir;
    setenv ("SPECTRA_PRESET_DIR", presetDir.getFile().getFullPathName().toRawUTF8(), 1);
    struct Cleanup { juce::File dir; ~Cleanup() { dir.deleteRecursively(); } } cleanup { presetDir.getFile() };
    if (argc > 2 && juce::String (argv[1]) == "--snapshot")
        return snapshot (argv[2]);
    if (argc > 2 && juce::String (argv[1]) == "--demo-frames")
        return demoFrames (argv[2], false);
    if (argc > 2 && juce::String (argv[1]) == "--demo-video")
        return demoFrames (argv[2], true);
    if (argc > 2 && juce::String (argv[1]) == "--demo-showcase")
        return demoShowcase (argv[2]);

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

    std::puts ("7. User presets save, reload and delete");
    {
        SpectraProcessor proc;
        const int factory = proc.getNumFactoryPresets();
        check (proc.getNumPrograms() == factory, "starts with factory presets only", proc.getNumPrograms());

        setParam (proc, "diffuse", 0.83f);
        setParam (proc, "shift", -7.0f);
        setParam (proc, "freeze", 1.0f);
        const int saved = proc.saveUserPreset ("  My Pad/1  ");
        check (saved == factory && proc.getCurrentProgram() == saved, "saved preset becomes current", saved);
        check (proc.getProgramName (saved) == "My Pad/1", "name kept (trimmed)", 0);
        check (SpectraProcessor::userPresetFolder().getNumberOfChildFiles (juce::File::findFiles) == 1, "one file written", 0);

        proc.setCurrentProgram (0);                               // load Init, then the user preset again
        setParam (proc, "freeze", 0.0f);
        proc.setCurrentProgram (saved);
        auto value = [&] (const char* id) { auto* prm = proc.apvts.getParameter (id); return prm->convertFrom0to1 (prm->getValue()); };
        check (std::abs (value ("diffuse") - 0.83f) < 0.001f, "diffuse restored", value ("diffuse"));
        check (std::abs (value ("shift") + 7.0f) < 0.01f, "shift restored", value ("shift"));
        check (value ("freeze") < 0.5f, "loading a preset never touches freeze", value ("freeze"));

        SpectraProcessor other;                                   // a second instance finds it on disk
        check (other.getNumPrograms() == factory + 1, "new instance sees the saved preset", other.getNumPrograms());

        juce::MemoryBlock state;                                  // project reload keeps the selection by name
        proc.getStateInformation (state);
        other.setStateInformation (state.getData(), (int) state.getSize());
        check (other.getCurrentProgram() == saved, "project reload restores the user preset", other.getCurrentProgram());

        check (proc.deleteUserPreset (saved) && proc.getNumPrograms() == factory, "delete removes it", proc.getNumPrograms());
        check (proc.saveUserPreset ("   ") == -1, "blank name refused", 0);
    }

    std::puts ("8. Every preset stays within 6 dB of Init's loudness");
    {
        auto loudness = [] (int program)
        {
            Rig rig (2);
            Keys keys;
            keys.notes.push_back ({ 73.42, 0.05, 0.2 });     // D2 bass
            keys.notes.push_back ({ 146.83, 0.05, 0.2 });
            keys.notes.push_back ({ 220.0, 0.06, 0.2 });
            keys.notes.push_back ({ 277.18, 0.07, 0.2 });
            keys.notes.push_back ({ 369.99, 0.08, 0.2 });
            long t = 0;
            auto gen = [&] (int, long n) { return keys (n); };
            rig.proc.setCurrentProgram (program);
            setParam (rig.proc, "mix", 1.0f);                 // pad only
            rig.run (0.6, gen, t);
            setParam (rig.proc, "freeze", 1.0f);
            rig.run (3.0, gen, t);                            // past every preset's attack
            const auto out = rig.run (2.0, gen, t);
            double sum = 0.0;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < out.getNumSamples(); ++i)
                    sum += (double) out.getSample (ch, i) * out.getSample (ch, i);
            return 10.0 * std::log10 (sum / (2.0 * out.getNumSamples()) + 1e-20);
        };
        const double reference = loudness (0);
        SpectraProcessor names;
        for (int i = 1; i < names.getNumFactoryPresets(); ++i)
        {
            const double diff = loudness (i) - reference;
            const auto label = names.getProgramName (i) + ": dB relative to Init";
            check (std::abs (diff) < 6.0, label.toRawUTF8(), diff);
        }
    }

    std::puts ("9. Re-freeze 1/4 at 120 bpm captures a new moment on every beat");
    {
        // The input changes pitch halfway between beats, so each beat line captures the pitch just before it.
        auto pitchAt = [] (long n) { const long seg = (long) std::floor (((double) n / sr - 0.25) / 0.5); return seg < 0 || seg % 2 == 0 ? 300.0 : 500.0; };
        auto gen = [&] (int, long n) { return sine (pitchAt (n), n); };
        const int beat = (int) (0.5 * sr);

        for (bool playing : { true, false })
        {
            Rig rig (2);
            freezeSettings (rig.proc);
            setParam (rig.proc, "refreeze", 3.0f);   // 1/4
            rig.transport.playing = playing;
            long t = 0;
            rig.run (0.9, gen, t);
            setParam (rig.proc, "freeze", 1.0f);     // first capture at 0.9 s (500 Hz)
            auto out = rig.run (3.1, gen, t);        // beat lines at 1.0, 1.5, ... 3.5 s

            int correct = 0, total = 0;
            for (int b = 2; b <= 7; ++b)             // measure 150..450 ms after each beat line
            {
                const int start = b * beat - (int) (0.9 * sr) + (int) (0.15 * sr);
                const double f = estimateFrequency (out.getReadPointer (0, start), (int) (0.3 * sr));
                const double expected = pitchAt (playing ? (long) b * beat - 1 : (long) (0.9 * sr));
                correct += std::abs (f - expected) < 3.0 ? 1 : 0;
                ++total;
            }
            check (correct == total, playing ? "playing: each beat holds the pitch just before it"
                                             : "stopped: no re-freeze, the first capture holds", correct);
        }
    }

    std::puts ("10. Fast re-freeze with a long Release stays smooth");
    {
        Rig rig (2);
        freezeSettings (rig.proc);
        setParam (rig.proc, "attack", 60.0f);
        setParam (rig.proc, "release", 5000.0f);   // much longer than a step: the cap has to apply
        setParam (rig.proc, "refreeze", 4.0f);     // 1/8 at 120 bpm = 250 ms
        rig.transport.playing = true;
        long t = 0;
        auto gen = [] (int, long n) { return sine (400.0, n); };
        rig.run (1.0, gen, t);
        setParam (rig.proc, "freeze", 1.0f);
        auto out = rig.run (4.0, gen, t);
        const int skip = (int) (1.0 * sr);
        float maxStep = 0.0f;
        for (int i = skip + 1; i < out.getNumSamples(); ++i)
            maxStep = std::max (maxStep, std::abs (out.getSample (0, i) - out.getSample (0, i - 1)));
        const float sineStep = 2.0f * juce::MathConstants<float>::pi * 400.0f / (float) sr * 0.5f * 2.0f;
        check (maxStep < sineStep, "max sample step below 2x sine slope", maxStep);

        // Short-window loudness shouldn't collapse when a fading capture is stolen.
        double lo = 1e9, hi = 0.0;
        const int win = 2048;
        for (int i = skip; i + win <= out.getNumSamples(); i += win)
        {
            const double r = rms (out.getReadPointer (0, i), win);
            lo = std::min (lo, r); hi = std::max (hi, r);
        }
        const double dipDb = 20.0 * std::log10 (hi / std::max (lo, 1e-9));
        check (dipDb < 9.0, "loudness range across captures < 9 dB", dipDb);
    }

    std::puts ("11. Presets never save or load Re-freeze");
    {
        SpectraProcessor proc;
        setParam (proc, "refreeze", 3.0f);
        const int saved = proc.saveUserPreset ("Rhythm");
        setParam (proc, "refreeze", 0.0f);
        proc.setCurrentProgram (saved);
        check (proc.apvts.getRawParameterValue ("refreeze")->load() < 0.5f, "user preset leaves re-freeze alone", 0);
        setParam (proc, "refreeze", 2.0f);
        proc.setCurrentProgram (1);
        check (std::abs (proc.apvts.getRawParameterValue ("refreeze")->load() - 2.0f) < 0.01f, "factory preset leaves re-freeze alone", 0);
        proc.deleteUserPreset (saved);
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILURES", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
