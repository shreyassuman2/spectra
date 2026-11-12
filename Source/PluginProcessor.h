#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SpectralEngine.h"

class SpectraProcessor final : public juce::AudioProcessor
{
public:
    SpectraProcessor();
    ~SpectraProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Spectra"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // User presets: one small XML file per preset in userPresetFolder(). Programs are the
    // factory presets first, then the user presets sorted by name. Call these from the message thread.
    static juce::File userPresetFolder();
    void refreshUserPresets();
    int  saveUserPreset (const juce::String& name);   // returns the new program index, or -1 on failure
    bool deleteUserPreset (int programIndex);
    juce::File getUserPresetFile (int programIndex) const;
    int  getNumFactoryPresets() const noexcept;
    bool isUserPreset (int programIndex) const noexcept { return programIndex >= getNumFactoryPresets(); }

    /** Rolls new random values for the texture parameters only. Call from the message thread. */
    void randomizeTexture();

    float getUiScale() const;
    void  setUiScale (float);

    spectra::VisualState& getVisualState() noexcept { return engine.getVisualState(); }

    juce::AudioProcessorValueTreeState apvts;

    static juce::StringArray textureParamIds();

    /** Choices of the "refreeze" parameter, in order. Index 0 is off. */
    static juce::StringArray refreezeChoices() { return { "off", "1 bar", "1/2", "1/4", "1/8" }; }

    /** Freeze and Re-freeze are performance controls: presets never save or change them. */
    static bool isPerformanceParam (const juce::String& id) { return id == "freeze" || id == "refreeze"; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void setParam (const juce::String& id, double plainValue);

    struct Trigger { int sample; float releaseCapMs; };
    /** Finds the re-freeze grid lines inside this block. Returns how many were written to `out`. */
    int findRefreezeTriggers (int numSamples, Trigger* out, int maxTriggers);
    double lastTriggerPpq = -1.0e9;   // audio thread only

    spectra::SpectralEngine engine;
    juce::AudioBuffer<float> wetBuffer;
    std::vector<float> duckBuffer;
    juce::SmoothedValue<float> mixSmoothed, levelSmoothed;
    void applyPresetFile (const juce::File&);

    struct UserPreset { juce::String name; juce::File file; };
    std::vector<UserPreset> userPresets;
    juce::CriticalSection userPresetLock;   // hosts may ask for program names from other threads
    std::atomic<int> currentProgram { 0 };
    juce::Random random;

    struct ParamPointers
    {
        std::atomic<float>* freeze = nullptr;
        std::atomic<float>* refreeze = nullptr;
        std::atomic<float>* size = nullptr;
        std::atomic<float>* scan = nullptr;
        std::atomic<float>* drift = nullptr;
        std::atomic<float>* spread = nullptr;
        std::atomic<float>* diffuse = nullptr;
        std::atomic<float>* absorb = nullptr;
        std::atomic<float>* blur = nullptr;
        std::atomic<float>* shift = nullptr;
        std::atomic<float>* shimmer = nullptr;
        std::atomic<float>* tilt = nullptr;
        std::atomic<float>* width = nullptr;
        std::atomic<float>* attack = nullptr;
        std::atomic<float>* release = nullptr;
        std::atomic<float>* mix = nullptr;
        std::atomic<float>* level = nullptr;
    } p;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectraProcessor)
};
