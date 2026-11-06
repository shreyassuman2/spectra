#include "PluginEditor.h"
#include "Presets.h"

#if JUCE_MAC
 #include <CoreFoundation/CoreFoundation.h>
#endif

namespace spectra::ui
{

namespace
{
    constexpr float pi = juce::MathConstants<float>::pi;
    const juce::Array<float> scaleSteps { 0.75f, 0.9f, 1.0f, 1.25f, 1.5f };

    juce::Font mono (float size, float tracking = 0.0f)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size, juce::Font::plain)
                               .withKerningFactor (tracking));
    }

    juce::Font light (float size, float tracking = 0.0f)
    {
        return juce::Font (juce::FontOptions ("Helvetica Neue", size, juce::Font::plain).withStyle ("Light").withKerningFactor (tracking));
    }

    float freqToNorm (float hz) { return std::log (hz / 20.0f) / std::log (1000.0f); }
}

bool prefersReducedMotion()
{
   #if JUCE_MAC
    Boolean valid = false;
    const Boolean v = CFPreferencesGetAppBooleanValue (CFSTR ("reduceMotion"), CFSTR ("com.apple.universalaccess"), &valid);
    return valid && v;
   #else
    return false;
   #endif
}

//==============================================================================
LookAndFeel::LookAndFeel()
{
    setColour (juce::PopupMenu::backgroundColourId, colours::surface);
    setColour (juce::PopupMenu::textColourId, colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::control);
    setColour (juce::PopupMenu::highlightedTextColourId, colours::gold);
    setColour (juce::TooltipWindow::backgroundColourId, colours::surface);
    setColour (juce::TooltipWindow::textColourId, colours::text);
    setColour (juce::TooltipWindow::outlineColourId, colours::line);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                    float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat();
    const float r = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f - 1.5f;
    const auto c = bounds.getCentre();
    const bool active = slider.isMouseOverOrDragging();
    const bool bipolar = slider.getProperties()["bipolar"];
    const float angle = startAngle + pos * (endAngle - startAngle);
    const auto stroke = juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    juce::Path track;
    track.addCentredArc (c.x, c.y, r, r, 0.0f, startAngle, endAngle, true);
    g.setColour (colours::line);
    g.strokePath (track, stroke);

    const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
    if (std::abs (angle - from) > 0.001f)
    {
        juce::Path value;
        value.addCentredArc (c.x, c.y, r, r, 0.0f, std::min (from, angle), std::max (from, angle), true);
        g.setColour (active ? colours::gold : colours::lavender);
        g.strokePath (value, stroke);
    }

    const float bodyR = r - 7.0f;
    g.setColour (active ? colours::control.brighter (0.08f) : colours::control);
    g.fillEllipse (juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (c));

    g.setColour (colours::text);
    g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (c.getPointOnCircumference (bodyR - 7.0f, angle)));
}

void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                    juce::Slider::SliderStyle, juce::Slider& slider)
{
    const bool active = slider.isMouseOverOrDragging();
    const bool bipolar = slider.getProperties()["bipolar"];
    const float cy = (float) y + (float) h * 0.5f;
    const float x0 = (float) x, x1 = (float) (x + w);

    g.setColour (colours::line);
    g.fillRoundedRectangle (juce::Rectangle<float> (x0, cy - 1.0f, x1 - x0, 2.0f), 1.0f);

    const float from = bipolar ? (x0 + x1) * 0.5f : x0;
    g.setColour (active ? colours::gold : colours::lavender);
    g.fillRoundedRectangle (juce::Rectangle<float> (std::min (from, pos), cy - 1.0f, std::abs (pos - from), 2.0f), 1.0f);

    g.setColour (active ? colours::gold : colours::text);
    g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ pos, cy }));
}

juce::Font LookAndFeel::getPopupMenuFont() { return mono (12.5f); }

void LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (colours::surface);
    g.setColour (colours::line);
    g.drawRect (0, 0, w, h);
}

