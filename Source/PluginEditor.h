#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

namespace spectra::ui
{

/** Night palette: indigo chassis, dusty-violet cards, pale lavender knobs, one warm moon-gold accent. */
namespace colours
{
    const juce::Colour backdrop     { 0xff07060b };
    const juce::Colour chassisTop   { 0xff221c36 };
    const juce::Colour chassisBot   { 0xff151124 };
    const juce::Colour tray         { 0xff0f0c19 };
    const juce::Colour glass        { 0xff0a0913 };
    const juce::Colour tile         { 0xff16122b };
    const juce::Colour tileHi       { 0xff201a3d };
    const juce::Colour cardTop      { 0xff342c52 };
    const juce::Colour cardBot      { 0xff28213f };
    const juce::Colour emblem       { 0xff3e3566 };
    const juce::Colour knob         { 0xffe7e1f7 };
    const juce::Colour knobShade    { 0xffa49cc6 };
    const juce::Colour lavender     { 0xffb69cff };
    const juce::Colour cyan         { 0xff8fd3ff };
    const juce::Colour gold         { 0xfff0c27a };
    const juce::Colour text         { 0xffe4def7 };
    const juce::Colour dim          { 0xff8a83a8 };
    const juce::Colour faint        { 0xff4a4366 };
    const juce::Colour line         { 0xff2c2547 };
}

namespace radius
{
    // Nested surfaces: outer = inner + padding.
    constexpr float chassis = 22.0f;
    constexpr float tray    = 18.0f;
    constexpr float bezelPad = 10.0f;
    constexpr float screen  = 14.0f;               // bezel
    constexpr float glass   = screen - bezelPad;   // 4: glass inside the bezel
    constexpr float card    = 12.0f;
    constexpr float cardPad = 8.0f;
    constexpr float emblem  = card - cardPad;      // 4
    constexpr float control = 8.0f;
}

/** Smoothly approaches a target; retargets mid-flight (never a fixed timeline). */
struct Smoothed
{
    float value = 0.0f, target = 0.0f;
    void snap (float v) { value = target = v; }
    bool update (float dt, float tauUp, float tauDown)
    {
        const float tau = target > value ? tauUp : tauDown;
        const float before = value;
        value += (target - value) * (1.0f - std::exp (-dt / std::max (tau, 1.0e-4f)));
        if (std::abs (target - value) < 0.001f) value = target;
        return ! juce::exactlyEqual (value, before);
    }
};

bool prefersReducedMotion();

//==============================================================================
class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int, int, int, int, float, float, float,
                           juce::Slider::SliderStyle, juce::Slider&) override {}   // tiles/bars paint themselves
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawTooltip (juce::Graphics&, const juce::String&, int w, int h) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String&, juce::Point<int>, juce::Rectangle<int>) override;
};

//==============================================================================
/** A parameter-bound slider plus the attachment, with reset-on-double-click and Shift for fine control. */
struct BoundSlider
{
    BoundSlider (juce::AudioProcessorValueTreeState&, const juce::String& paramId,
                 juce::Slider::SliderStyle, const juce::String& tooltip);
    mutable juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    juce::String valueText() const { return slider.getTextFromValue (slider.getValue()); }
    float normalised() const { return (float) slider.valueToProportionOfLength (slider.getValue()); }
};

/** Small pale knob with a label that turns into the value while you touch it. */
class SmallKnob final : public juce::Component
{
public:
    SmallKnob (juce::AudioProcessorValueTreeState&, const juce::String& paramId,
               const juce::String& label, const juce::String& tooltip, bool bipolar = false);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    BoundSlider bound;
    juce::String label;
};

//==============================================================================
/** Freeze button drawn as a moon inside the first card's emblem panel. */
class MoonButton final : public juce::Button
{
public:
    MoonButton();
    void tick (float dt, float energy, float time, bool reducedMotion);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    Smoothed on, press, glow;
    float time = 0.0f;
    bool still = false;
};

//==============================================================================
/** A standing module card: emblem panel, big knob, gold pill, small knobs, title, value slot. */
class ModuleCard final : public juce::Component
{
public:
    enum class Emblem { moon, constellation, planet };

    ModuleCard (juce::AudioProcessorValueTreeState&, Emblem, const juce::String& title,
                const juce::String& bigParam, const juce::String& bigLabel, const juce::String& bigTip,
                bool bigBipolar = false);

    void addSmallKnob (std::unique_ptr<SmallKnob>);
    MoonButton* getMoon() { return moon.get(); }
    void tick (float dt, float energy, float time, bool reducedMotion, float emblemParam);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Rectangle<float> emblemArea() const;
    void paintConstellation (juce::Graphics&, juce::Rectangle<float>) const;
    void paintPlanet (juce::Graphics&, juce::Rectangle<float>) const;

