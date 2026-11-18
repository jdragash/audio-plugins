#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "Channels.h"
#include "dsp/OctaveEngine.h"

// Puppy Chow: a polyphonic octave pedal.
// The guitar passes straight through (Dry) and the engine adds up to seven
// extra voices: octaves down and up, plus a fourth down, a fifth up and an
// octave-plus-fifth up. Each channel has a level, a pan and an on/off switch.
class PuppyChowProcessor final : public juce::AudioProcessor
{
public:
    PuppyChowProcessor();
    ~PuppyChowProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameters; }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    float channelTargetGain (int channel) const noexcept;
    std::pair<float, float> channelPanGains (int channel, bool stereoOut) const noexcept;

    juce::AudioProcessorValueTreeState parameters;

    std::array<std::atomic<float>*, puppychow::numChannels> levelDb {};
    std::array<std::atomic<float>*, puppychow::numChannels> channelOn {};
    std::array<std::atomic<float>*, puppychow::numChannels> channelPan {};
    std::atomic<float>* detuneCents = nullptr;
    std::atomic<float>* swellMs = nullptr;
    std::atomic<float>* trackingMode = nullptr;
    std::atomic<float>* outputDb = nullptr;

    // Per channel: level x pan, for the left and right outputs.
    std::array<juce::SmoothedValue<float>, puppychow::numChannels> gainsL, gainsR;
    juce::SmoothedValue<float> detuneMix;
    juce::SmoothedValue<float> outputGain;

    // One engine per tracking mode, so switching never rebuilds anything on
    // the audio thread.
    static constexpr int numTrackingModes = 3;
    std::array<puppychow::OctaveEngine, numTrackingModes> engines;
    int activeEngine = -1;

    // Swell: fades the voices in after each new note is picked.
    double sampleRateHz = 48000.0;
    float fastEnv = 0.0f, slowEnv = 0.0f, swellRamp = 1.0f, swellGain = 1.0f;
    int samplesSinceOnset = 0;

    float wetHighpassStateL = 0.0f, wetHighpassStateR = 0.0f, wetHighpassCoeff = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PuppyChowProcessor)
};