juce::Rectangle<int> LookAndFeel::getTooltipBounds (const juce::String& text, juce::Point<int> pos,
                                                    juce::Rectangle<int> parentArea)
{
    const int w = std::min (340, juce::GlyphArrangement::getStringWidthInt (mono (11.5f), text) + 24);
    const int h = 28;
    return juce::Rectangle<int> (pos.x > parentArea.getCentreX() ? pos.x - (w + 12) : pos.x + 14,
                                 pos.y > parentArea.getCentreY() ? pos.y - (h + 8)  : pos.y + 18,
                                 w, h).constrainedWithin (parentArea);
}

void LookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    g.setColour (colours::surface);
    g.fillRoundedRectangle (juce::Rectangle<int> (0, 0, w, h).toFloat(), 6.0f);
    g.setColour (colours::line);
    g.drawRoundedRectangle (juce::Rectangle<int> (0, 0, w, h).toFloat().reduced (0.5f), 6.0f, 1.0f);
    g.setColour (colours::text);
    g.setFont (mono (11.5f));
    g.drawFittedText (text, juce::Rectangle<int> (0, 0, w, h).reduced (12, 2), juce::Justification::centredLeft, 1);
}

//==============================================================================
BoundSlider::BoundSlider (juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
                          juce::Slider::SliderStyle style, const juce::String& tooltip, bool bipolar)
{
    slider.setSliderStyle (style);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (pi * 1.25f, pi * 2.75f, true);
    slider.setMouseDragSensitivity (240);
    slider.setVelocityModeParameters (0.5, 1, 0.05, true, juce::ModifierKeys::shiftModifier);   // Shift = fine
    slider.getProperties().set ("bipolar", bipolar);
    slider.setTooltip (tooltip);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);
    if (auto* param = state.getParameter (paramId))
        slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
}

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& labelText,
            const juce::String& tooltip, bool bipolar)
    : bound (state, paramId, juce::Slider::RotaryHorizontalVerticalDrag, tooltip, bipolar), label (labelText.toLowerCase())
{
    bound.slider.onValueChange = [this] { repaint(); };
    bound.slider.onDragStart = bound.slider.onDragEnd = [this] { repaint(); };
    addAndMakeVisible (bound.slider);
}

void Knob::resized()
{
    bound.slider.setBounds ((getWidth() - 60) / 2, 0, 60, 60);
}

void Knob::paint (juce::Graphics& g)
{
    const bool active = bound.slider.isMouseOverOrDragging();
    g.setFont (mono (10.5f, 0.06f));
    g.setColour (colours::dim);
    g.drawText (label, 0, 66, getWidth(), 14, juce::Justification::centred, false);
    g.setFont (mono (11.5f));
    g.setColour (active ? colours::gold : colours::text);
    g.drawText (bound.valueText(), 0, 82, getWidth(), 16, juce::Justification::centred, false);
}

//==============================================================================
SliderRow::SliderRow (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& labelText,
                      const juce::String& tooltip, bool bipolar)
    : bound (state, paramId, juce::Slider::LinearHorizontal, tooltip, bipolar), label (labelText.toLowerCase())
{
    bound.slider.onValueChange = [this] { repaint(); };
    bound.slider.onDragStart = bound.slider.onDragEnd = [this] { repaint(); };
    addAndMakeVisible (bound.slider);
}

void SliderRow::resized()
{
    bound.slider.setBounds (0, 20, getWidth(), 18);
}

void SliderRow::paint (juce::Graphics& g)
{
    const bool active = bound.slider.isMouseOverOrDragging();
    const auto textRow = juce::Rectangle<int> (0, 0, getWidth(), 16).reduced (9, 0);   // align with the track ends
    g.setFont (mono (10.5f, 0.06f));
    g.setColour (active ? colours::text : colours::dim);
    g.drawText (label, textRow, juce::Justification::centredLeft, false);
    g.setColour (active ? colours::gold : colours::text.withAlpha (0.85f));
    g.drawText (bound.valueText(), textRow, juce::Justification::centredRight, false);
}

