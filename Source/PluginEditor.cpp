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

    juce::Font sans (float size, bool bold = false, float tracking = 0.0f)
    {
        auto opts = juce::FontOptions (size).withKerningFactor (tracking);
        return juce::Font (bold ? opts.withStyle ("Bold") : opts);
    }

    juce::Font mono (float size, bool bold = false, float tracking = 0.0f)
    {
        auto opts = juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size, bold ? juce::Font::bold : juce::Font::plain)
                        .withKerningFactor (tracking);
        return juce::Font (opts);
    }

    juce::Font light (float size, float tracking = 0.0f)
    {
        return juce::Font (juce::FontOptions ("Helvetica Neue", size, juce::Font::plain).withStyle ("Light").withKerningFactor (tracking));
    }

    juce::Colour accentAt (float t)   // 0 = cyan, 1 = lavender
    {
        return colours::cyan.interpolatedWith (colours::lavender, juce::jlimit (0.0f, 1.0f, t));
    }

    float freqToX (float hz) { return std::log (hz / 20.0f) / std::log (1000.0f); }

    /** Cheap deterministic hash -> 0..1, for fixed star / scatter layouts. */
    float hash01 (int i, int salt)
    {
        uint32_t h = (uint32_t) i * 374761393u + (uint32_t) salt * 668265263u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return (float) ((h ^ (h >> 16)) & 0xffffu) / 65535.0f;
    }

    void dropShadow (juce::Graphics& g, juce::Rectangle<float> r, float cornerRadius,
                     float alpha, int blur, juce::Point<int> offset)
    {
        juce::Path p;
        p.addRoundedRectangle (r, cornerRadius);
        juce::DropShadow (juce::Colours::black.withAlpha (alpha), blur, offset).drawForPath (g, p);
    }

    /** Recessed surface: darker fill + shadow line along the top inner edge. */
    void recess (juce::Graphics& g, juce::Rectangle<float> r, float cornerRadius, juce::Colour fill)
    {
        g.setColour (fill);
        g.fillRoundedRectangle (r, cornerRadius);
        juce::Path p;
        p.addRoundedRectangle (r.reduced (0.5f), cornerRadius);
        juce::ColourGradient inner (juce::Colours::black.withAlpha (0.55f), 0.0f, r.getY(),
                                    juce::Colours::black.withAlpha (0.0f), 0.0f, r.getY() + std::min (14.0f, r.getHeight()), false);
        g.setGradientFill (inner);
        g.strokePath (p, juce::PathStrokeType (1.5f));
        juce::ColourGradient lip (juce::Colours::white.withAlpha (0.0f), 0.0f, r.getBottom() - 10.0f,
                                  juce::Colours::white.withAlpha (0.06f), 0.0f, r.getBottom(), false);
        g.setGradientFill (lip);
        g.strokePath (p, juce::PathStrokeType (1.0f));
    }

    void drawCrescent (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour col, juce::Colour behind)
    {
        g.setColour (col);
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
        g.setColour (behind);
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c.translated (r * 0.55f, -r * 0.25f)));
    }
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
    setColour (juce::PopupMenu::backgroundColourId, colours::tileHi);
    setColour (juce::PopupMenu::textColourId, colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::lavender.withAlpha (0.16f));
    setColour (juce::PopupMenu::highlightedTextColourId, colours::gold);
    setColour (juce::TooltipWindow::backgroundColourId, colours::tileHi);
    setColour (juce::TooltipWindow::textColourId, colours::text);
    setColour (juce::TooltipWindow::outlineColourId, colours::line);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                    float startAngle, float endAngle, juce::Slider& slider)
{
    if (slider.getProperties()["invisible"]) return;   // tiles and tick bars draw themselves

    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (2.0f);
    const float r = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const bool large = r > 24.0f;
    const float arcStroke = large ? 2.0f : 1.6f;
    const float angle = startAngle + pos * (endAngle - startAngle);
    const bool bipolar = slider.getProperties()["bipolar"];
    const bool active = slider.isMouseOverOrDragging();
    const auto pst = juce::PathStrokeType (arcStroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    // Value ring
    const float arcR = r - arcStroke * 0.5f;
    juce::Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.strokePath (track, pst);

    const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
    if (std::abs (angle - from) > 0.001f)
    {
        juce::Path value;
        value.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, std::min (from, angle), std::max (from, angle), true);
        g.setColour (colours::lavender.withAlpha (active ? 1.0f : 0.8f));
        g.strokePath (value, pst);
    }

    // Pale knob body, lit from the top-left
    const float bodyR = r - (large ? 7.0f : 5.0f);
    const auto body = juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (c);
    juce::Path bodyPath;
    bodyPath.addEllipse (body);
    juce::DropShadow (juce::Colours::black.withAlpha (0.6f), large ? 14 : 8, { 0, large ? 6 : 3 }).drawForPath (g, bodyPath);

    juce::ColourGradient fill (colours::knob.brighter (active ? 0.05f : 0.0f), c.x - bodyR * 0.5f, c.y - bodyR * 0.7f,
                               colours::knobShade, c.x + bodyR * 0.6f, c.y + bodyR, true);
    g.setGradientFill (fill);
    g.fillPath (bodyPath);

    // Soft concave face
    const float faceR = bodyR * 0.78f;
    juce::ColourGradient face (colours::knobShade.withAlpha (0.35f), c.x, c.y - faceR,
                               colours::knob.withAlpha (0.0f), c.x, c.y + faceR, false);
    g.setGradientFill (face);
    g.fillEllipse (juce::Rectangle<float> (faceR * 2.0f, faceR * 2.0f).withCentre (c));

    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.drawEllipse (body.reduced (0.5f), 0.8f);

    const auto p0 = c.getPointOnCircumference (bodyR * 0.5f, angle);
    const auto p1 = c.getPointOnCircumference (bodyR * 0.86f, angle);
    g.setColour (juce::Colour (0xff3a3358));
    g.drawLine ({ p0, p1 }, large ? 2.6f : 2.0f);
}

juce::Font LookAndFeel::getPopupMenuFont() { return mono (13.0f); }

void LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (colours::tileHi);
    g.setColour (colours::line);
    g.drawRect (0, 0, w, h);
}

