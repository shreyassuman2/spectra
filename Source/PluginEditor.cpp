#include "PluginEditor.h"
#include "Presets.h"

namespace spectra::ui
{

namespace
{
    juce::Font font (float size, bool bold = false, float tracking = 0.0f)
    {
        auto opts = juce::FontOptions (size).withKerningFactor (tracking);
        return juce::Font (bold ? opts.withStyle ("Bold") : opts);
    }

    juce::Colour accentAt (float t)   // 0 = cyan, 1 = violet
    {
        return colours::cyan.interpolatedWith (colours::violet, juce::jlimit (0.0f, 1.0f, t));
    }

    float freqToX (float hz) { return std::log (hz / 20.0f) / std::log (1000.0f); }
}

//==============================================================================
LookAndFeel::LookAndFeel()
{
    setColour (juce::ComboBox::backgroundColourId, colours::panel);
    setColour (juce::ComboBox::outlineColourId, colours::hairline);
    setColour (juce::ComboBox::textColourId, colours::text);
    setColour (juce::ComboBox::arrowColourId, colours::dim);
    setColour (juce::PopupMenu::backgroundColourId, colours::panel);
    setColour (juce::PopupMenu::textColourId, colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::panelHi);
    setColour (juce::PopupMenu::highlightedTextColourId, colours::cyan);
    setColour (juce::TextButton::buttonColourId, colours::panel);
    setColour (juce::TextButton::textColourOffId, colours::text);
    setColour (juce::TextButton::textColourOnId, colours::cyan);
    setColour (juce::TooltipWindow::backgroundColourId, colours::panelHi);
    setColour (juce::TooltipWindow::textColourId, colours::text);
    setColour (juce::TooltipWindow::outlineColourId, colours::hairline);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                    float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (3.0f);
    const float radius = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float arcRadius = radius - 2.0f;
    const float angle = startAngle + pos * (endAngle - startAngle);
    const bool bipolar = slider.getProperties()["bipolar"];
    const bool hover = slider.isMouseOverOrDragging();

    // track
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (colours::hairline);
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // value arc
    const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
    if (std::abs (angle - from) > 0.001f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                             std::min (from, angle), std::max (from, angle), true);
        juce::ColourGradient grad (colours::cyan, centre.x - radius, centre.y + radius,
                                   colours::violet, centre.x + radius, centre.y - radius, false);
        g.setGradientFill (grad);
        g.strokePath (value, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // body
    const float bodyR = radius - 8.0f;
    juce::ColourGradient body (colours::panelHi.brighter (hover ? 0.12f : 0.05f), centre.x, centre.y - bodyR,
                               colours::panel, centre.x, centre.y + bodyR, false);
    g.setGradientFill (body);
    g.fillEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
    g.setColour (colours::hairline.brighter (hover ? 0.3f : 0.0f));
    g.drawEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);

    // indicator dot
    const float dotR = bodyR - 6.0f;
    const auto dot = centre.getPointOnCircumference (dotR, angle);
    g.setColour (colours::text);
    g.fillEllipse (dot.x - 2.2f, dot.y - 2.2f, 4.4f, 4.4f);
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                        bool highlighted, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (down ? colours::panelHi.brighter (0.1f) : highlighted ? colours::panelHi : colours::panel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (highlighted ? colours::cyan.withAlpha (0.5f) : colours::hairline);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<int> (0, 0, w, h).toFloat().reduced (0.5f);
    const bool hover = box.isMouseOver (true);
    g.setColour (hover ? colours::panelHi : colours::panel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (hover ? colours::cyan.withAlpha (0.5f) : colours::hairline);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);

    juce::Path arrow;
    const float ax = (float) w - 18.0f, ay = (float) h * 0.5f;
    arrow.startNewSubPath (ax - 4.0f, ay - 2.0f);
    arrow.lineTo (ax, ay + 2.0f);
    arrow.lineTo (ax + 4.0f, ay - 2.0f);
    g.setColour (colours::dim);
    g.strokePath (arrow, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox&)          { return font (13.0f); }
juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int) { return font (12.5f, false, 0.04f); }
juce::Font LookAndFeel::getPopupMenuFont()                         { return font (13.5f); }

juce::Rectangle<int> LookAndFeel::getTooltipBounds (const juce::String& text, juce::Point<int> pos,
                                                    juce::Rectangle<int> parentArea)
{
    const int w = std::min (300, juce::GlyphArrangement::getStringWidthInt (font (12.5f), text) + 22);
    const int h = 26;
    return juce::Rectangle<int> (pos.x > parentArea.getCentreX() ? pos.x - (w + 12) : pos.x + 14,
                                 pos.y > parentArea.getCentreY() ? pos.y - (h + 8)  : pos.y + 16,
                                 w, h).constrainedWithin (parentArea);
}

void LookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    const auto r = juce::Rectangle<int> (0, 0, w, h).toFloat();
    g.setColour (colours::panelHi);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (colours::hairline.brighter (0.2f));
    g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
    g.setColour (colours::text);
    g.setFont (font (12.5f));
    g.drawFittedText (text, juce::Rectangle<int> (0, 0, w, h).reduced (10, 2), juce::Justification::centredLeft, 1);
}

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
            const juce::String& labelText, const juce::String& tooltip, bool bipolar)
    : label (labelText.toUpperCase())
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setMouseDragSensitivity (220);
    slider.getProperties().set ("bipolar", bipolar);
    slider.setTooltip (tooltip);
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);

    if (auto* param = state.getParameter (paramId))
        slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
}

