#include "PluginProcessor.h"
#include "PluginEditor.h"

using puppychow::OctaveEngine;
using puppychow::channels;
using puppychow::numChannels;

PuppyChowProcessor::PuppyChowProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PuppyChow", createParameterLayout())
{
    for (size_t i = 0; i < (size_t) numChannels; ++i)
    {
        levelDb[i] = parameters.getRawParameterValue (channels[i].id);
        channelOn[i] = parameters.getRawParameterValue (juce::String (channels[i].id) + "_on");
    }

    detuneCents = parameters.getRawParameterValue ("detune");
    swellMs = parameters.getRawParameterValue ("swell");
    trackingMode = parameters.getRawParameterValue ("tracking");
}

juce::AudioProcessorValueTreeState::ParameterLayout PuppyChowProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (const auto& ch : channels)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ch.id, 1 }, ch.name,
            juce::NormalisableRange<float> (puppychow::minLevelDb, 6.0f, 0.1f, 2.0f),
            ch.defaultDb,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { juce::String (ch.id) + "_on", 1 }, juce::String (ch.name) + " On", ch.defaultOn));
    }

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "detune", 1 }, "Detune",
        juce::NormalisableRange<float> (0.0f, puppychow::maxDetuneCents, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("cents")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "swell", 1 }, "Swell",
        juce::NormalisableRange<float> (0.0f, puppychow::maxSwellMs, 1.0f, 0.5f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "tracking", 1 }, "Tracking",
        juce::StringArray { "Tight", "Balanced", "Clean" }, 2));

    return layout;
}

float PuppyChowProcessor::channelTargetGain (int channel) const noexcept
{
    const auto i = (size_t) channel;
    const float dbValue = levelDb[i]->load();
    const bool on = channelOn[i]->load() > 0.5f;
    return (! on || dbValue <= puppychow::minLevelDb + 0.05f) ? 0.0f : juce::Decibels::decibelsToGain (dbValue);
}