juce::Rectangle<int> LookAndFeel::getTooltipBounds (const juce::String& text, juce::Point<int> pos,
                                                    juce::Rectangle<int> parentArea)
{
    const int w = std::min (340, juce::GlyphArrangement::getStringWidthInt (sans (12.5f), text) + 24);
    const int h = 28;
    return juce::Rectangle<int> (pos.x > parentArea.getCentreX() ? pos.x - (w + 12) : pos.x + 14,
                                 pos.y > parentArea.getCentreY() ? pos.y - (h + 8)  : pos.y + 18,
                                 w, h).constrainedWithin (parentArea);
}

void LookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    const auto r = juce::Rectangle<int> (0, 0, w, h).toFloat();
    g.setColour (colours::tileHi);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (colours::line.brighter (0.3f));
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    g.setColour (colours::text);
    g.setFont (sans (12.5f));
    g.drawFittedText (text, juce::Rectangle<int> (0, 0, w, h).reduced (12, 2), juce::Justification::centredLeft, 1);
}

//==============================================================================
BoundSlider::BoundSlider (juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
                          juce::Slider::SliderStyle style, const juce::String& tooltip)
{
    slider.setSliderStyle (style);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (pi * 1.25f, pi * 2.75f, true);
    slider.setMouseDragSensitivity (240);
    slider.setVelocityModeParameters (0.5, 1, 0.05, true, juce::ModifierKeys::shiftModifier);   // Shift = fine
    slider.setTooltip (tooltip);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);
    if (auto* param = state.getParameter (paramId))
        slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
}

//==============================================================================
SmallKnob::SmallKnob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
                      const juce::String& labelText, const juce::String& tooltip, bool bipolar)
    : bound (state, paramId, juce::Slider::RotaryHorizontalVerticalDrag, tooltip), label (labelText.toUpperCase())
{
    bound.slider.getProperties().set ("bipolar", bipolar);
    bound.slider.onValueChange = [this] { repaint(); };
    bound.slider.onDragStart = bound.slider.onDragEnd = [this] { repaint(); };
    addAndMakeVisible (bound.slider);
}

void SmallKnob::resized()
{
    auto r = getLocalBounds();
    r.removeFromBottom (16);
    const int side = std::min (r.getWidth(), r.getHeight());
    bound.slider.setBounds (r.withSizeKeepingCentre (side, side));
}

void SmallKnob::paint (juce::Graphics& g)
{
    const bool active = bound.slider.isMouseOverOrDragging();
    g.setFont (mono (9.5f, false, 0.08f));
    g.setColour (active ? colours::gold : colours::dim);
    g.drawText (active ? bound.valueText() : label, getLocalBounds().removeFromBottom (14), juce::Justification::centred, false);
}

//==============================================================================
MoonButton::MoonButton() : juce::Button ("Freeze")
{
    setClickingTogglesState (true);
    setTooltip ("Freeze: capture the last moment of sound and hold it as a pad. Click again to release.");
}

void MoonButton::tick (float dt, float energy, float t, bool reducedMotion)
{
    time = t;
    still = reducedMotion;
    on.target    = getToggleState() ? 1.0f : 0.0f;
    press.target = isDown() ? 1.0f : 0.0f;
    glow.target  = getToggleState() ? 0.5f + 0.5f * energy : (isOver() ? 0.25f : 0.0f);

    bool changed = on.update (dt, 0.12f, 0.12f);                              // waxing / waning cross-fade
    changed |= press.update (dt, reducedMotion ? 1.0e-4f : 0.05f, 0.05f);    // 150 ms ease-out press to 0.96
    changed |= glow.update (dt, 0.12f, 0.25f);
    if (changed || (on.value > 0.0f && ! still)) repaint();
}

void MoonButton::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto bounds = getLocalBounds().toFloat();
    const float a = on.value;

    // Stars: always present, twinkle while frozen
    for (int i = 0; i < 16; ++i)
    {
        const float sx = bounds.getX() + 6.0f + hash01 (i, 1) * (bounds.getWidth() - 12.0f);
        const float sy = bounds.getY() + 22.0f + hash01 (i, 2) * (bounds.getHeight() - 52.0f);
        const float tw = still ? 1.0f : 0.6f + 0.4f * std::sin (time * (0.8f + hash01 (i, 3) * 2.0f) + (float) i);
        const float s = 0.8f + hash01 (i, 4) * 1.2f;
        g.setColour (colours::knob.withAlpha ((0.18f + 0.5f * a) * tw));
        g.fillEllipse (juce::Rectangle<float> (s, s).withCentre ({ sx, sy }));
    }

    const auto centre = bounds.getCentre().translated (0.0f, -2.0f);
    const float r = 26.0f * (1.0f - 0.04f * press.value);   // press -> 0.96
    const auto disc = juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (centre);

    // Halo
    if (glow.value > 0.0f)
    {
        const float rr = r * 2.1f;
        juce::ColourGradient halo (colours::gold.withAlpha (0.35f * glow.value), centre.x, centre.y,
                                   colours::lavender.withAlpha (0.0f), centre.x + rr, centre.y, true);
        halo.addColour (0.5, colours::lavender.withAlpha (0.18f * glow.value));
        g.setGradientFill (halo);
        g.fillEllipse (juce::Rectangle<float> (rr * 2.0f, rr * 2.0f).withCentre (centre));
    }

    // Moon disc
    juce::Path moonPath;
    moonPath.addEllipse (disc);
    juce::ColourGradient moonFill (colours::knob.interpolatedWith (colours::gold, 0.25f * a), centre.x - r * 0.4f, centre.y - r * 0.5f,
                                   colours::knobShade.interpolatedWith (colours::lavender, 0.3f), centre.x + r, centre.y + r, true);
    g.setGradientFill (moonFill);
    g.setOpacity (0.55f + 0.45f * a + (highlighted ? 0.1f : 0.0f));
    g.fillPath (moonPath);
    g.setOpacity (1.0f);

    // Craters
    for (int i = 0; i < 4; ++i)
    {
        const float cr = r * (0.12f + 0.1f * hash01 (i, 7));
        const auto cc = centre.translated ((hash01 (i, 8) - 0.5f) * r * 1.1f, (hash01 (i, 9) - 0.5f) * r * 1.1f);
        g.setColour (colours::knobShade.withAlpha (0.35f));
        g.fillEllipse (juce::Rectangle<float> (cr * 2.0f, cr * 2.0f).withCentre (cc));
    }

    // Earth-shadow: a crescent when idle that slides away as the moon waxes full
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (moonPath);
        const float offset = r * (0.6f + 1.8f * a);
        g.setColour (colours::emblem.withAlpha (0.92f));
        g.fillEllipse (juce::Rectangle<float> (r * 2.1f, r * 2.1f).withCentre (centre.translated (offset, -r * 0.2f)));
    }

    // Static cue for the state
    const bool frozen = getToggleState();
    g.setFont (mono (9.5f, true, 0.25f));
    g.setColour (frozen ? colours::gold : colours::text.withAlpha (highlighted ? 0.9f : 0.6f));
    g.drawText (frozen ? "FROZEN" : "FREEZE", bounds.withHeight (20.0f).translated (0.0f, 4.0f), juce::Justification::centred, false);
}

