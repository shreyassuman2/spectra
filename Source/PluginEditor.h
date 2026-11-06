#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

namespace spectra::ui
{

namespace colours
{
    const juce::Colour background { 0xff0b0d11 };
    const juce::Colour panel      { 0xff141821 };
    const juce::Colour panelHi    { 0xff1b2130 };
    const juce::Colour hairline   { 0xff232a36 };
    const juce::Colour cyan       { 0xff8fd3ff };
    const juce::Colour violet     { 0xffb69cff };
    const juce::Colour text       { 0xffd8dee9 };
    const juce::Colour dim        { 0xff6b7480 };
}

//==============================================================================
class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh,
                       juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override;
    juce::Font getPopupMenuFont() override;
    void drawTooltip (juce::Graphics&, const juce::String&, int w, int h) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String&, juce::Point<int>, juce::Rectangle<int>) override;
};

//==============================================================================
class Knob final : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState&, const juce::String& paramId,
          const juce::String& label, const juce::String& tooltip, bool bipolar = false);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Slider slider;
    juce::String label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

//==============================================================================
class FreezeOrb final : public juce::Button
{
public:
    FreezeOrb();
    void setLevel (float newLevel, float time);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    float level = 0.0f, phase = 0.0f;
};

//==============================================================================
class Display final : public juce::Component
{
public:
    explicit Display (VisualState&);
    void tick();   // called at 30 Hz
    void paint (juce::Graphics&) override;

    float getEnergy() const noexcept { return energy; }

private:
    struct Particle { float x, y, vx, vy, life, maxLife, size, hue; };

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
    bool frozen = false;
};

//==============================================================================
class Content final : public juce::Component
{
public:
    static constexpr int baseWidth = 860, baseHeight = 560;

    explicit Content (SpectraProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();

private:
    struct Section { juce::String name; int row, firstCol, numCols; };

    SpectraProcessor& spectraProcessor;
    Display display;
    FreezeOrb orb;
    juce::ComboBox presetBox;
    juce::TextButton randomizeButton { "Randomize" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAttachment;
    std::vector<std::unique_ptr<Knob>> row1, row2;
    std::vector<Section> sections;
    juce::Rectangle<int> controlsArea, orbArea;
    float time = 0.0f;

    juce::Rectangle<int> cellBounds (int row, int col) const;
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

    SpectraProcessor& spectraProcessor;
    spectra::ui::LookAndFeel lnf;
    spectra::ui::Content content;
    juce::TooltipWindow tooltipWindow { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectraEditor)
};
