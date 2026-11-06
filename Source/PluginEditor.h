#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

namespace spectra::ui
{

/** Minimal night palette: one flat surface, one value colour, gold only for "frozen" and "touching". */
namespace colours
{
    const juce::Colour background { 0xff100d1e };
    const juce::Colour surface    { 0xff17132b };
    const juce::Colour well       { 0xff0c0a18 };
    const juce::Colour control    { 0xff1f1a38 };
    const juce::Colour line       { 0xff2a2445 };
    const juce::Colour lavender   { 0xffb69cff };
    const juce::Colour gold       { 0xfff0c27a };
    const juce::Colour text       { 0xffe6e1f5 };
    const juce::Colour dim        { 0xff8b85a6 };
    const juce::Colour faint      { 0xff4a4466 };
}

/** Smoothly approaches a target; retargets mid-flight (never a fixed timeline). */
struct Smoothed
{
    float value = 0.0f, target = 0.0f;
    void snap (float v) { value = target = v; }
    bool update (float dt, float tau)
    {
        const float before = value;
        value += (target - value) * (1.0f - std::exp (-dt / std::max (tau, 1.0e-4f)));
        if (std::abs (target - value) < 0.002f) value = target;
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
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText, const juce::Drawable* icon,
                            const juce::Colour* textColour) override;
    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                    int& idealWidth, int& idealHeight) override;
    int getPopupMenuBorderSize() override { return 6; }
    void drawTooltip (juce::Graphics&, const juce::String&, int w, int h) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String&, juce::Point<int>, juce::Rectangle<int>) override;
    void fillTextEditorBackground (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int w, int h, juce::TextEditor&) override;
};

//==============================================================================
/** Slider + parameter attachment with double-click reset and Shift for fine control. */
struct BoundSlider
{
    BoundSlider (juce::AudioProcessorValueTreeState&, const juce::String& paramId,
                 juce::Slider::SliderStyle, const juce::String& tooltip, bool bipolar);
    mutable juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    juce::String valueText() const { return slider.getTextFromValue (slider.getValue()); }
};

/** Simple flat knob with its name and value underneath. */
class Knob final : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& label,
          const juce::String& tooltip, bool bipolar = false);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    BoundSlider bound;
    juce::String label;
};

/** Settings-page row: name and value on one line, a thin slider underneath. */
class SliderRow final : public juce::Component
{
public:
    SliderRow (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& label,
               const juce::String& tooltip, bool bipolar = false);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    BoundSlider bound;
    juce::String label;
};

//==============================================================================
class FreezeButton final : public juce::Button
{
public:
    FreezeButton();
    void tick (float dt, bool reducedMotion);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    Smoothed on, press;
};

/** Lowercase text button; gold on hover, optionally marked as selected. */
class TextLink final : public juce::Button
{
public:
    explicit TextLink (const juce::String& text) : juce::Button (text) {}
    void setSelected (bool s) { if (s != selected) { selected = s; repaint(); } }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    bool selected = false;
};

//==============================================================================
class SpectrumView final : public juce::Component
{
public:
    explicit SpectrumView (VisualState&);
    void tick();   // ~30 Hz
    void paint (juce::Graphics&) override;

private:
    juce::Rectangle<float> curvesArea() const;
    juce::Rectangle<float> stripArea() const;
    juce::Path makeCurve (const std::array<float, VisualState::numBands>&, juce::Rectangle<float>, bool closed) const;
    void rebuildThumbnail();

    VisualState& vs;
    std::array<float, VisualState::numBands> input {}, pad {};
    juce::Image thumbImage;
    int lastThumbVersion = -1, thumbCols = 0;
    float regionStart = 0.0f;
    std::array<float, VisualState::numVoices> playheads {}, levels {};
    bool frozen = false;
};

/** Preset list shown on the screen: factory presets with a one-line description, then the
    user's own presets. Click to load; right-click a user preset to delete it. "save as..." stores
    the current sound under a new name. */
class PresetsPage final : public juce::Component
{
public:
    explicit PresetsPage (SpectraProcessor&);
    void refresh();   // re-reads the user preset folder
    void tick();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class List final : public juce::Component
    {
    public:
        explicit List (PresetsPage& o) : owner (o) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
        void paint (juce::Graphics&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        int layoutHeight() const;
        int rowAt (juce::Point<int>) const;               // program index, or -1
        juce::Rectangle<int> rowBounds (int program) const;

    private:
        PresetsPage& owner;
        int hovered = -1;
    };

    void updateLayout();
    void beginSave();
    void finishSave (bool commit);
    void showUserPresetMenu (int program);

    static constexpr int rowH = 36, headerH = 30, barH = 44;

    SpectraProcessor& proc;
    List list { *this };
    juce::Viewport viewport;
    juce::TextEditor nameEditor;
    TextLink saveLink { "save as..." }, confirmLink { "save" }, cancelLink { "cancel" };
    int shownProgram = -1, shownCount = -1;
};

class SettingsPage final : public juce::Component
{
public:
    SettingsPage (SpectraProcessor&);
    void resized() override;

private:
    std::vector<std::unique_ptr<SliderRow>> rows;
    TextLink randomize { "randomize texture" };
};

/** The screen: three tabs ("spectrum", "settings", "presets") that cross-fade. */
class Screen final : public juce::Component
{
public:
    explicit Screen (SpectraProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick (float dt, bool reducedMotion);

    enum Page { spectrumPage, settingsPage, presetsPage };
    void showPage (int);
    int getPage() const { return page; }

private:
    SpectrumView spectrum;
    SettingsPage settings;
    PresetsPage presets;
    TextLink spectrumTab { "spectrum" }, settingsTab { "settings" }, presetsTab { "presets" };
    std::array<juce::Component*, 3> pages {};
    std::array<TextLink*, 3> tabs {};
    std::array<Smoothed, 3> pageAlpha {};
    int page = spectrumPage;
    float spectrumAccumulator = 0.0f;
};

//==============================================================================
/** Preset name between two chevrons; click the name for the list. */
class PresetNav final : public juce::Component
{
public:
    explicit PresetNav (SpectraProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }
    void refresh();
    std::function<void()> onNameClicked;

private:
    struct Chevron final : public juce::Button
    {
        Chevron (const juce::String& n, bool leftFacing) : juce::Button (n), left (leftFacing) {}
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
        bool left;
    };
    void step (int delta);
    juce::Rectangle<int> nameArea() const;

    SpectraProcessor& proc;
    Chevron prev { "Previous preset", true }, next { "Next preset", false };
    int shownProgram = -1;
};

//==============================================================================
class Content final : public juce::Component
{
public:
    static constexpr int baseWidth = 860, baseHeight = 500;

    Content (SpectraProcessor&, std::function<void (float)> onScaleChosen);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick (float dt);
    void setScaleLabel (float scale);

private:
    void showScaleMenu();

    SpectraProcessor& spectraProcessor;
    std::function<void (float)> onScaleChosen;

    PresetNav presetNav;
    Screen screen;
    FreezeButton freeze;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAttachment;
    TextLink scaleButton { "100%" };

    bool reducedMotion = false;
    float motionCheckAccumulator = 0.0f;
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
    juce::TooltipWindow tooltipWindow { this, 700 };
    double lastTick = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectraEditor)
};