//==============================================================================
FreezeButton::FreezeButton() : juce::Button ("Freeze")
{
    setClickingTogglesState (true);
    setTooltip ("Capture the last moment of sound and hold it as a pad. Click again to release.");
}

void FreezeButton::tick (float dt, bool reducedMotion)
{
    on.target = getToggleState() ? 1.0f : 0.0f;
    press.target = isDown() ? 1.0f : 0.0f;
    bool changed = on.update (dt, 0.1f);                              // ~300 ms cross-fade
    changed |= press.update (dt, reducedMotion ? 1.0e-4f : 0.05f);   // 150 ms ease-out press to 0.96
    if (changed) repaint();
}

void FreezeButton::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    const float r = 56.0f * (1.0f - 0.04f * press.value);
    const auto disc = juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c);
    const float a = on.value;

    g.setColour (colours::control);
    g.fillEllipse (disc);
    if (a > 0.0f)
    {
        g.setColour (colours::gold.withAlpha (a));
        g.fillEllipse (disc);
    }
    g.setColour ((highlighted ? colours::lavender : colours::line).withAlpha (1.0f - a));
    g.drawEllipse (disc.reduced (0.75f), 1.5f);

    const bool frozen = getToggleState();
    g.setFont (mono (12.5f, 0.2f));
    g.setColour (frozen ? colours::background : (highlighted ? colours::text : colours::dim));
    g.drawText (frozen ? "frozen" : "freeze", disc, juce::Justification::centred, false);
}

//==============================================================================
void TextLink::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto r = getLocalBounds().toFloat();
    g.setFont (mono (11.0f, 0.04f));
    g.setColour (selected ? colours::text : highlighted ? colours::text : colours::dim);
    g.drawText (getButtonText(), r, juce::Justification::centredLeft, false);

    if (selected)
    {
        const float tw = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), getButtonText());
        g.setColour (colours::gold);
        g.fillRect (juce::Rectangle<float> (r.getX(), r.getBottom() - 2.0f, tw, 1.0f));
    }
}

//==============================================================================
SpectrumView::SpectrumView (VisualState& state) : vs (state)
{
    thumbImage = juce::Image (juce::Image::ARGB, VisualState::thumbCols, VisualState::thumbRows, true);
    setInterceptsMouseClicks (false, false);
}

juce::Rectangle<float> SpectrumView::curvesArea() const { auto r = getLocalBounds().toFloat(); r.removeFromBottom (28.0f + 18.0f); return r; }
juce::Rectangle<float> SpectrumView::stripArea() const  { auto r = getLocalBounds().toFloat(); return r.removeFromBottom (28.0f); }

void SpectrumView::rebuildThumbnail()
{
    thumbCols = vs.thumbNumCols.load();
    juce::Image::BitmapData bd (thumbImage, juce::Image::BitmapData::writeOnly);
    for (int c = 0; c < VisualState::thumbCols; ++c)
        for (int r = 0; r < VisualState::thumbRows; ++r)
        {
            const float v = c < thumbCols ? vs.thumb[(size_t) (r * VisualState::thumbCols + c)].load (std::memory_order_relaxed) : 0.0f;
            const float t = juce::jlimit (0.0f, 1.0f, (v - 0.3f) / 0.6f);
            bd.setPixelColour (c, VisualState::thumbRows - 1 - r, colours::well.interpolatedWith (colours::lavender, t));
        }
}

void SpectrumView::tick()
{
    for (int b = 0; b < VisualState::numBands; ++b)
    {
        const float in = vs.inputBands[(size_t) b].load (std::memory_order_relaxed);
        const float pd = vs.padBands[(size_t) b].load (std::memory_order_relaxed);
        auto& si = input[(size_t) b];
        auto& sp = pad[(size_t) b];
        si += (in - si) * (in > si ? 0.6f : 0.18f);
        sp += (pd - sp) * (pd > sp ? 0.5f : 0.2f);
    }

    frozen = vs.frozen.load();
    regionStart += (vs.regionStart.load() - regionStart) * 0.3f;
    for (int v = 0; v < VisualState::numVoices; ++v)
    {
        playheads[(size_t) v] += (vs.playhead[(size_t) v].load() - playheads[(size_t) v]) * 0.35f;
        levels[(size_t) v] = vs.voiceLevel[(size_t) v].load();
    }

    const int version = vs.thumbVersion.load();
    if (version != lastThumbVersion)
    {
        lastThumbVersion = version;
        rebuildThumbnail();
    }
    repaint();
}