void Knob::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (16);
    r.removeFromBottom (18);
    const int side = std::min (r.getWidth(), r.getHeight());
    slider.setBounds (r.withSizeKeepingCentre (side, side));
}

void Knob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    g.setFont (font (10.5f, true, 0.12f));
    g.setColour (colours::dim);
    g.drawText (label, r.removeFromTop (16), juce::Justification::centred, false);

    g.setFont (font (12.0f));
    g.setColour (slider.isMouseOverOrDragging() ? colours::cyan : colours::text);
    g.drawText (slider.getTextFromValue (slider.getValue()), r.removeFromBottom (18),
                juce::Justification::centred, false);
}

//==============================================================================
FreezeOrb::FreezeOrb() : juce::Button ("Freeze")
{
    setClickingTogglesState (true);
    setTooltip ("Capture the last moment of sound and hold it as a pad. Click again to release.");
}

void FreezeOrb::setLevel (float newLevel, float time)
{
    level = newLevel;
    phase = time;
    repaint();
}

void FreezeOrb::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto centre = bounds.getCentre().translated (0.0f, -10.0f);
    const float r = std::min (bounds.getWidth(), bounds.getHeight() - 20.0f) * 0.36f * (down ? 0.97f : 1.0f);
    const bool on = getToggleState();
    const float pulse = 0.85f + 0.15f * std::sin (phase * 2.2f);
    const float glow = on ? (0.35f + 0.65f * level) * pulse : (highlighted ? 0.18f : 0.08f);

    // halo
    {
        const float rr = r * 1.75f;
        juce::ColourGradient halo (accentAt (0.3f).withAlpha (0.55f * glow), centre.x, centre.y,
                                   accentAt (0.9f).withAlpha (0.0f), centre.x + rr, centre.y, true);
        halo.addColour (0.55, accentAt (0.6f).withAlpha (0.22f * glow));
        g.setGradientFill (halo);
        g.fillEllipse (centre.x - rr, centre.y - rr, rr * 2.0f, rr * 2.0f);
    }

    // body
    if (on)
    {
        juce::ColourGradient grad (colours::cyan.brighter (0.2f).withAlpha (0.95f), centre.x - r * 0.3f, centre.y - r * 0.4f,
                                   colours::violet.withAlpha (0.9f), centre.x + r, centre.y + r, true);
        g.setGradientFill (grad);
    }
    else
    {
        juce::ColourGradient grad (colours::panelHi.brighter (highlighted ? 0.15f : 0.05f), centre.x, centre.y - r,
                                   colours::panel, centre.x, centre.y + r, false);
        g.setGradientFill (grad);
    }
    g.fillEllipse (centre.x - r, centre.y - r, r * 2.0f, r * 2.0f);

    // frosted rim + specular
    g.setColour (on ? juce::Colours::white.withAlpha (0.35f) : colours::hairline.brighter (highlighted ? 0.4f : 0.15f));
    g.drawEllipse (centre.x - r, centre.y - r, r * 2.0f, r * 2.0f, 1.2f);
    g.setColour (juce::Colours::white.withAlpha (on ? 0.22f : 0.04f));
    g.fillEllipse (centre.x - r * 0.55f, centre.y - r * 0.8f, r * 1.1f, r * 0.55f);

    g.setFont (font (13.0f, true, 0.3f));
    g.setColour (on ? colours::background : colours::text.withAlpha (highlighted ? 0.95f : 0.7f));
    g.drawText (on ? "FROZEN" : "FREEZE", juce::Rectangle<float> (centre.x - r, centre.y - 10.0f, r * 2.0f, 20.0f),
                juce::Justification::centred, false);

    g.setFont (font (10.5f, false, 0.08f));
    g.setColour (colours::dim);
    g.drawText (on ? "click to release" : "click to capture",
                bounds.withTop (bounds.getBottom() - 20.0f), juce::Justification::centred, false);
}