//==============================================================================
ModuleCard::ModuleCard (juce::AudioProcessorValueTreeState& state, Emblem e, const juce::String& titleText,
                        const juce::String& bigParam, const juce::String& bigLabelText, const juce::String& bigTip,
                        bool bigBipolar)
    : emblem (e), title (titleText), bigLabel (bigLabelText.toUpperCase()),
      big (state, bigParam, juce::Slider::RotaryHorizontalVerticalDrag, bigTip)
{
    big.slider.getProperties().set ("bipolar", bigBipolar);
    big.slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (big.slider);

    if (emblem == Emblem::moon)
    {
        moon = std::make_unique<MoonButton>();
        addAndMakeVisible (*moon);
    }
}

void ModuleCard::addSmallKnob (std::unique_ptr<SmallKnob> k)
{
    addAndMakeVisible (*k);
    smallKnobs.push_back (std::move (k));
    resized();
}

juce::Rectangle<float> ModuleCard::emblemArea() const
{
    return getLocalBounds().toFloat().reduced (radius::cardPad).withHeight (126.0f);
}

void ModuleCard::tick (float dt, float e, float t, bool reducedMotion, float param)
{
    energy = e;
    time = t;
    still = reducedMotion;
    const bool paramChanged = std::abs (param - emblemParam) > 0.001f;
    emblemParam = param;

    if (moon != nullptr)
        moon->tick (dt, e, t, reducedMotion);
    else if (paramChanged || ! still)
        repaint (emblemArea().toNearestInt());
}

void ModuleCard::resized()
{
    const auto w = getWidth();
    if (moon != nullptr) moon->setBounds (emblemArea().toNearestInt());
    big.slider.setBounds (juce::Rectangle<int> (66, 66).withCentre ({ w / 2, 158 }));

    const int knobW = 52, knobH = 58, top = 216;
    const int n = (int) smallKnobs.size();
    for (int i = 0; i < n; ++i)
    {
        const int x = n == 1 ? (w - knobW) / 2 : (w / 2) - knobW + i * knobW + (i == 0 ? -2 : 2);
        smallKnobs[(size_t) i]->setBounds (x, top, knobW, knobH);
    }
}

void ModuleCard::paintConstellation (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto c = area.getCentre().translated (0.0f, -4.0f);
    const float spread = emblemParam;   // Diffuse 0..1

    // Nebula grows with Diffuse
    const float nr = 16.0f + 34.0f * spread;
    juce::ColourGradient neb (colours::lavender.withAlpha (0.10f + 0.25f * spread + 0.15f * energy), c.x, c.y,
                              colours::lavender.withAlpha (0.0f), c.x + nr, c.y, true);
    g.setGradientFill (neb);
    g.fillEllipse (juce::Rectangle<float> (nr * 2.0f, nr * 2.0f).withCentre (c));

    constexpr int n = 11;
    std::array<juce::Point<float>, n> pts;
    for (int i = 0; i < n; ++i)
    {
        const float bx = (hash01 (i, 11) - 0.5f) * 56.0f;
        const float by = (hash01 (i, 12) - 0.5f) * 56.0f;
        const float wob = still ? 0.0f : std::sin (time * (0.4f + energy) + (float) i * 1.7f);
        const float jx = (hash01 (i, 13) - 0.5f) * 40.0f * spread + wob * 2.0f * spread;
        const float jy = (hash01 (i, 14) - 0.5f) * 40.0f * spread + wob * 1.5f * spread;
        pts[(size_t) i] = { c.x + bx * (1.0f - 0.25f * spread) + jx, c.y + by * (1.0f - 0.25f * spread) + jy };
    }

    // Constellation lines fade out as the sound diffuses
    g.setColour (colours::knob.withAlpha (0.45f * (1.0f - spread)));
    for (int i = 0; i < n - 1; ++i)
        if (hash01 (i, 15) > 0.25f)
            g.drawLine ({ pts[(size_t) i], pts[(size_t) i + 1] }, 0.8f);

    for (int i = 0; i < n; ++i)
    {
        const float s = 2.0f + hash01 (i, 16) * 2.5f;
        const float tw = still ? 1.0f : 0.7f + 0.3f * std::sin (time * 1.3f + (float) i);
        g.setColour (colours::knob.withAlpha ((0.5f + 0.4f * energy) * tw));
        g.fillEllipse (juce::Rectangle<float> (s, s).withCentre (pts[(size_t) i]));
    }
}

void ModuleCard::paintPlanet (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto c = area.getCentre().translated (0.0f, -2.0f);
    const float pr = 20.0f;
    const float ringA = 44.0f, ringB = 12.0f;
    const auto rot = juce::AffineTransform::rotation (-0.35f, c.x, c.y);
    const float ringAlpha = 0.3f + 0.6f * emblemParam;   // Mix brightens the ring

    juce::Path ring;
    ring.addEllipse (c.x - ringA, c.y - ringB, ringA * 2.0f, ringB * 2.0f);
    ring.applyTransform (rot);

    // back half of the ring
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (juce::Rectangle<float> (area.getX(), area.getY(), area.getWidth(), c.y - area.getY()).toNearestInt()
                                .transformedBy (juce::AffineTransform()));
        g.setColour (colours::knob.withAlpha (ringAlpha * 0.6f));
        g.strokePath (ring, juce::PathStrokeType (1.6f));
    }

    juce::ColourGradient planet (colours::lavender.brighter (0.2f), c.x - pr * 0.6f, c.y - pr * 0.7f,
                                 juce::Colour (0xff2a2350), c.x + pr, c.y + pr, true);
    g.setGradientFill (planet);
    g.fillEllipse (juce::Rectangle<float> (pr * 2.0f, pr * 2.0f).withCentre (c));
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawLine (c.x - pr * 0.8f, c.y - 4.0f, c.x + pr * 0.8f, c.y - 7.0f, 1.0f);
    g.drawLine (c.x - pr * 0.7f, c.y + 5.0f, c.x + pr * 0.85f, c.y + 2.0f, 1.0f);

    // front half of the ring
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (juce::Rectangle<float> (area.getX(), c.y, area.getWidth(), area.getBottom() - c.y).toNearestInt());
        g.setColour (colours::knob.withAlpha (ringAlpha));
        g.strokePath (ring, juce::PathStrokeType (1.6f));
    }

    // Satellite riding the ring, speed follows the pad
    const float t = still ? 0.6f : time * (0.25f + 0.9f * energy);
    const auto sat = juce::Point<float> (c.x + ringA * std::cos (t), c.y + ringB * std::sin (t)).transformedBy (rot);
    g.setColour (colours::gold.withAlpha (0.6f + 0.4f * energy));
    g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (sat));
}