juce::Path SpectrumView::makeCurve (const std::array<float, VisualState::numBands>& values,
                                    juce::Rectangle<float> area, bool closed) const
{
    juce::Path path;
    const int n = VisualState::numBands;
    auto pointAt = [&] (int b)
    {
        return juce::Point<float> (area.getX() + area.getWidth() * ((float) b + 0.5f) / (float) n,
                                   area.getBottom() - area.getHeight() * values[(size_t) b]);
    };
    path.startNewSubPath (closed ? juce::Point<float> (area.getX(), area.getBottom()) : pointAt (0));
    if (closed) path.lineTo (area.getX(), pointAt (0).y);
    for (int b = 0; b < n - 1; ++b)
    {
        const auto p0 = pointAt (b), p1 = pointAt (b + 1);
        path.quadraticTo (p0, (p0 + p1) * 0.5f);
    }
    path.lineTo (area.getRight(), pointAt (n - 1).y);
    if (closed)
    {
        path.lineTo (area.getRight(), area.getBottom());
        path.closeSubPath();
    }
    return path;
}

void SpectrumView::paint (juce::Graphics& g)
{
    const auto curves = curvesArea();
    const auto strip = stripArea();

    g.setFont (mono (9.5f));
    g.setColour (colours::faint);
    for (float hz : { 100.0f, 1000.0f, 10000.0f })
    {
        const float x = curves.getX() + curves.getWidth() * freqToNorm (hz);
        g.drawText (hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz),
                    juce::Rectangle<float> (x - 20.0f, curves.getBottom() + 2.0f, 40.0f, 12.0f), juce::Justification::centred, false);
    }

    g.setColour (colours::text.withAlpha (0.18f));
    g.strokePath (makeCurve (input, curves, false), juce::PathStrokeType (1.0f));

    float padPeak = 0.0f;
    for (float v : pad) padPeak = std::max (padPeak, v);
    if (padPeak > 0.01f)
    {
        const float presence = juce::jlimit (0.0f, 1.0f, padPeak * 2.0f);
        juce::ColourGradient fill (colours::lavender.withAlpha (0.14f * presence), 0.0f, curves.getY(),
                                   colours::lavender.withAlpha (0.0f), 0.0f, curves.getBottom(), false);
        g.setGradientFill (fill);
        g.fillPath (makeCurve (pad, curves, true));
        g.setColour (colours::lavender.withAlpha (presence));
        g.strokePath (makeCurve (pad, curves, false), juce::PathStrokeType (1.6f));
    }

    // Captured slice
    g.setColour (colours::well);
    g.fillRoundedRectangle (strip, 4.0f);
    if (thumbCols > 0)
    {
        juce::Graphics::ScopedSaveState s (g);
        juce::Path clip;
        clip.addRoundedRectangle (strip, 4.0f);
        g.reduceClipRegion (clip);
        g.setOpacity (frozen ? 1.0f : 0.35f);
        g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
        g.drawImage (thumbImage.getClippedImage ({ 0, 0, thumbCols, VisualState::thumbRows }), strip,
                     juce::RectanglePlacement::stretchToFit);
        g.setOpacity (1.0f);

        if (frozen)
        {
            const float rx = strip.getX() + strip.getWidth() * regionStart;
            g.setColour (colours::well.withAlpha (0.65f));
            g.fillRect (strip.withRight (rx));
            g.setColour (colours::gold.withAlpha (0.8f));
            g.drawRoundedRectangle (juce::Rectangle<float> (rx, strip.getY(), strip.getRight() - rx, strip.getHeight()).reduced (0.5f), 4.0f, 1.0f);
        }
        for (int v = 0; v < VisualState::numVoices; ++v)
            if (levels[(size_t) v] > 0.001f)
            {
                const float x = strip.getX() + strip.getWidth() * juce::jlimit (0.0f, 1.0f, playheads[(size_t) v]);
                g.setColour (colours::text.withAlpha (0.9f * levels[(size_t) v]));
                g.fillRect (juce::Rectangle<float> (x - 0.75f, strip.getY(), 1.5f, strip.getHeight()));
            }
    }
    else
    {
        g.setColour (colours::faint);
        g.setFont (mono (10.0f, 0.04f));
        g.drawText ("nothing captured yet", strip, juce::Justification::centred, false);
    }
}