//==============================================================================
Display::Display (VisualState& state) : vs (state)
{
    particles.reserve (160);
    thumbImage = juce::Image (juce::Image::ARGB, VisualState::thumbCols, VisualState::thumbRows, true);
    setInterceptsMouseClicks (false, false);
}

void Display::rebuildThumbnail()
{
    thumbCols = vs.thumbNumCols.load();
    juce::Image::BitmapData bd (thumbImage, juce::Image::BitmapData::writeOnly);
    for (int c = 0; c < VisualState::thumbCols; ++c)
        for (int r = 0; r < VisualState::thumbRows; ++r)
        {
            const float v = c < thumbCols ? vs.thumb[(size_t) (r * VisualState::thumbCols + c)].load (std::memory_order_relaxed) : 0.0f;
            const float t = juce::jlimit (0.0f, 1.0f, (v - 0.3f) / 0.6f);
            juce::Colour col;
            if (t < 0.5f)       col = colours::background.interpolatedWith (juce::Colour (0xff2a6a8c), t * 2.0f);
            else if (t < 0.85f) col = juce::Colour (0xff2a6a8c).interpolatedWith (colours::violet, (t - 0.5f) / 0.35f);
            else                col = colours::violet.interpolatedWith (juce::Colours::white, (t - 0.85f) / 0.15f);
            bd.setPixelColour (c, VisualState::thumbRows - 1 - r, col);
        }
}

void Display::spawnParticle (juce::Rectangle<float> area)
{
    // Spawn near loud parts of the pad spectrum.
    int band = rng.nextInt (VisualState::numBands);
    for (int tries = 0; tries < 3; ++tries)
    {
        const int other = rng.nextInt (VisualState::numBands);
        if (pad[(size_t) other] > pad[(size_t) band]) band = other;
    }
    const float x = area.getX() + area.getWidth() * ((float) band + rng.nextFloat()) / (float) VisualState::numBands;
    const float yCurve = area.getBottom() - area.getHeight() * pad[(size_t) band];
    const float y = juce::jlimit (area.getY(), area.getBottom(), yCurve + rng.nextFloat() * 40.0f - 10.0f);

    Particle pt;
    pt.x = x; pt.y = y;
    pt.vx = (rng.nextFloat() - 0.5f) * 0.4f;
    pt.vy = -(0.15f + rng.nextFloat() * 0.6f) * (0.5f + energy);
    pt.maxLife = 60.0f + rng.nextFloat() * 110.0f;
    pt.life = pt.maxLife;
    pt.size = 1.2f + rng.nextFloat() * 2.8f;
    pt.hue = juce::jlimit (0.0f, 1.0f, centroid + (rng.nextFloat() - 0.5f) * 0.5f);
    particles.push_back (pt);
}