void ModuleCard::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();

    // Card body
    juce::ColourGradient body (colours::cardTop, 0.0f, 0.0f, colours::cardBot, 0.0f, r.getBottom(), false);
    g.setGradientFill (body);
    g.fillRoundedRectangle (r, radius::card);
    juce::Path outline;
    outline.addRoundedRectangle (r.reduced (0.5f), radius::card);
    juce::ColourGradient rim (juce::Colours::white.withAlpha (0.12f), 0.0f, 0.0f,
                              juce::Colours::white.withAlpha (0.0f), 0.0f, 40.0f, false);
    g.setGradientFill (rim);
    g.strokePath (outline, juce::PathStrokeType (1.0f));

    // Emblem panel
    const auto em = emblemArea();
    juce::ColourGradient emFill (colours::emblem.brighter (0.06f), em.getX(), em.getY(),
                                 colours::emblem.darker (0.15f), em.getX(), em.getBottom(), false);
    g.setGradientFill (emFill);
    g.fillRoundedRectangle (em, radius::emblem);
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (em.toNearestInt());
        if (emblem == Emblem::constellation) paintConstellation (g, em);
        if (emblem == Emblem::planet)        paintPlanet (g, em);
    }

    // Gold pill under the big knob
    g.setFont (mono (9.5f, true, 0.12f));
    const float pillW = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), bigLabel) + 16.0f;
    const auto pill = juce::Rectangle<float> (pillW, 15.0f).withCentre ({ r.getCentreX(), 199.0f });
    g.setColour (colours::gold);
    g.fillRoundedRectangle (pill, 3.0f);
    g.setColour (juce::Colour (0xff2a2140));
    g.drawText (bigLabel, pill, juce::Justification::centred, false);

    // Title lettering
    g.setFont (sans (15.0f, true, 0.32f));
    g.setColour (colours::text.withAlpha (0.88f));
    g.drawText (title, juce::Rectangle<float> (0.0f, 290.0f, r.getWidth(), 24.0f), juce::Justification::centred, false);
    g.setColour (colours::lavender.withAlpha (0.5f));
    g.fillRect (juce::Rectangle<float> (14.0f, 1.0f).withCentre ({ r.getCentreX(), 320.0f }));

    // Value slot for the big knob
    const auto slot = juce::Rectangle<float> (12.0f, r.getBottom() - 38.0f, r.getWidth() - 24.0f, 22.0f);
    recess (g, slot, 11.0f, colours::tray);
    g.setFont (mono (11.5f));
    g.setColour (big.slider.isMouseOverOrDragging() ? colours::gold : colours::text.withAlpha (0.9f));
    g.drawText (big.valueText(), slot, juce::Justification::centred, false);
}

//==============================================================================
Tile::Tile (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& labelText,
            const juce::String& tooltip, Glyph gl)
    : bound (state, paramId, juce::Slider::RotaryVerticalDrag, tooltip + "  (drag up / down)"),
      label (labelText.toUpperCase()), glyph (gl)
{
    bound.slider.getProperties().set ("invisible", true);
    bound.slider.setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    bound.slider.onValueChange = [this] { repaint(); };
    bound.slider.onDragStart = bound.slider.onDragEnd = [this] { repaint(); };
    addAndMakeVisible (bound.slider);
}

void Tile::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const bool active = bound.slider.isMouseOverOrDragging();
    const float v = bound.normalised();

    g.setColour (active ? colours::tileHi : colours::tile);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (colours::line.withAlpha (active ? 1.0f : 0.6f));
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

    auto glyphArea = r.withTrimmedBottom (18.0f).reduced (12.0f, 8.0f);
    const auto ink = colours::text.withAlpha (0.85f);

    switch (glyph)
    {
        case Glyph::position:
        {
            const float y = glyphArea.getCentreY();
            g.setColour (colours::faint);
            g.drawLine (glyphArea.getX(), y, glyphArea.getRight(), y, 1.5f);
            const float x = glyphArea.getX() + v * glyphArea.getWidth();
            g.setColour (colours::gold);
            g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ x, y }));
            break;
        }
        case Glyph::scatter:
        {
            constexpr int n = 9;
            for (int i = 0; i < n; ++i)
            {
                const float x = glyphArea.getX() + glyphArea.getWidth() * ((float) i + 0.5f) / (float) n;
                const float y = glyphArea.getCentreY() + (hash01 (i, 21) - 0.5f) * glyphArea.getHeight() * v;
                g.setColour (ink);
                g.fillEllipse (juce::Rectangle<float> (3.4f, 3.4f).withCentre ({ x, y }));
            }
            break;
        }
        case Glyph::number:
        {
            g.setFont (light (25.0f));
            g.setColour (ink);
            g.drawText (juce::String (juce::roundToInt (v * 100.0f)), glyphArea.expanded (0.0f, 4.0f), juce::Justification::centred, false);
            break;
        }
        case Glyph::blur:
        {
            const int copies = 1 + juce::roundToInt (v * 6.0f);
            for (int k = 0; k < copies; ++k)
            {
                juce::Path wave;
                const float off = ((float) k - (float) (copies - 1) * 0.5f) * 1.6f;
                for (int i = 0; i <= 32; ++i)
                {
                    const float t = (float) i / 32.0f;
                    const auto p = juce::Point<float> (glyphArea.getX() + t * glyphArea.getWidth(),
                                                       glyphArea.getCentreY() + off - std::sin (t * 2.0f * pi) * glyphArea.getHeight() * 0.32f);
                    if (i == 0) wave.startNewSubPath (p); else wave.lineTo (p);
                }
                g.setColour (ink.withAlpha (copies == 1 ? 0.85f : 0.85f / (float) copies * 1.8f));
                g.strokePath (wave, juce::PathStrokeType (1.5f));
            }
            break;
        }
    }

    g.setFont (mono (9.5f, false, 0.06f));
    g.setColour (active ? colours::gold : colours::dim);
    g.drawText (active ? bound.valueText() : label, r.withTop (r.getBottom() - 18.0f), juce::Justification::centred, false);
}