//==============================================================================
SettingsPage::SettingsPage (SpectraProcessor& proc)
{
    auto& s = proc.apvts;
    auto add = [&] (const char* id, const char* name, const char* tip, bool bipolar = false)
    {
        rows.push_back (std::make_unique<SliderRow> (s, id, name, tip, bipolar));
        addAndMakeVisible (*rows.back());
    };
    add ("scan",    "scan",    "Where in the captured slice the pad plays from.");
    add ("drift",   "drift",   "Slow random wandering through the captured slice.");
    add ("spread",  "spread",  "Random frame jitter for a grainy texture.");
    add ("absorb",  "absorb",  "While frozen, slowly blend new input into the pad.");
    add ("blur",    "blur",    "Smear neighbouring frequencies together.");
    add ("shimmer", "shimmer", "Blend in a copy of the pad one octave up.");
    add ("tilt",    "tone",    "Darker to the left, brighter to the right.", true);
    add ("width",   "width",   "Mono at 0, wide stereo at 100%.");
    add ("attack",  "attack",  "Fade-in time when you freeze.");
    add ("release", "release", "Fade-out time when you release or re-freeze.");
    add ("level",   "level",   "Output level of the pad.");

    randomize.setTooltip ("Roll new values for scan, drift, spread, diffuse, blur, tone, width and shimmer.");
    randomize.onClick = [&proc] { proc.randomizeTexture(); };
    addAndMakeVisible (randomize);
}

void SettingsPage::resized()
{
    const int gap = 36, colW = (getWidth() - gap) / 2, rowH = 50;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const int col = (int) i / 6, row = (int) i % 6;
        rows[i]->setBounds (col * (colW + gap), 4 + row * rowH, colW, 40);
    }
    randomize.setBounds (colW + gap + 9, 4 + 5 * rowH + 8, colW - 9, 22);
}

//==============================================================================
Screen::Screen (SpectraProcessor& proc) : spectrum (proc.getVisualState()), settings (proc)
{
    addAndMakeVisible (spectrum);
    addChildComponent (settings);
    addAndMakeVisible (spectrumTab);
    addAndMakeVisible (settingsTab);
    spectrumTab.setSelected (true);
    spectrumTab.onClick = [this] { showSettings (false); };
    settingsTab.onClick = [this] { showSettings (true); };
}

void Screen::showSettings (bool show)
{
    settingsAmount.target = show ? 1.0f : 0.0f;
    spectrumTab.setSelected (! show);
    settingsTab.setSelected (show);
    if (show) settings.setVisible (true);
    settings.setInterceptsMouseClicks (show, show);
}

void Screen::resized()
{
    spectrumTab.setBounds (24, 16, 72, 22);
    settingsTab.setBounds (104, 16, 72, 22);
    const auto content = getLocalBounds().withTrimmedTop (56).reduced (24, 0).withTrimmedBottom (24);
    spectrum.setBounds (content);
    settings.setBounds (content);
}

void Screen::paint (juce::Graphics& g)
{
    g.setColour (colours::surface);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 12.0f);
}