void Display::tick()
{
    // Smooth the spectra
    float sum = 0.0f, wSum = 0.0f, cSum = 0.0f;
    for (int b = 0; b < VisualState::numBands; ++b)
    {
        const float in = vs.inputBands[(size_t) b].load (std::memory_order_relaxed);
        const float pd = vs.padBands[(size_t) b].load (std::memory_order_relaxed);
        auto& si = input[(size_t) b];
        auto& sp = pad[(size_t) b];
        si += (in - si) * (in > si ? 0.6f : 0.18f);
        sp += (pd - sp) * (pd > sp ? 0.5f : 0.2f);
        sum += sp;
        const float w = sp * sp * sp * sp;
        wSum += w;
        cSum += w * (float) b;
    }
    const float mean = sum / (float) VisualState::numBands;
    const float targetEnergy = juce::jlimit (0.0f, 1.0f, (mean - 0.12f) / 0.4f);
    energy += (targetEnergy - energy) * 0.12f;
    if (wSum > 1.0e-6f)
        centroid += (cSum / wSum / (float) VisualState::numBands - centroid) * 0.08f;

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

    // Particles
    const auto area = getLocalBounds().toFloat().reduced (12.0f).withTrimmedBottom (62.0f);
    spawnAccumulator += 0.25f + energy * 5.0f;
    while (spawnAccumulator >= 1.0f && particles.size() < 150)
    {
        spawnParticle (area);
        spawnAccumulator -= 1.0f;
    }
    spawnAccumulator = std::min (spawnAccumulator, 1.0f);

    for (auto& pt : particles)
    {
        pt.x += pt.vx + 0.25f * std::sin (pt.life * 0.05f + pt.hue * 6.0f);
        pt.y += pt.vy;
        pt.life -= 1.0f;
    }
    particles.erase (std::remove_if (particles.begin(), particles.end(),
                                     [&area] (const Particle& pt) { return pt.life <= 0.0f || pt.y < area.getY(); }),
                     particles.end());
    repaint();
}