//==============================================================================
TickBar::TickBar (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& labelText,
                  const juce::String& tooltip)
    : bound (state, paramId, juce::Slider::RotaryHorizontalDrag, tooltip + "  (drag left / right)"),
      label (labelText.toUpperCase())
{
    bound.slider.getProperties().set ("invisible", true);
    bound.slider.setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    bound.slider.onValueChange = [this] { repaint(); };
    bound.slider.onDragStart = bound.slider.onDragEnd = [this] { repaint(); };
    addAndMakeVisible (bound.slider);
}

void TickBar::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const bool active = bound.slider.isMouseOverOrDragging();
    const float v = bound.normalised();

    constexpr int n = 30;
    const auto ticks = r.withHeight (16.0f).withTrimmedRight (2.0f);
    const float step = ticks.getWidth() / (float) n;
    for (int i = 0; i < n; ++i)
    {
        const bool lit = ((float) i + 0.5f) / (float) n <= v;
        const float x = ticks.getX() + step * (float) i + step * 0.5f;
        g.setColour (lit ? (active ? colours::gold : colours::text.withAlpha (0.85f)) : colours::faint.withAlpha (0.8f));
        g.fillRect (juce::Rectangle<float> (x - 1.0f, ticks.getY() + (lit ? 0.0f : 3.0f), 2.0f, lit ? 16.0f : 13.0f));
    }

    g.setFont (mono (10.5f, false, 0.08f));
    g.setColour (active ? colours::gold : colours::dim);
    const auto textRow = r.withTrimmedTop (22.0f);
    g.drawText (label, textRow, juce::Justification::centredLeft, false);
    g.setColour (active ? colours::gold : colours::text.withAlpha (0.6f));
    g.drawText (bound.valueText(), textRow, juce::Justification::centredRight, false);
}

//==============================================================================
Display::Display (VisualState& state) : vs (state)
{
    particles.reserve (160);
    thumbImage = juce::Image (juce::Image::ARGB, VisualState::thumbCols, VisualState::thumbRows, true);
    setInterceptsMouseClicks (false, false);
}

juce::Rectangle<float> Display::curvesArea() const { return getLocalBounds().toFloat().withTrimmedBottom (34.0f + 8.0f); }
juce::Rectangle<float> Display::stripArea() const  { auto r = getLocalBounds().toFloat(); return r.removeFromBottom (34.0f); }

void Display::rebuildThumbnail()
{
    thumbCols = vs.thumbNumCols.load();
    juce::Image::BitmapData bd (thumbImage, juce::Image::BitmapData::writeOnly);
    const juce::Colour mid (0xff4a3a8c);
    for (int c = 0; c < VisualState::thumbCols; ++c)
        for (int r = 0; r < VisualState::thumbRows; ++r)
        {
            const float v = c < thumbCols ? vs.thumb[(size_t) (r * VisualState::thumbCols + c)].load (std::memory_order_relaxed) : 0.0f;
            const float t = juce::jlimit (0.0f, 1.0f, (v - 0.3f) / 0.6f);
            juce::Colour col;
            if (t < 0.5f)       col = colours::glass.interpolatedWith (mid, t * 2.0f);
            else if (t < 0.85f) col = mid.interpolatedWith (colours::lavender, (t - 0.5f) / 0.35f);
            else                col = colours::lavender.interpolatedWith (colours::gold.brighter (0.4f), (t - 0.85f) / 0.15f);
            bd.setPixelColour (c, VisualState::thumbRows - 1 - r, col);
        }
}

void Display::spawnParticle (juce::Rectangle<float> area)
{
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
    pt.size = 1.2f + rng.nextFloat() * 2.4f;
    pt.hue = juce::jlimit (0.0f, 1.0f, centroid + (rng.nextFloat() - 0.5f) * 0.5f);
    particles.push_back (pt);
}

void Display::tick (bool reducedMotion)
{
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
    energy += (juce::jlimit (0.0f, 1.0f, (mean - 0.12f) / 0.4f) - energy) * 0.12f;
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

    showParticles = ! reducedMotion;   // ambient motion off under Reduce Motion
    if (showParticles)
    {
        const auto area = curvesArea();
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
    }
    else
    {
        particles.clear();
    }
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
    const auto curves = curvesArea();
    const auto strip = stripArea();

    // Grid
    g.setFont (mono (9.0f));
    for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = curves.getX() + curves.getWidth() * freqToX (hz);
        g.setColour (colours::line.withAlpha (0.7f));
        g.drawVerticalLine ((int) x, curves.getY(), curves.getBottom());
        g.setColour (colours::faint);
        const juce::String label = hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz);
        g.drawText (label, juce::Rectangle<float> (x + 3.0f, curves.getBottom() - 12.0f, 30.0f, 11.0f),
                    juce::Justification::left, false);
    }

    if (showParticles)
    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (curves.toNearestInt());
        for (const auto& pt : particles)
        {
            const float alpha = std::sin (pt.life / pt.maxLife * pi) * (0.18f + 0.6f * energy);
            const auto col = accentAt (0.4f + 0.6f * pt.hue);
            const float glowSize = pt.size * 3.5f;
            g.setColour (col.withAlpha (alpha * 0.18f));
            g.fillEllipse (pt.x - glowSize * 0.5f, pt.y - glowSize * 0.5f, glowSize, glowSize);
            g.setColour (col.withAlpha (alpha));
            g.fillEllipse (pt.x - pt.size * 0.5f, pt.y - pt.size * 0.5f, pt.size, pt.size);
        }
    }

    g.setColour (colours::text.withAlpha (0.05f));
    g.fillPath (makeCurve (input, curves, true));
    g.setColour (colours::text.withAlpha (0.25f));
    g.strokePath (makeCurve (input, curves, false), juce::PathStrokeType (1.0f));

    float padPeak = 0.0f;
    for (float v : pad) padPeak = std::max (padPeak, v);
    if (padPeak > 0.01f)
    {
        const float presence = juce::jlimit (0.0f, 1.0f, padPeak * 2.0f);
        juce::ColourGradient fill (colours::lavender.withAlpha (0.24f * presence), 0.0f, curves.getY(),
                                   colours::lavender.withAlpha (0.02f), 0.0f, curves.getBottom(), false);
        g.setGradientFill (fill);
        g.fillPath (makeCurve (pad, curves, true));
        juce::ColourGradient lineGrad (colours::cyan.withAlpha (presence), curves.getX(), 0.0f,
                                       colours::lavender.withAlpha (presence), curves.getRight(), 0.0f, false);
        lineGrad.addColour (0.85, colours::gold.withAlpha (presence * 0.9f));
        g.setGradientFill (lineGrad);
        g.strokePath (makeCurve (pad, curves, false), juce::PathStrokeType (1.8f));
    }

    // Captured-slice strip
    recess (g, strip, 4.0f, colours::glass.darker (0.3f));
    if (thumbCols > 0)
    {
        juce::Graphics::ScopedSaveState s (g);
        juce::Path clipPath;
        clipPath.addRoundedRectangle (strip.reduced (1.0f), 3.0f);
        g.reduceClipRegion (clipPath);
        g.setOpacity (frozen ? 1.0f : 0.35f);
        g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
        g.drawImage (thumbImage.getClippedImage ({ 0, 0, thumbCols, VisualState::thumbRows }), strip,
                     juce::RectanglePlacement::stretchToFit);
        g.setOpacity (1.0f);

        if (frozen)
        {
            const float rx = strip.getX() + strip.getWidth() * regionStart;
            g.setColour (colours::glass.withAlpha (0.6f));
            g.fillRect (strip.withRight (rx));
            g.setColour (colours::gold.withAlpha (0.7f));
            g.drawRoundedRectangle (juce::Rectangle<float> (rx, strip.getY(), strip.getRight() - rx, strip.getHeight()).reduced (0.5f), 3.0f, 1.0f);
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
        g.setColour (colours::faint);
        g.setFont (mono (9.5f, false, 0.12f));
        g.drawText ("NOTHING CAPTURED YET", strip, juce::Justification::centred, false);
    }
}

