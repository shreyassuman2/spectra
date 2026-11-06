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

    /** Rolls new random values for the texture parameters only. Call from the message thread. */
    void randomizeTexture();

    float getUiScale() const;
    void  setUiScale (float);

    spectra::VisualState& getVisualState() noexcept { return engine.getVisualState(); }

    juce::AudioProcessorValueTreeState apvts;

    static juce::StringArray textureParamIds();

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void setParam (const juce::String& id, double plainValue);

    spectra::SpectralEngine engine;
    juce::AudioBuffer<float> wetBuffer;
    std::vector<float> duckBuffer;
    juce::SmoothedValue<float> mixSmoothed, levelSmoothed;
    int currentProgram = 0;
    juce::Random random;

    struct ParamPointers
    {
        std::atomic<float>* freeze = nullptr;
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