juce::Path Display::makeCurve (const std::array<float, VisualState::numBands>& values,
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

void Display::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (colours::panel);
    g.fillRoundedRectangle (bounds, 10.0f);
    g.setColour (colours::hairline);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 10.0f, 1.0f);

    auto inner = bounds.reduced (12.0f);
    const auto strip = inner.removeFromBottom (50.0f);
    inner.removeFromBottom (12.0f);
    const auto curves = inner;

    // Grid
    g.setFont (font (10.0f, false, 0.05f));
    for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = curves.getX() + curves.getWidth() * freqToX (hz);
        g.setColour (colours::hairline.withAlpha (0.6f));
        g.drawVerticalLine ((int) x, curves.getY(), curves.getBottom());
        g.setColour (colours::dim.withAlpha (0.7f));
        const juce::String label = hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz);
        g.drawText (label, juce::Rectangle<float> (x + 3.0f, curves.getBottom() - 14.0f, 30.0f, 12.0f),
                    juce::Justification::left, false);
    }

    // Layer 1: particles
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (curves.toNearestInt());
        for (const auto& pt : particles)
        {
            const float lifeRatio = pt.life / pt.maxLife;
            const float alpha = std::sin (lifeRatio * juce::MathConstants<float>::pi) * (0.18f + 0.6f * energy);
            const auto col = accentAt (pt.hue);
            const float glowSize = pt.size * 3.5f;
            g.setColour (col.withAlpha (alpha * 0.18f));
            g.fillEllipse (pt.x - glowSize * 0.5f, pt.y - glowSize * 0.5f, glowSize, glowSize);
            g.setColour (col.withAlpha (alpha));
            g.fillEllipse (pt.x - pt.size * 0.5f, pt.y - pt.size * 0.5f, pt.size, pt.size);
        }
    }

    // Layer 2: spectrum curves
    g.setColour (colours::text.withAlpha (0.05f));
    g.fillPath (makeCurve (input, curves, true));
    g.setColour (colours::text.withAlpha (0.28f));
    g.strokePath (makeCurve (input, curves, false), juce::PathStrokeType (1.0f));

    float padPeak = 0.0f;
    for (float v : pad) padPeak = std::max (padPeak, v);
    if (padPeak > 0.01f)
    {
        const float presence = juce::jlimit (0.0f, 1.0f, padPeak * 2.0f);
        juce::ColourGradient fill (colours::cyan.withAlpha (0.22f * presence), curves.getX(), 0.0f,
                                   colours::violet.withAlpha (0.22f * presence), curves.getRight(), 0.0f, false);
        g.setGradientFill (fill);
        g.fillPath (makeCurve (pad, curves, true));

        juce::ColourGradient line (colours::cyan.withAlpha (presence), curves.getX(), 0.0f,
                                   colours::violet.withAlpha (presence), curves.getRight(), 0.0f, false);
        g.setGradientFill (line);
        g.strokePath (makeCurve (pad, curves, false), juce::PathStrokeType (1.8f));
    }

    // Layer 3: captured-slice spectrogram strip
    g.setColour (colours::background);
    g.fillRoundedRectangle (strip, 6.0f);

    if (thumbCols > 0)
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (strip.toNearestInt());
        g.setOpacity (frozen ? 1.0f : 0.35f);
        g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
        g.drawImage (thumbImage.getClippedImage ({ 0, 0, thumbCols, VisualState::thumbRows }), strip,
                     juce::RectanglePlacement::stretchToFit);
        g.setOpacity (1.0f);

        if (frozen)
        {
            // Inactive (outside Size) region dimmed, active region bracketed
            const float rx = strip.getX() + strip.getWidth() * regionStart;
            g.setColour (colours::background.withAlpha (0.6f));
            g.fillRect (strip.withRight (rx));
            g.setColour (colours::cyan.withAlpha (0.7f));
            g.drawRect (juce::Rectangle<float> (rx, strip.getY(), strip.getRight() - rx, strip.getHeight()), 1.0f);
        }

        for (int v = 0; v < VisualState::numVoices; ++v)
            if (levels[(size_t) v] > 0.001f)
            {
                const float x = strip.getX() + strip.getWidth() * juce::jlimit (0.0f, 1.0f, playheads[(size_t) v]);
                g.setColour (juce::Colours::white.withAlpha (0.85f * levels[(size_t) v]));
                g.fillRect (juce::Rectangle<float> (x - 1.0f, strip.getY(), 2.0f, strip.getHeight()));
            }
    }
    else
    {
        g.setColour (colours::dim);
        g.setFont (font (11.0f, false, 0.1f));
        g.drawText ("NOTHING CAPTURED YET - PRESS FREEZE", strip, juce::Justification::centred, false);
    }

    g.setColour (colours::hairline);
    g.drawRoundedRectangle (strip, 6.0f, 1.0f);
}