void Screen::tick (float dt, bool)
{
    spectrumAccumulator += dt;
    if (spectrumAccumulator >= 1.0f / 30.0f)
    {
        spectrumAccumulator = std::fmod (spectrumAccumulator, 1.0f / 30.0f);
        spectrum.tick();
    }

    // Tab switch: a short opacity cross-fade only (high-frequency interaction).
    if (settingsAmount.update (dt, 0.05f) || settings.isVisible())
    {
        const float a = settingsAmount.value;
        settings.setAlpha (a);
        spectrum.setAlpha (1.0f - a);
        if (a <= 0.0f && settingsAmount.target <= 0.0f) settings.setVisible (false);
    }
}

//==============================================================================
void PresetNav::Chevron::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    const float s = left ? 1.0f : -1.0f;
    juce::Path p;
    p.startNewSubPath (c.x + 2.5f * s, c.y - 5.0f);
    p.lineTo (c.x - 2.5f * s, c.y);
    p.lineTo (c.x + 2.5f * s, c.y + 5.0f);
    g.setColour (highlighted ? colours::gold : colours::dim);
    g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

PresetNav::PresetNav (SpectraProcessor& p) : proc (p)
{
    prev.onClick = [this] { step (-1); };
    next.onClick = [this] { step (1); };
    prev.setTooltip ("Previous preset");
    next.setTooltip ("Next preset");
    addAndMakeVisible (prev);
    addAndMakeVisible (next);
}

juce::Rectangle<int> PresetNav::nameArea() const { return getLocalBounds().reduced (28, 0); }

void PresetNav::resized()
{
    prev.setBounds (getLocalBounds().removeFromLeft (24));
    next.setBounds (getLocalBounds().removeFromRight (24));
}

void PresetNav::step (int delta)
{
    const int n = proc.getNumPrograms();
    proc.setCurrentProgram ((proc.getCurrentProgram() + delta + n) % n);
    refresh();
}

void PresetNav::refresh()
{
    if (shownProgram != proc.getCurrentProgram())
    {
        shownProgram = proc.getCurrentProgram();
        repaint();
    }
}

void PresetNav::mouseUp (const juce::MouseEvent& e)
{
    if (! nameArea().contains (e.getPosition())) return;
    juce::PopupMenu menu;
    for (int i = 0; i < proc.getNumPrograms(); ++i)
        menu.addItem (i + 1, proc.getProgramName (i).toLowerCase(), true, i == proc.getCurrentProgram());
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth()),
                        [this] (int result) { if (result > 0) { proc.setCurrentProgram (result - 1); refresh(); } });
}

void PresetNav::paint (juce::Graphics& g)
{
    const bool hover = isMouseOver (false);
    g.setFont (light (16.0f, 0.04f));
    g.setColour (hover ? colours::text : colours::text.withAlpha (0.85f));
    g.drawText (proc.getProgramName (proc.getCurrentProgram()).toLowerCase(), nameArea(), juce::Justification::centred, false);
}

//==============================================================================
Content::Content (SpectraProcessor& proc, std::function<void (float)> scaleCallback)
    : spectraProcessor (proc), onScaleChosen (std::move (scaleCallback)), presetNav (proc), screen (proc)
{
    auto& s = proc.apvts;
    knobs.push_back (std::make_unique<Knob> (s, "size",    "size",    "How much of the last moment is captured, one frame up to 2 s."));
    knobs.push_back (std::make_unique<Knob> (s, "diffuse", "diffuse", "Tonal and pitched at 0, an airy wash at 100%."));
    knobs.push_back (std::make_unique<Knob> (s, "shift",   "shift",   "Pitch in semitones. Hold Shift to drag finely.", true));
    knobs.push_back (std::make_unique<Knob> (s, "mix",     "mix",     "How much the pad replaces the dry signal while frozen."));
    for (auto& k : knobs) addAndMakeVisible (*k);

    addAndMakeVisible (screen);
    addAndMakeVisible (freeze);
    addAndMakeVisible (presetNav);
    freezeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (s, "freeze", freeze);

    scaleButton.setTooltip ("Window size");
    scaleButton.onClick = [this] { showScaleMenu(); };
    addAndMakeVisible (scaleButton);

    reducedMotion = prefersReducedMotion();
    setSize (baseWidth, baseHeight);
}