    Emblem emblem;
    juce::String title, bigLabel;
    BoundSlider big;
    std::unique_ptr<MoonButton> moon;
    std::vector<std::unique_ptr<SmallKnob>> smallKnobs;
    float energy = 0.0f, time = 0.0f, emblemParam = 0.0f;
    bool still = false;
};

//==============================================================================
/** Screen tile: a glyph or big number that you drag vertically. */
class Tile final : public juce::Component
{
public:
    enum class Glyph { position, scatter, number, blur };
    Tile (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& label,
          const juce::String& tooltip, Glyph);
    void paint (juce::Graphics&) override;
    void resized() override { bound.slider.setBounds (getLocalBounds()); }

private:
    BoundSlider bound;
    juce::String label;
    Glyph glyph;
};

/** Row of ticks lit up to the value; drag horizontally. */
class TickBar final : public juce::Component
{
public:
    TickBar (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& label,
             const juce::String& tooltip);
    void paint (juce::Graphics&) override;
    void resized() override { bound.slider.setBounds (getLocalBounds()); }

private:
    BoundSlider bound;
    juce::String label;
};

//==============================================================================
/** Spectrum (particles + curves) and the captured-slice strip, drawn on the glass. */
class Display final : public juce::Component
{
public:
    explicit Display (VisualState&);
    void tick (bool reducedMotion);   // ~30 Hz
    void paint (juce::Graphics&) override;
    float getEnergy() const noexcept { return energy; }

private:
    struct Particle { float x, y, vx, vy, life, maxLife, size, hue; };

    juce::Rectangle<float> curvesArea() const;
    juce::Rectangle<float> stripArea() const;
    void rebuildThumbnail();
    void spawnParticle (juce::Rectangle<float> area);
    juce::Path makeCurve (const std::array<float, VisualState::numBands>&, juce::Rectangle<float>, bool closed) const;

    VisualState& vs;
    std::array<float, VisualState::numBands> input {}, pad {};
    std::vector<Particle> particles;
    juce::Random rng;
    juce::Image thumbImage;
    int lastThumbVersion = -1, thumbCols = 0;
    float energy = 0.0f, centroid = 0.5f, regionStart = 0.0f, spawnAccumulator = 0.0f;
    std::array<float, VisualState::numVoices> playheads {}, levels {};
    bool frozen = false, showParticles = true;
};

/** The glass screen: tiles on top, display in the middle, tick bars at the bottom. */
class Screen final : public juce::Component
{
public:
    Screen (juce::AudioProcessorValueTreeState&, VisualState&);
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    Display& getDisplay() { return display; }

private:
    std::vector<std::unique_ptr<Tile>> tiles;
    std::unique_ptr<TickBar> attack, release;
    Display display;
};

//==============================================================================
/** Recessed name window with prev / next dots; click the name for the full list. */
class PresetDisplay final : public juce::Component
{
public:
    explicit PresetDisplay (SpectraProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }
    void refresh();

private:
    struct DotButton final : public juce::Button
    {
        explicit DotButton (const juce::String& n) : juce::Button (n) {}
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    };

    void step (int delta);
    juce::Rectangle<int> nameArea() const;

    SpectraProcessor& proc;
    DotButton prev { "Previous preset" }, next { "Next preset" };
    int shownProgram = -1;
};

/** Footer pill: outlined, mono text. */
class PillButton final : public juce::Button
{
public:
    explicit PillButton (const juce::String& text) : juce::Button (text) {}
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

class DiceButton final : public juce::Button
{
public:
    DiceButton() : juce::Button ("Randomize") {}
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

//==============================================================================
class Content final : public juce::Component
{
public:
    static constexpr int baseWidth = 960, baseHeight = 540;

    Content (SpectraProcessor&, std::function<void (float)> onScaleChosen);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick (float dt);
    void setScaleLabel (float scale);

private:
    void showScaleMenu();
    void paintGrille (juce::Graphics&, juce::Rectangle<float>, float glow) const;

    SpectraProcessor& spectraProcessor;
    std::function<void (float)> onScaleChosen;

    PresetDisplay presetDisplay;
    Screen screen;
    std::unique_ptr<ModuleCard> freezeCard, hazeCard, orbitCard;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAttachment;
    DiceButton randomizeButton;
    PillButton scaleButton { "100%" };

    juce::Image grain;
    juce::Rectangle<float> trayArea;
    bool reducedMotion = false;
    float time = 0.0f, displayAccumulator = 0.0f, motionCheckAccumulator = 0.0f, grilleGlow = 0.0f;
};

} // namespace spectra::ui

//==============================================================================
class SpectraEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SpectraEditor (SpectraProcessor&);
    ~SpectraEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void applyScale (float scale);

    SpectraProcessor& spectraProcessor;
    spectra::ui::LookAndFeel lnf;
    spectra::ui::Content content;
    juce::TooltipWindow tooltipWindow { this, 600 };
    double lastTick = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectraEditor)
};