//==============================================================================
Content::Content (SpectraProcessor& proc)
    : spectraProcessor (proc), display (proc.getVisualState())
{
    auto& s = proc.apvts;
    auto make = [&s] (const char* id, const char* name, const char* tip, bool bipolar = false)
    {
        return std::make_unique<Knob> (s, id, name, tip, bipolar);
    };

    row1.push_back (make ("size",    "Size",    "How much of the last moment is captured: one frame (classic freeze) up to 2 seconds."));
    row1.push_back (make ("absorb",  "Absorb",  "While frozen, slowly blend new input into the pad so it evolves with the song."));
    row1.push_back (make ("scan",    "Scan",    "Where in the captured slice the pad plays from: start to end."));
    row1.push_back (make ("drift",   "Drift",   "Slow random wandering around the Scan position."));
    row1.push_back (make ("spread",  "Spread",  "Random frame jitter for a grainy, shimmering texture."));
    row1.push_back (make ("diffuse", "Diffuse", "Tonal and pitched at 0, an airy wash at 100%."));
    row1.push_back (make ("mix",     "Mix",     "How much the pad replaces the dry signal while frozen."));
    row1.push_back (make ("level",   "Level",   "Output level of the pad."));

    row2.push_back (make ("blur",    "Blur",    "Smear neighbouring frequencies together for a softer pad."));
    row2.push_back (make ("shift",   "Shift",   "Pitch the pad up or down, in semitones, without changing its length.", true));
    row2.push_back (make ("shimmer", "Shimmer", "Blend in a copy of the pad one octave up."));
    row2.push_back (make ("tilt",    "Tilt",    "Darken (left) or brighten (right) the pad, pivoting at 1 kHz.", true));
    row2.push_back (make ("width",   "Width",   "Stereo spread: mono at 0, decorrelated left/right at 100%."));
    row2.push_back (make ("attack",  "Attack",  "Fade-in time when you freeze."));
    row2.push_back (make ("release", "Release", "Fade-out time when you release or re-freeze."));

    for (auto& k : row1) addAndMakeVisible (*k);
    for (auto& k : row2) addAndMakeVisible (*k);

    sections = { { "CAPTURE", 0, 0, 2 }, { "TEXTURE", 0, 2, 4 }, { "OUTPUT", 0, 6, 2 },
                 { "TONE",    1, 0, 5 }, { "ENVELOPE", 1, 5, 2 } };

    addAndMakeVisible (display);
    addAndMakeVisible (orb);
    freezeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (s, "freeze", orb);

    for (int i = 0; i < spectraProcessor.getNumPrograms(); ++i)
        presetBox.addItem (spectraProcessor.getProgramName (i), i + 1);
    presetBox.setSelectedId (spectraProcessor.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.setTooltip ("Factory presets. Loading one never starts or stops a freeze.");
    presetBox.onChange = [this]
    {
        const int idx = presetBox.getSelectedId() - 1;
        if (idx >= 0) spectraProcessor.setCurrentProgram (idx);
    };
    addAndMakeVisible (presetBox);

    randomizeButton.setTooltip ("Roll new values for the texture controls (Scan, Drift, Spread, Diffuse, Blur, Tilt, Width, Shimmer).");
    randomizeButton.onClick = [this] { spectraProcessor.randomizeTexture(); };
    addAndMakeVisible (randomizeButton);

    setSize (baseWidth, baseHeight);
}

juce::Rectangle<int> Content::cellBounds (int row, int col) const
{
    const int cols = row == 0 ? 8 : 7;
    const float w = (float) controlsArea.getWidth() / (float) cols;
    const int top = controlsArea.getY() + row * 116 + 20;
    return juce::Rectangle<int> (controlsArea.getX() + juce::roundToInt (w * (float) col), top,
                                 juce::roundToInt (w), 94);
}

void Content::resized()
{
    presetBox.setBounds (baseWidth - 20 - 110 - 10 - 190, 16, 190, 28);
    randomizeButton.setBounds (baseWidth - 20 - 110, 16, 110, 28);
    display.setBounds (20, 60, baseWidth - 40, 236);

    orbArea = juce::Rectangle<int> (20, 308, 190, 236);
    controlsArea = juce::Rectangle<int> (222, 308, baseWidth - 222 - 20, 236);
    orb.setBounds (orbArea.reduced (4));

    for (size_t i = 0; i < row1.size(); ++i) row1[i]->setBounds (cellBounds (0, (int) i).reduced (2, 0));
    for (size_t i = 0; i < row2.size(); ++i) row2[i]->setBounds (cellBounds (1, (int) i).reduced (2, 0));
}

void Content::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    // Wordmark
    g.setFont (font (24.0f, true, 0.42f));
    g.setColour (colours::text);
    g.drawText ("SPECTRA", 24, 14, 200, 32, juce::Justification::centredLeft, false);
    g.setFont (font (11.0f, false, 0.1f));
    g.setColour (colours::dim);
    g.drawText ("spectral freeze  /  granular texture", 204, 14, 260, 32, juce::Justification::centredLeft, false);

    // Panels
    g.setColour (colours::panel);
    g.fillRoundedRectangle (orbArea.toFloat(), 10.0f);
    g.fillRoundedRectangle (controlsArea.toFloat().expanded (0.0f, 0.0f), 10.0f);
    g.setColour (colours::hairline);
    g.drawRoundedRectangle (orbArea.toFloat().reduced (0.5f), 10.0f, 1.0f);
    g.drawRoundedRectangle (controlsArea.toFloat().reduced (0.5f), 10.0f, 1.0f);

    // Section headers
    g.setFont (font (9.5f, true, 0.22f));
    for (const auto& s : sections)
    {
        const auto first = cellBounds (s.row, s.firstCol);
        const auto last  = cellBounds (s.row, s.firstCol + s.numCols - 1);
        const int y = first.getY() - 14;
        const int x0 = first.getX() + 10, x1 = last.getRight() - 10;
        const int textW = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), s.name) + 8;
        g.setColour (colours::cyan.withAlpha (0.55f));
        g.drawText (s.name, x0, y - 6, textW, 12, juce::Justification::centredLeft, false);
        g.setColour (colours::hairline);
        g.drawHorizontalLine (y, (float) (x0 + textW), (float) x1);
    }
}