void PuppyChowProcessor::prepareToPlay (double sampleRate, int)
{
    sampleRateHz = sampleRate;

    engines[0].prepare (sampleRate, OctaveEngine::Tuning::tight());
    engines[1].prepare (sampleRate, OctaveEngine::Tuning::balanced());
    engines[2].prepare (sampleRate, OctaveEngine::Tuning::clean());
    activeEngine = -1;

    for (size_t i = 0; i < gains.size(); ++i)
    {
        gains[i].reset (sampleRate, 0.02); // 20 ms smoothing avoids zipper noise
        gains[i].setCurrentAndTargetValue (channelTargetGain ((int) i));
    }

    detuneMix.reset (sampleRate, 0.05);
    detuneMix.setCurrentAndTargetValue (juce::jlimit (0.0f, 1.0f, detuneCents->load() / 3.0f));

    fastEnv = slowEnv = 0.0f;
    swellRamp = swellGain = 1.0f;
    samplesSinceOnset = 0;

    // Gentle 20 Hz high-pass on the voices so two octaves below a low note
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

    // Tracking mode: switch engines at a block boundary, starting the new one clean.
    const int wantedEngine = juce::jlimit (0, numTrackingModes - 1, (int) std::lround (trackingMode->load()));
    if (wantedEngine != activeEngine)
    {
        activeEngine = wantedEngine;
        engines[(size_t) activeEngine].reset();
    }
    auto& engine = engines[(size_t) activeEngine];

    // Levels, and which voices the engine can skip because nobody hears them.
    bool active[OctaveEngine::numVoices] = {};
    for (int i = 0; i < numChannels; ++i)
    {
        auto& g = gains[(size_t) i];
        g.setTargetValue (channelTargetGain (i));
        if (channels[i].engineVoice >= 0)
            active[channels[i].engineVoice] = g.getTargetValue() > 0.0f || g.isSmoothing();
    }

    const float cents = detuneCents->load();
    engine.setDetuneCents (cents);
    // The detuned copy fades in over the first 3 cents so the slider has no step.
    detuneMix.setTargetValue (juce::jlimit (0.0f, 1.0f, cents / 3.0f));
    const bool detuneOn = detuneMix.getTargetValue() > 0.0f || detuneMix.isSmoothing();
    active[OctaveEngine::up1Detuned] = detuneOn && active[OctaveEngine::up1];
    active[OctaveEngine::up2Detuned] = detuneOn && active[OctaveEngine::up2];
    engine.setActiveVoices (active);

    // Swell settings.
    const float swellTimeMs = swellMs->load();
    const bool swellOn = swellTimeMs >= 1.0f;
    const float swellStep = swellOn ? (float) (1000.0 / (swellTimeMs * sampleRateHz)) : 1.0f;
    const float fastRelease = (float) std::exp (-1.0 / (0.020 * sampleRateHz));
    const float slowCoeff = (float) std::exp (-1.0 / (0.080 * sampleRateHz));
    const float gainSmooth = (float) std::exp (-1.0 / (0.003 * sampleRateHz));
    const int holdoff = (int) (0.060 * sampleRateHz);

    // Work in small fixed-size chunks so no memory is ever allocated here.
    constexpr int chunk = 64;
    constexpr int numVoices = OctaveEngine::numVoices;
    float mono[chunk];
    float voices[numVoices][chunk];
    float* voicePtrs[numVoices];
    for (int v = 0; v < numVoices; ++v)
        voicePtrs[v] = voices[v];

    for (int start = 0; start < numSamples; start += chunk)
    {
        const int n = juce::jmin (chunk, numSamples - start);

        // The engine listens to a mono mix of the input.
        const int sourceChannels = juce::jmax (1, juce::jmin (numIn, numOut));
        const float inScale = 1.0f / (float) sourceChannels;
        for (int i = 0; i < n; ++i)
            mono[i] = 0.0f;
        for (int ch = 0; ch < sourceChannels; ++ch)
        {
            const float* src = buffer.getReadPointer (ch, start);
            for (int i = 0; i < n; ++i)
                mono[i] += src[i] * inScale;
        }

        engine.process (mono, voicePtrs, n);

        for (int i = 0; i < n; ++i)
        {
            // Mix the voices. The octave-up voices blend in their detuned copies.
            const float det = detuneMix.getNextValue();
            float wet = 0.0f;
            float dry = 1.0f;

            for (int c = 0; c < numChannels; ++c)
            {
                const float g = gains[(size_t) c].getNextValue();
                const int v = channels[c].engineVoice;

                if (v < 0)
                    dry = g;
                else if (v == OctaveEngine::up1)
                    wet += g * (voices[v][i] * (1.0f - 0.3f * det) + voices[OctaveEngine::up1Detuned][i] * 0.7f * det);
                else if (v == OctaveEngine::up2)
                    wet += g * (voices[v][i] * (1.0f - 0.3f * det) + voices[OctaveEngine::up2Detuned][i] * 0.7f * det);
                else
                    wet += g * voices[v][i];
            }

            // Swell: watch the input level. When it jumps (a new note is picked),
            // drop the voices to silence and fade them back in over the swell time.
            const float level = std::abs (mono[i]);
            fastEnv = level > fastEnv ? level : level + fastRelease * (fastEnv - level);
            slowEnv = fastEnv + slowCoeff * (slowEnv - fastEnv);
            ++samplesSinceOnset;
            if (fastEnv > 2.0f * slowEnv + 0.003f && samplesSinceOnset > holdoff)
            {
                samplesSinceOnset = 0;
                if (swellOn)
                    swellRamp = 0.0f;
            }
            swellRamp = swellOn ? juce::jmin (1.0f, swellRamp + swellStep) : 1.0f;
            swellGain = swellRamp + gainSmooth * (swellGain - swellRamp);
            wet *= swellGain;

            // One-pole high-pass: subtract a slow-moving average.
            wetHighpassState = wet + wetHighpassCoeff * (wetHighpassState - wet);
            wet -= wetHighpassState;

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