void Content::setScaleLabel (float scale)
{
    scaleButton.setButtonText (juce::String (juce::roundToInt (scale * 100.0f)) + "%");
}

void Content::showScaleMenu()
{
    juce::PopupMenu menu;
    const float current = spectraProcessor.getUiScale();
    for (int i = 0; i < scaleSteps.size(); ++i)
        menu.addItem (i + 1, juce::String (juce::roundToInt (scaleSteps[i] * 100.0f)) + "%", true,
                      std::abs (scaleSteps[i] - current) < 0.01f);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&scaleButton),
                        [this] (int result) { if (result > 0 && onScaleChosen) onScaleChosen (scaleSteps[result - 1]); });
}

void Content::resized()
{
    presetNav.setBounds (baseWidth - 28 - 44 - 20 - 180, 18, 180, 30);
    scaleButton.setBounds (baseWidth - 28 - 44, 18, 44, 30);

    screen.setBounds (28, 64, 540, 408);

    const int colX = 592, colW = baseWidth - 28 - colX;
    freeze.setBounds (colX, 70, colW, 160);
    const int cellW = colW / 2, cellH = 108;
    for (size_t i = 0; i < knobs.size(); ++i)
        knobs[i]->setBounds (colX + (int) (i % 2) * cellW, 252 + (int) (i / 2) * (cellH + 6), cellW, cellH);
}

void Content::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
    g.setFont (light (22.0f, 0.12f));
    g.setColour (colours::text);
    g.drawText ("spectra", 30, 16, 200, 32, juce::Justification::centredLeft, false);
}

void Content::tick (float dt)
{
    motionCheckAccumulator += dt;
    if (motionCheckAccumulator > 1.0f)
    {
        motionCheckAccumulator = 0.0f;
        reducedMotion = prefersReducedMotion();
    }
    screen.tick (dt, reducedMotion);
    freeze.tick (dt, reducedMotion);
    presetNav.refresh();
}

} // namespace spectra::ui

//==============================================================================
SpectraEditor::SpectraEditor (SpectraProcessor& p)
    : AudioProcessorEditor (&p), spectraProcessor (p),
      content (p, [this] (float s) { applyScale (s); })
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);

    // Fixed size steps instead of free dragging: a free resize with a locked aspect ratio
    // fights the host's own resize negotiation and makes the window jump.
    setResizable (false, false);
    applyScale (spectraProcessor.getUiScale());

    lastTick = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (60);
}

SpectraEditor::~SpectraEditor()
{
    setLookAndFeel (nullptr);
}

void SpectraEditor::applyScale (float scale)
{
    using C = spectra::ui::Content;
    scale = juce::jlimit (0.75f, 1.5f, scale);
    spectraProcessor.setUiScale (scale);
    content.setScaleLabel (scale);
    setSize (juce::roundToInt ((float) C::baseWidth * scale), juce::roundToInt ((float) C::baseHeight * scale));
}

void SpectraEditor::paint (juce::Graphics& g)
{
    g.fillAll (spectra::ui::colours::background);
}

void SpectraEditor::resized()
{
    using C = spectra::ui::Content;
    const float scale = std::min ((float) getWidth() / (float) C::baseWidth, (float) getHeight() / (float) C::baseHeight);
    const float x = ((float) getWidth()  - (float) C::baseWidth  * scale) * 0.5f;
    const float y = ((float) getHeight() - (float) C::baseHeight * scale) * 0.5f;
    content.setBounds (0, 0, C::baseWidth, C::baseHeight);
    content.setTransform (juce::AffineTransform::scale (scale).translated (x, y));
}

void SpectraEditor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = (float) juce::jlimit (0.0, 0.1, (now - lastTick) * 0.001);
    lastTick = now;
    content.tick (dt);
}