//==============================================================================
Screen::Screen (juce::AudioProcessorValueTreeState& s, VisualState& vs) : display (vs)
{
    using G = Tile::Glyph;
    auto add = [&] (const char* id, const char* name, const char* tip, G glyph)
    {
        tiles.push_back (std::make_unique<Tile> (s, id, name, tip, glyph));
        addAndMakeVisible (*tiles.back());
    };
    add ("scan",    "Scan",    "Scan: where in the captured slice the pad plays from.", G::position);
    add ("spread",  "Spread",  "Spread: random frame jitter for a grainy, shimmering texture.", G::scatter);
    add ("absorb",  "Absorb",  "Absorb: while frozen, slowly blend new input into the pad.", G::number);
    add ("blur",    "Blur",    "Blur: smear neighbouring frequencies together.", G::blur);
    add ("shimmer", "Shimmer", "Shimmer: blend in a copy of the pad one octave up.", G::number);

    attack  = std::make_unique<TickBar> (s, "attack",  "Attack",  "Attack: fade-in time when you freeze.");
    release = std::make_unique<TickBar> (s, "release", "Release", "Release: fade-out time when you release or re-freeze.");
    addAndMakeVisible (*attack);
    addAndMakeVisible (*release);
    addAndMakeVisible (display);
}

void Screen::resized()
{
    const auto inner = getLocalBounds().reduced ((int) radius::bezelPad + 8);
    const int gap = 6;
    const float tw = ((float) inner.getWidth() - (float) gap * 4.0f) / 5.0f;
    for (size_t i = 0; i < tiles.size(); ++i)
        tiles[i]->setBounds (inner.getX() + juce::roundToInt ((tw + (float) gap) * (float) i), inner.getY(),
                             juce::roundToInt (tw), 58);

    const int barsY = inner.getBottom() - 38;
    const int bw = (inner.getWidth() - 20) / 2;
    attack->setBounds (inner.getX(), barsY, bw, 38);
    release->setBounds (inner.getRight() - bw, barsY, bw, 38);

    display.setBounds (inner.getX(), inner.getY() + 58 + 10, inner.getWidth(), barsY - 10 - (inner.getY() + 58 + 10));
}

void Screen::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    // Bezel
    juce::ColourGradient bezel (juce::Colour (0xff141126), 0.0f, 0.0f, juce::Colour (0xff0c0a17), 0.0f, r.getBottom(), false);
    g.setGradientFill (bezel);
    g.fillRoundedRectangle (r, radius::screen);
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.drawRoundedRectangle (r.reduced (0.5f), radius::screen, 1.0f);

    // Glass
    const auto glassR = r.reduced (radius::bezelPad);
    recess (g, glassR, radius::glass, colours::glass);
    juce::ColourGradient glow (colours::lavender.withAlpha (0.05f), glassR.getCentreX(), glassR.getCentreY(),
                               colours::lavender.withAlpha (0.0f), glassR.getRight(), glassR.getBottom(), true);
    g.setGradientFill (glow);
    g.fillRoundedRectangle (glassR, radius::glass);
}

void Screen::paintOverChildren (juce::Graphics& g)
{
    // Diagonal glass sheen
    const auto glassR = getLocalBounds().toFloat().reduced (radius::bezelPad);
    juce::Path sheen;
    sheen.startNewSubPath (glassR.getX() + glassR.getWidth() * 0.18f, glassR.getY());
    sheen.lineTo (glassR.getX() + glassR.getWidth() * 0.46f, glassR.getY());
    sheen.lineTo (glassR.getX() + glassR.getWidth() * 0.12f, glassR.getBottom());
    sheen.lineTo (glassR.getX(), glassR.getBottom());
    sheen.lineTo (glassR.getX(), glassR.getY() + glassR.getHeight() * 0.55f);
    sheen.closeSubPath();

    juce::Graphics::ScopedSaveState s (g);
    juce::Path clip;
    clip.addRoundedRectangle (glassR, radius::glass);
    g.reduceClipRegion (clip);
    juce::ColourGradient grad (juce::Colours::white.withAlpha (0.045f), glassR.getX(), glassR.getY(),
                               juce::Colours::white.withAlpha (0.0f), glassR.getX() + glassR.getWidth() * 0.4f, glassR.getBottom(), false);
    g.setGradientFill (grad);
    g.fillPath (sheen);
}

