#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

namespace
{
    using Range = juce::NormalisableRange<float>;

    juce::String percentText (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }
    float percentValue (const juce::String& t) { return t.retainCharacters ("-0123456789.").getFloatValue() / 100.0f; }

    juce::String timeText (float ms, int)
    {
        return ms < 1000.0f ? juce::String (juce::roundToInt (ms)) + " ms"
                            : juce::String (ms / 1000.0f, 2) + " s";
    }
    float timeValue (const juce::String& t)
    {
        const float v = t.retainCharacters ("0123456789.").getFloatValue();
        const auto lower = t.toLowerCase();
        return (lower.contains ("s") && ! lower.contains ("ms")) ? v * 1000.0f : v;
    }

    juce::String semisText (float v, int)
    {
        const float r = std::round (v * 10.0f) / 10.0f;
        if (std::abs (r) < 0.05f) return "0 st";
        return (r > 0.0f ? "+" : "") + juce::String (r, 1) + " st";
    }

    juce::String tiltText (float v, int)
    {
        const float db = v * 6.0f;
        if (std::abs (db) < 0.05f) return "flat";
        return (db > 0.0f ? "+" : "") + juce::String (db, 1) + " dB/oct";
    }

    juce::String dbText (float v, int) { return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB"; }

    float numberValue (const juce::String& t) { return t.retainCharacters ("-0123456789.").getFloatValue(); }

    Range logRange (float lo, float hi, float centre)
    {
        Range r (lo, hi);
        r.setSkewForCentre (centre);
        return r;
    }

    std::unique_ptr<juce::AudioParameterFloat> percentParam (const char* id, const char* name, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name, Range (0.0f, 1.0f), def,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentText)
                                                 .withValueFromStringFunction (percentValue));
    }

    std::unique_ptr<juce::AudioParameterFloat> timeParam (const char* id, const char* name,
                                                          float lo, float hi, float centre, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name, logRange (lo, hi, centre), def,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (timeText)
                                                 .withValueFromStringFunction (timeValue)
                                                 .withLabel ("ms"));
    }
}

//==============================================================================
SpectraProcessor::SpectraProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SpectraState", createLayout())
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    p.freeze  = get ("freeze");
    p.size    = get ("size");
    p.scan    = get ("scan");
    p.drift   = get ("drift");
    p.spread  = get ("spread");
    p.diffuse = get ("diffuse");
    p.absorb  = get ("absorb");
    p.blur    = get ("blur");
    p.shift   = get ("shift");
    p.shimmer = get ("shimmer");
    p.tilt    = get ("tilt");
    p.width   = get ("width");
    p.attack  = get ("attack");
    p.release = get ("release");
    p.mix     = get ("mix");
    p.level   = get ("level");
}

juce::AudioProcessorValueTreeState::ParameterLayout SpectraProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "freeze", 1 }, "Freeze", false));

    layout.add (timeParam ("size", "Size", 20.0f, 2000.0f, 300.0f, 400.0f));
    layout.add (percentParam ("absorb",  "Absorb",  0.0f));
    layout.add (percentParam ("scan",    "Scan",    0.5f));
    layout.add (percentParam ("drift",   "Drift",   0.2f));
    layout.add (percentParam ("spread",  "Spread",  0.1f));
    layout.add (percentParam ("diffuse", "Diffuse", 0.3f));
    layout.add (percentParam ("blur",    "Blur",    0.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "shift", 1 }, "Shift", Range (-24.0f, 24.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (semisText)
                                             .withValueFromStringFunction (numberValue)
                                             .withLabel ("st")));

    layout.add (percentParam ("shimmer", "Shimmer", 0.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "tilt", 1 }, "Tone", Range (-1.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (tiltText)
                                             .withValueFromStringFunction ([] (const juce::String& t)
                                                                           { return numberValue (t) / 6.0f; })));

    layout.add (percentParam ("width", "Width", 0.6f));

    layout.add (timeParam ("attack",  "Attack",  10.0f, 5000.0f,  400.0f,  400.0f));
    layout.add (timeParam ("release", "Release", 10.0f, 10000.0f, 1000.0f, 1500.0f));

    layout.add (percentParam ("mix", "Mix", 0.7f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "level", 1 }, "Level", Range (-24.0f, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (dbText)
                                             .withValueFromStringFunction (numberValue)
                                             .withLabel ("dB")));
    return layout;
}

juce::StringArray SpectraProcessor::textureParamIds()
{
    return { "scan", "drift", "spread", "diffuse", "blur", "tilt", "width", "shimmer" };
}

//==============================================================================
bool SpectraProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in  = layouts.getMainInputChannelSet();
    return out == juce::AudioChannelSet::stereo()
        && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void SpectraProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();

    engine.prepare (sampleRate, numIn, numOut);

    const int chunk = std::max (samplesPerBlock, 512);
    wetBuffer.setSize (std::max (2, numOut), chunk);
    duckBuffer.assign ((size_t) chunk, 0.0f);

    mixSmoothed.reset (sampleRate, 0.05);
    levelSmoothed.reset (sampleRate, 0.05);
    mixSmoothed.setCurrentAndTargetValue (p.mix->load());
    levelSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (p.level->load()));
    setLatencySamples (0);
}

void SpectraProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn  = std::max (1, getTotalNumInputChannels());
    const int numOut = std::min (getTotalNumOutputChannels(), buffer.getNumChannels());
    const int numSamples = buffer.getNumSamples();

    // Mono in -> stereo out: duplicate the dry signal into the extra outputs.
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.copyFrom (ch, 0, buffer, 0, 0, numSamples);

    spectra::EngineParams ep;
    ep.freeze     = p.freeze->load() > 0.5f;
    ep.sizeMs     = p.size->load();
    ep.scan       = p.scan->load();
    ep.drift      = p.drift->load();
    ep.spread     = p.spread->load();
    ep.diffuse    = p.diffuse->load();
    ep.absorb     = p.absorb->load();
    ep.blur       = p.blur->load();
    ep.shiftSemis = p.shift->load();
    ep.shimmer    = p.shimmer->load();
    ep.tilt       = p.tilt->load();
    ep.width      = p.width->load();
    ep.attackMs   = p.attack->load();
    ep.releaseMs  = p.release->load();

    mixSmoothed.setTargetValue (p.mix->load());
    levelSmoothed.setTargetValue (juce::Decibels::decibelsToGain (p.level->load()));

    const float halfPi = juce::MathConstants<float>::halfPi;
    const int maxChunk = std::min (wetBuffer.getNumSamples(), (int) duckBuffer.size());

    for (int offset = 0; offset < numSamples; offset += maxChunk)
    {
        const int n = std::min (maxChunk, numSamples - offset);

        const float* inputs[2] = { buffer.getReadPointer (0, offset),
                                   buffer.getReadPointer (std::min (1, numIn - 1), offset) };
        float* wet[2] = { wetBuffer.getWritePointer (0), wetBuffer.getWritePointer (1) };

        engine.process (inputs, wet, duckBuffer.data(), n, ep);

        for (int i = 0; i < n; ++i)
        {
            const float mix   = mixSmoothed.getNextValue();
            const float level = levelSmoothed.getNextValue();
            const float wetGain = std::sin (mix * halfPi) * level;
            const float dryGain = 1.0f + (std::cos (mix * halfPi) - 1.0f) * duckBuffer[(size_t) i];

            for (int ch = 0; ch < numOut; ++ch)
            {
                float* out = buffer.getWritePointer (ch, offset);
                out[i] = out[i] * dryGain + wet[std::min (ch, 1)][i] * wetGain;
            }
        }
    }
}

//==============================================================================
int SpectraProcessor::getNumPrograms() { return (int) spectra::factoryPresets.size(); }

const juce::String SpectraProcessor::getProgramName (int index)
{
    if (juce::isPositiveAndBelow (index, getNumPrograms()))
        return spectra::factoryPresets[(size_t) index].name;
    return {};
}

void SpectraProcessor::setParam (const juce::String& id, double plainValue)
{
    if (auto* param = apvts.getParameter (id))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 (static_cast<float> (plainValue)));
        param->endChangeGesture();
    }
}

void SpectraProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms()))
        return;

    currentProgram = index;
    const auto& pr = spectra::factoryPresets[(size_t) index];
    setParam ("size", pr.size);       setParam ("scan", pr.scan);
    setParam ("drift", pr.drift);     setParam ("spread", pr.spread);
    setParam ("diffuse", pr.diffuse); setParam ("absorb", pr.absorb);
    setParam ("blur", pr.blur);       setParam ("shift", pr.shift);
    setParam ("shimmer", pr.shimmer); setParam ("tilt", pr.tilt);
    setParam ("width", pr.width);     setParam ("attack", pr.attack);
    setParam ("release", pr.release); setParam ("mix", pr.mix);
    setParam ("level", pr.level);
    // "freeze" is intentionally untouched.
}

void SpectraProcessor::randomizeTexture()
{
    for (const auto& id : textureParamIds())
        if (auto* param = apvts.getParameter (id))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (random.nextFloat());
            param->endChangeGesture();
        }
}

float SpectraProcessor::getUiScale() const
{
    return (float) apvts.state.getProperty ("uiScale", 1.0f);
}

void SpectraProcessor::setUiScale (float s)
{
    apvts.state.setProperty ("uiScale", s, nullptr);
}

//==============================================================================
void SpectraProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SpectraProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            currentProgram = juce::jlimit (0, getNumPrograms() - 1, (int) state.getProperty ("program", 0));
            apvts.replaceState (state);
        }
}

juce::AudioProcessorEditor* SpectraProcessor::createEditor()
{
    return new SpectraEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpectraProcessor();
}
