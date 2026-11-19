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
        channelPan[i] = parameters.getRawParameterValue (juce::String (channels[i].id) + "_pan");
    }

    detuneCents = parameters.getRawParameterValue ("detune");
    swellMs = parameters.getRawParameterValue ("swell");
    swellDry = parameters.getRawParameterValue ("swell_dry");
    trackingMode = parameters.getRawParameterValue ("tracking");
    outputDb = parameters.getRawParameterValue ("output");
}

namespace
{
    // Gentle safety limiter on the final output. Below -2 dBFS nothing changes;
    // above it, peaks are rounded off smoothly so they never exceed 0 dBFS.
    // Stacking several voices on a hard strum can otherwise overload whatever
    // comes next, which sounds like crackle.
    inline float softLimit (float x) noexcept
    {
        constexpr float knee = 0.8f, room = 1.0f - knee;
        const float a = std::abs (x);
        if (a <= knee)
            return x;
        const float y = knee + room * std::tanh ((a - knee) / room);
        return x < 0.0f ? -y : y;
    }
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

        // Pan: -100 is hard left, 0 centre, +100 hard right.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { juce::String (ch.id) + "_pan", 1 }, juce::String (ch.name) + " Pan",
            juce::NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int)
                {
                    const int amount = (int) std::lround (std::abs (v));
                    return amount == 0 ? juce::String ("C") : juce::String (v < 0 ? "L" : "R") + juce::String (amount);
                })
                .withValueFromStringFunction ([] (const juce::String& text)
                {
                    const auto t = text.trim().toUpperCase();
                    if (t.isEmpty() || t == "C") return 0.0f;
                    if (t.startsWithChar ('L')) return -std::abs (t.substring (1).getFloatValue());
                    if (t.startsWithChar ('R')) return std::abs (t.substring (1).getFloatValue());
                    return t.getFloatValue();
                })));
    }

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "detune", 1 }, "Detune",
        juce::NormalisableRange<float> (0.0f, puppychow::maxDetuneCents, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("cents")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "swell", 1 }, "Swell",
        juce::NormalisableRange<float> (0.0f, puppychow::maxSwellMs, 1.0f, 0.5f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    // Off: Swell fades in only the voices. On: the dry guitar swells too,
    // like a volume pedal rolled up after every pick.
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "swell_dry", 1 }, "Swell Dry", false));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "tracking", 1 }, "Tracking",
        juce::StringArray { "Tight", "Balanced", "Clean" }, 2));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "output", 1 }, "Output",
        juce::NormalisableRange<float> (puppychow::minOutputDb, puppychow::maxOutputDb, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    return layout;
}

float PuppyChowProcessor::channelTargetGain (int channel) const noexcept
{
    const auto i = (size_t) channel;
    const float dbValue = levelDb[i]->load();
    const bool on = channelOn[i]->load() > 0.5f;
    return (! on || dbValue <= puppychow::minLevelDb + 0.05f) ? 0.0f : juce::Decibels::decibelsToGain (dbValue);
}

// Constant-power pan law, scaled so the centre position is unchanged (unity
// on both sides). Moving a channel to one side raises that side by 3 dB and
// fades the other out, so its loudness stays even as it moves.
std::pair<float, float> PuppyChowProcessor::channelPanGains (int channel, bool stereoOut) const noexcept
{
    if (! stereoOut)
        return { 1.0f, 1.0f };

    const float pan = juce::jlimit (-1.0f, 1.0f, channelPan[(size_t) channel]->load() / 100.0f);
    const float angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
    return { std::cos (angle) * juce::MathConstants<float>::sqrt2, std::sin (angle) * juce::MathConstants<float>::sqrt2 };
}