//==============================================================================
void PresetDisplay::DotButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    const float rr = down ? 4.6f : 5.0f;
    g.setColour (highlighted ? colours::gold : colours::dim);
    g.drawEllipse (juce::Rectangle<float> (rr * 2.0f, rr * 2.0f).withCentre (c), 1.6f);
    if (highlighted) g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre (c));
}

PresetDisplay::PresetDisplay (SpectraProcessor& p) : proc (p)
{
    prev.onClick = [this] { step (-1); };
    next.onClick = [this] { step (1); };
    prev.setTooltip ("Previous preset");
    next.setTooltip ("Next preset");
    addAndMakeVisible (prev);
    addAndMakeVisible (next);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

juce::Rectangle<int> PresetDisplay::nameArea() const { return getLocalBounds().reduced (30, 0); }

void PresetDisplay::resized()
{
    prev.setBounds (getLocalBounds().removeFromLeft (22).withSizeKeepingCentre (22, 22));
    next.setBounds (getLocalBounds().removeFromRight (22).withSizeKeepingCentre (22, 22));
}

void PresetDisplay::step (int delta)
{
    const int n = proc.getNumPrograms();
    proc.setCurrentProgram ((proc.getCurrentProgram() + delta + n) % n);
    refresh();
}

void PresetDisplay::refresh()
{
    if (shownProgram != proc.getCurrentProgram())
    {
        shownProgram = proc.getCurrentProgram();
        repaint();
    }
}

void PresetDisplay::mouseUp (const juce::MouseEvent& e)
{
    if (! nameArea().contains (e.getPosition())) return;

    juce::PopupMenu menu;
    for (int i = 0; i < proc.getNumPrograms(); ++i)
        menu.addItem (i + 1, proc.getProgramName (i), true, i == proc.getCurrentProgram());
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (nameArea().getWidth()),
                        [this] (int result)
                        {
                            if (result > 0) { proc.setCurrentProgram (result - 1); refresh(); }
                        });
}

void PresetDisplay::paint (juce::Graphics& g)
{
    const auto r = nameArea().toFloat();
    const bool hover = isMouseOver (false) && nameArea().contains (getMouseXYRelative());
    recess (g, r, r.getHeight() * 0.5f, hover ? juce::Colour (0xff17132a) : juce::Colour (0xff110e20));
    g.setFont (mono (14.0f, false, 0.04f));
    g.setColour (hover ? colours::text : colours::text.withAlpha (0.85f));
    g.drawText (proc.getProgramName (proc.getCurrentProgram()), r, juce::Justification::centred, false);
}

//==============================================================================
void PillButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (highlighted ? colours::text : colours::dim);
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.3f);
    g.setFont (mono (11.0f, false, 0.04f));
    g.drawText (getButtonText(), r.translated (0.0f, down ? 0.5f : 0.0f), juce::Justification::centred, false);
}

void DiceButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (highlighted ? colours::text : colours::dim);
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.3f);

    const auto c = r.getCentre();
    const float s = down ? 0.96f : 1.0f;
    const auto box = juce::Rectangle<float> (12.0f * s, 12.0f * s).withCentre (c);
    g.drawRoundedRectangle (box, 2.5f, 1.3f);
    const float d = 3.0f * s;
    for (auto p : { juce::Point<float> (-d, -d), juce::Point<float> (d, d), juce::Point<float> (0, 0),
                    juce::Point<float> (d, -d), juce::Point<float> (-d, d) })
        g.fillEllipse (juce::Rectangle<float> (1.9f, 1.9f).withCentre (c + p));
}

//==============================================================================
Content::Content (SpectraProcessor& proc, std::function<void (float)> scaleCallback)
    : spectraProcessor (proc), onScaleChosen (std::move (scaleCallback)),
      presetDisplay (proc), screen (proc.apvts, proc.getVisualState())
{
    auto& s = proc.apvts;
    using E = ModuleCard::Emblem;

    freezeCard = std::make_unique<ModuleCard> (s, E::moon, "FREEZE", "size", "Size",
                                               "Size: how much of the last moment is captured, one frame up to 2 s.");
    freezeCard->addSmallKnob (std::make_unique<SmallKnob> (s, "drift", "Drift", "Drift: slow random wandering through the captured slice."));

    hazeCard = std::make_unique<ModuleCard> (s, E::constellation, "HAZE", "diffuse", "Diffuse",
                                             "Diffuse: tonal and pitched at 0, an airy wash at 100%.");
    hazeCard->addSmallKnob (std::make_unique<SmallKnob> (s, "shift", "Shift", "Shift: pitch in semitones. Hold Shift to drag finely.", true));
    hazeCard->addSmallKnob (std::make_unique<SmallKnob> (s, "tilt", "Tone", "Tone: darker to the left, brighter to the right.", true));

    orbitCard = std::make_unique<ModuleCard> (s, E::planet, "ORBIT", "mix", "Mix",
                                              "Mix: how much the pad replaces the dry signal while frozen.");
    orbitCard->addSmallKnob (std::make_unique<SmallKnob> (s, "width", "Width", "Width: mono at 0, wide stereo at 100%."));
    orbitCard->addSmallKnob (std::make_unique<SmallKnob> (s, "level", "Level", "Level: output level of the pad."));

    addAndMakeVisible (presetDisplay);
    addAndMakeVisible (screen);
    addAndMakeVisible (*freezeCard);
    addAndMakeVisible (*hazeCard);
    addAndMakeVisible (*orbitCard);
    freezeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (s, "freeze", *freezeCard->getMoon());

    randomizeButton.setTooltip ("Randomize the texture: Scan, Drift, Spread, Diffuse, Blur, Tone, Width, Shimmer.");
    randomizeButton.onClick = [this] { spectraProcessor.randomizeTexture(); };
    addAndMakeVisible (randomizeButton);

    scaleButton.setTooltip ("Window size");
    scaleButton.onClick = [this] { showScaleMenu(); };
    addAndMakeVisible (scaleButton);

    // Static film grain for the chassis
    grain = juce::Image (juce::Image::ARGB, 128, 128, true);
    juce::Random r (42);
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x)
            grain.setPixelAt (x, y, juce::Colours::white.withAlpha (r.nextFloat() * 0.035f));

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
                        [this] (int result)
                        {
                            if (result > 0 && onScaleChosen)
                                onScaleChosen (scaleSteps[result - 1]);
                        });
}

