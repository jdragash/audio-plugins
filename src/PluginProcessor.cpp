#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr float offDb = -60.0f; // bottom of every level knob means "off"

    struct LevelParam { const char* id; const char* name; float defaultDb; };

    // Order matters: Dry first, then the voices in OctaveEngine::Voice order.
    const LevelParam levelParams[] = {
        { "dry",   "Dry",           0.0f },
        { "down2", "-2 Octaves",  offDb },
        { "down1", "-1 Octave",   -6.0f },
        { "up1",   "+1 Octave",   -6.0f },
        { "up2",   "+2 Octaves",  offDb },
    };

    float gainFromDb (float dbValue) noexcept
    {
        return dbValue <= offDb + 0.05f ? 0.0f : juce::Decibels::decibelsToGain (dbValue);
    }
}

PuppyChowProcessor::PuppyChowProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PuppyChow", createParameterLayout())
{
    for (size_t i = 0; i < levelDb.size(); ++i)
        levelDb[i] = parameters.getRawParameterValue (levelParams[i].id);
}

juce::AudioProcessorValueTreeState::ParameterLayout PuppyChowProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (const auto& p : levelParams)
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { p.id, 1 }, p.name,
            juce::NormalisableRange<float> (offDb, 6.0f, 0.1f, 2.0f),
            p.defaultDb,
            juce::AudioParameterFloatAttributes()
                .withLabel ("dB")
                .withStringFromValueFunction ([] (float v, int) { return v <= offDb + 0.05f ? juce::String ("Off") : juce::String (v, 1); })
                .withValueFromStringFunction ([] (const juce::String& t) { return t.trim().equalsIgnoreCase ("off") ? offDb : t.getFloatValue(); })));

    return layout;
}

void PuppyChowProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);

    for (size_t i = 0; i < gains.size(); ++i)
    {
        gains[i].reset (sampleRate, 0.02); // 20 ms smoothing avoids zipper noise
        gains[i].setCurrentAndTargetValue (gainFromDb (levelDb[i]->load()));
    }

    // Gentle 20 Hz high-pass on the octaves so two octaves below a low note
    // can't push speakers around with inaudible rumble.
    wetHighpassCoeff = (float) std::exp (-juce::MathConstants<double>::twoPi * 20.0 / sampleRate);
    wetHighpassState = 0.0f;
}

bool PuppyChowProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    // Guitar is usually mono; support mono->mono, mono->stereo, and stereo->stereo.
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;
    return in.size() <= out.size();
}

void PuppyChowProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numIn  = getTotalNumInputChannels();
    const auto numOut = getTotalNumOutputChannels();
    const auto numSamples = buffer.getNumSamples();

    // Mono in, stereo out: copy the input to every output channel.
    for (auto ch = numIn; ch < numOut; ++ch)
    {
        if (numIn > 0)
            buffer.copyFrom (ch, 0, buffer, 0, 0, numSamples);
        else
            buffer.clear (ch, 0, numSamples);
    }

    for (size_t i = 0; i < gains.size(); ++i)
        gains[i].setTargetValue (gainFromDb (levelDb[i]->load()));

    // Work in small fixed-size chunks so no memory is ever allocated here.
    constexpr int chunk = 64;
    constexpr int numVoices = puppychow::OctaveEngine::numVoices;
    float mono[chunk];
    float voices[numVoices][chunk];
    float* voicePtrs[numVoices] = { voices[0], voices[1], voices[2], voices[3] };

    for (int start = 0; start < numSamples; start += chunk)
    {
        const int n = juce::jmin (chunk, numSamples - start);

        // The engine listens to a mono mix of the input.
        const float inScale = numIn > 1 ? 1.0f / (float) numIn : 1.0f;
        for (int i = 0; i < n; ++i)
            mono[i] = 0.0f;
        for (int ch = 0; ch < juce::jmax (1, juce::jmin (numIn, numOut)); ++ch)
        {
            const float* src = buffer.getReadPointer (ch, start);
            for (int i = 0; i < n; ++i)
                mono[i] += src[i] * inScale;
        }

        engine.process (mono, voicePtrs, n);

        for (int i = 0; i < n; ++i)
        {
            float wet = 0.0f;
            for (int v = 0; v < numVoices; ++v)
                wet += voices[v][i] * gains[(size_t) v + 1].getNextValue();

            // One-pole high-pass: subtract a slow-moving average.
            wetHighpassState = wet + wetHighpassCoeff * (wetHighpassState - wet);
            wet -= wetHighpassState;

            const float dry = gains[0].getNextValue();
            for (int ch = 0; ch < numOut; ++ch)
            {
                float* dst = buffer.getWritePointer (ch, start);
                dst[i] = dst[i] * dry + wet;
            }
        }
    }
}

juce::AudioProcessorEditor* PuppyChowProcessor::createEditor()
{
    return new PuppyChowEditor (*this);
}

void PuppyChowProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void PuppyChowProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PuppyChowProcessor();
}