void PuppyChowProcessor::prepareToPlay (double sampleRate, int)
{
    sampleRateHz = sampleRate;

    engines[0].prepare (sampleRate, OctaveEngine::Tuning::tight());
    engines[1].prepare (sampleRate, OctaveEngine::Tuning::balanced());
    engines[2].prepare (sampleRate, OctaveEngine::Tuning::clean());
    activeEngine = -1;

    const bool stereoOut = getTotalNumOutputChannels() > 1;
    for (int i = 0; i < numChannels; ++i)
    {
        const float level = channelTargetGain (i);
        const auto [panL, panR] = channelPanGains (i, stereoOut);
        gainsL[(size_t) i].reset (sampleRate, 0.02); // 20 ms smoothing avoids zipper noise
        gainsR[(size_t) i].reset (sampleRate, 0.02);
        gainsL[(size_t) i].setCurrentAndTargetValue (level * panL);
        gainsR[(size_t) i].setCurrentAndTargetValue (level * panR);
    }

    outputGain.reset (sampleRate, 0.02);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputDb->load()));

    detuneMix.reset (sampleRate, 0.05);
    detuneMix.setCurrentAndTargetValue (juce::jlimit (0.0f, 1.0f, detuneCents->load() / 3.0f));

    fastEnv = slowEnv = 0.0f;
    swellRamp = swellGain = 1.0f;
    samplesSinceOnset = 0;

    // Gentle 20 Hz high-pass on the voices so two octaves below a low note
    // can't push speakers around with inaudible rumble.
    wetHighpassCoeff = (float) std::exp (-juce::MathConstants<double>::twoPi * 20.0 / sampleRate);
    wetHighpassStateL = wetHighpassStateR = 0.0f;
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
    const bool stereoOut = numOut > 1;
    bool active[OctaveEngine::numVoices] = {};
    for (int i = 0; i < numChannels; ++i)
    {
        const float level = channelTargetGain (i);
        const auto [panL, panR] = channelPanGains (i, stereoOut);
        auto& gL = gainsL[(size_t) i];
        auto& gR = gainsR[(size_t) i];
        gL.setTargetValue (level * panL);
        gR.setTargetValue (level * panR);
        if (channels[i].engineVoice >= 0)
            active[channels[i].engineVoice] = gL.getTargetValue() > 0.0f || gR.getTargetValue() > 0.0f
                                              || gL.isSmoothing() || gR.isSmoothing();
    }

    outputGain.setTargetValue (juce::Decibels::decibelsToGain (outputDb->load()));

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
    const bool swellTheDry = swellDry->load() >= 0.5f;
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

        // Bad input values (NaN or infinity) would get stuck in the filters.
        for (int i = 0; i < n; ++i)
            mono[i] = std::isfinite (mono[i]) ? mono[i] : 0.0f;

        engine.process (mono, voicePtrs, n);

        // Safety net: if the voices ever stop being real numbers, start the
        // engine and the high-pass afresh and drop this chunk of voices, so a
        // single bad value can never silence the plugin (and the whole track
        // after it) until it is bypassed.
        float check = 0.0f;
        for (int v = 0; v < numVoices; ++v)
            for (int i = 0; i < n; ++i)
                check += voices[v][i] * 0.0f;
        if (! std::isfinite (check) || ! std::isfinite (wetHighpassStateL) || ! std::isfinite (wetHighpassStateR))
        {
            engine.reset();
            for (int v = 0; v < numVoices; ++v)
                for (int i = 0; i < n; ++i)
                    voices[v][i] = 0.0f;
            wetHighpassStateL = wetHighpassStateR = 0.0f;
        }

        for (int i = 0; i < n; ++i)
        {
            // Mix the voices into left and right. The octave-up voices blend in
            // their detuned copies.
            const float det = detuneMix.getNextValue();
            float wetL = 0.0f, wetR = 0.0f;
            float dryL = 1.0f, dryR = 1.0f;

            for (int c = 0; c < numChannels; ++c)
            {
                const float gL = gainsL[(size_t) c].getNextValue();
                const float gR = gainsR[(size_t) c].getNextValue();
                const int v = channels[c].engineVoice;

                if (v < 0)
                {
                    dryL = gL; dryR = gR;
                    continue;
                }

                float x = voices[v][i];
                if (v == OctaveEngine::up1)
                    x = x * (1.0f - 0.3f * det) + voices[OctaveEngine::up1Detuned][i] * 0.7f * det;
                else if (v == OctaveEngine::up2)
                    x = x * (1.0f - 0.3f * det) + voices[OctaveEngine::up2Detuned][i] * 0.7f * det;

                wetL += gL * x;
                wetR += gR * x;
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
            wetL *= swellGain;
            wetR *= swellGain;
            if (swellTheDry)
            {
                dryL *= swellGain;
                dryR *= swellGain;
            }

            // One-pole high-pass: subtract a slow-moving average.
            wetHighpassStateL = wetL + wetHighpassCoeff * (wetHighpassStateL - wetL);
            wetHighpassStateR = wetR + wetHighpassCoeff * (wetHighpassStateR - wetR);
            wetL -= wetHighpassStateL;
            wetR -= wetHighpassStateR;

            // The dry guitar keeps its own left/right (if the input is stereo) and
            // the Dry pan acts like a balance control on it.
            const float out = outputGain.getNextValue();
            for (int ch = 0; ch < numOut; ++ch)
            {
                float* dst = buffer.getWritePointer (ch, start);
                const bool right = ch == 1;
                const float y = softLimit ((dst[i] * (right ? dryR : dryL) + (right ? wetR : wetL)) * out);
                dst[i] = std::isfinite (y) ? y : 0.0f;   // never pass a bad value down the chain
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