void Content::resized()
{
    presetDisplay.setBounds ((baseWidth - 420) / 2, 18, 420, 30);
    trayArea = juce::Rectangle<float> (24.0f, 66.0f, 912.0f, 402.0f);
    screen.setBounds (40, 82, 484, 334);

    const int cardW = 116, gap = 17, cardY = 56, cardH = 398;
    freezeCard->setBounds (544, cardY, cardW, cardH);
    hazeCard->setBounds (544 + cardW + gap, cardY, cardW, cardH);
    orbitCard->setBounds (544 + 2 * (cardW + gap), cardY, cardW, cardH);

    randomizeButton.setBounds (baseWidth / 2 - 21, 494, 42, 24);
    scaleButton.setBounds (baseWidth - 30 - 34 - 10 - 64, 494, 64, 24);
}

void Content::paintGrille (juce::Graphics& g, juce::Rectangle<float> area, float glow) const
{
    const float spacing = 7.0f;
    for (float y = area.getY() + spacing * 0.5f; y < area.getBottom(); y += spacing)
        for (float x = area.getX() + spacing * 0.5f; x < area.getRight(); x += spacing)
        {
            const float wave = 0.5f + 0.5f * std::sin (x * 0.03f + y * 0.05f + time * 1.2f);
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.fillEllipse (juce::Rectangle<float> (3.2f, 3.2f).withCentre ({ x, y + 0.5f }));
            g.setColour (colours::faint.interpolatedWith (colours::lavender, glow * wave * 0.6f).withAlpha (0.35f + 0.4f * glow * wave));
            g.fillEllipse (juce::Rectangle<float> (2.2f, 2.2f).withCentre ({ x, y }));
        }
}

void Content::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.fillAll (colours::backdrop);

    // Chassis
    const auto chassis = bounds.reduced (4.0f);
    juce::Path chassisPath;
    chassisPath.addRoundedRectangle (chassis, radius::chassis);
    juce::ColourGradient body (colours::chassisTop, 0.0f, chassis.getY(), colours::chassisBot, 0.0f, chassis.getBottom(), false);
    g.setGradientFill (body);
    g.fillPath (chassisPath);
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (chassisPath);
        g.setTiledImageFill (grain, 0, 0, 1.0f);
        g.fillPath (chassisPath);
    }
    juce::ColourGradient rim (juce::Colours::white.withAlpha (0.10f), 0.0f, chassis.getY(),
                              juce::Colours::white.withAlpha (0.0f), 0.0f, chassis.getY() + 60.0f, false);
    g.setGradientFill (rim);
    g.strokePath (chassisPath, juce::PathStrokeType (1.0f));

    // Logo: crescent + wordmark
    drawCrescent (g, { 36.0f, 33.0f }, 8.0f, colours::gold, colours::chassisTop);
    g.setFont (mono (12.5f, true, 0.38f));
    g.setColour (colours::text.withAlpha (0.85f));
    g.drawText ("SPECTRA", 52, 22, 140, 22, juce::Justification::centredLeft, false);

    // Status, top right: static cue for freeze state
    const bool frozen = spectraProcessor.getVisualState().frozen.load();
    g.setFont (mono (10.5f, false, 0.14f));
    g.setColour (frozen ? colours::gold : colours::dim);
    g.drawText (frozen ? "FROZEN" : "LISTENING", baseWidth - 30 - 120, 22, 108, 22, juce::Justification::centredRight, false);
    const auto led = juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ (float) baseWidth - 34.0f, 33.0f });
    g.setColour (frozen ? colours::gold : colours::faint);
    g.fillEllipse (led);

    // Tray
    recess (g, trayArea, radius::tray, colours::tray);
    paintGrille (g, juce::Rectangle<float> (534.0f, 76.0f, 392.0f, 382.0f), grilleGlow);

    // Card shadows (cards stand proud of the tray)
    for (auto* card : { freezeCard.get(), hazeCard.get(), orbitCard.get() })
        dropShadow (g, card->getBounds().toFloat(), radius::card, 0.6f, 22, { 0, 12 });

    // Wordmark under the screen
    g.setFont (light (30.0f, 0.02f));
    g.setColour (colours::text.withAlpha (0.8f));
    g.drawText ("Spectra", 46, 422, 140, 40, juce::Justification::centredLeft, false);
    g.setFont (light (14.0f, 0.04f));
    g.setColour (colours::dim);
    g.drawText ("night freeze", 152, 430, 160, 30, juce::Justification::centredLeft, false);

    // Footer
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRect (30.0f, 482.0f, (float) baseWidth - 60.0f, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.fillRect (30.0f, 483.0f, (float) baseWidth - 60.0f, 1.0f);

    g.setFont (mono (10.5f, false, 0.16f));
    g.setColour (colours::dim);
    g.drawText ("SPECTRAL FREEZE  /  GRANULAR TEXTURE", 34, 494, 360, 24, juce::Justification::centredLeft, false);
    g.drawText ("v1.0", baseWidth - 30 - 34, 494, 34, 24, juce::Justification::centredRight, false);
}

void Content::tick (float dt)
{
    time += dt;

    motionCheckAccumulator += dt;
    if (motionCheckAccumulator > 1.0f)
    {
        motionCheckAccumulator = 0.0f;
        reducedMotion = prefersReducedMotion();
    }

    auto& display = screen.getDisplay();
    displayAccumulator += dt;
    if (displayAccumulator >= 1.0f / 30.0f)
    {
        displayAccumulator = std::fmod (displayAccumulator, 1.0f / 30.0f);
        display.tick (reducedMotion);

        const float e = display.getEnergy();
        auto norm = [this] (const char* id) { return spectraProcessor.apvts.getParameter (id)->getValue(); };
        freezeCard->tick (1.0f / 30.0f, e, time, reducedMotion, 0.0f);
        hazeCard->tick   (1.0f / 30.0f, e, time, reducedMotion, norm ("diffuse"));
        orbitCard->tick  (1.0f / 30.0f, e, time, reducedMotion, norm ("mix"));

        const float targetGlow = reducedMotion ? e * 0.5f : e;
        if (std::abs (targetGlow - grilleGlow) > 0.004f || (! reducedMotion && grilleGlow > 0.01f))
        {
            grilleGlow += (targetGlow - grilleGlow) * 0.15f;
            repaint (juce::Rectangle<int> (534, 76, 392, 382));
        }
        repaint (juce::Rectangle<int> (baseWidth - 170, 18, 150, 30));   // status cue
    }

    presetDisplay.refresh();
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
    g.fillAll (spectra::ui::colours::backdrop);
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