void Content::tick()
{
    time += 1.0f / 30.0f;
    display.tick();

    float level = 0.0f;
    for (auto& l : spectraProcessor.getVisualState().voiceLevel) level = std::max (level, l.load());
    orb.setLevel (juce::jmax (level * 0.6f, display.getEnergy()), time);

    const int sel = spectraProcessor.getCurrentProgram() + 1;
    if (presetBox.getSelectedId() != sel)
        presetBox.setSelectedId (sel, juce::dontSendNotification);
}

} // namespace spectra::ui

//==============================================================================
SpectraEditor::SpectraEditor (SpectraProcessor& p)
    : AudioProcessorEditor (&p), spectraProcessor (p), content (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);

    using C = spectra::ui::Content;
    setResizable (true, true);
    setResizeLimits (juce::roundToInt (C::baseWidth * 0.75f), juce::roundToInt (C::baseHeight * 0.75f),
                     juce::roundToInt (C::baseWidth * 1.5f),  juce::roundToInt (C::baseHeight * 1.5f));
    if (auto* constrainer = getConstrainer())
        constrainer->setFixedAspectRatio ((double) C::baseWidth / (double) C::baseHeight);

    const float scale = juce::jlimit (0.75f, 1.5f, spectraProcessor.getUiScale());
    setSize (juce::roundToInt (C::baseWidth * scale), juce::roundToInt (C::baseHeight * scale));

    startTimerHz (30);
}

SpectraEditor::~SpectraEditor()
{
    setLookAndFeel (nullptr);
}

void SpectraEditor::paint (juce::Graphics& g)
{
    g.fillAll (spectra::ui::colours::background);
}

void SpectraEditor::resized()
{
    using C = spectra::ui::Content;
    const float scale = (float) getWidth() / (float) C::baseWidth;
    content.setBounds (0, 0, C::baseWidth, C::baseHeight);
    content.setTransform (juce::AffineTransform::scale (scale));
    spectraProcessor.setUiScale (scale);
}

void SpectraEditor::timerCallback()
{
    content.tick();
}
