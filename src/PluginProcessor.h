#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "dsp/OctaveEngine.h"

// Puppy Chow: a polyphonic octave pedal.
// The guitar passes straight through (Dry) and the octave engine adds up to
// four extra voices: one and two octaves down, one and two octaves up.
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
    juce::AudioProcessorValueTreeState parameters;

    // Index 0 is Dry, then the four engine voices in engine order.
    static constexpr int numLevels = 1 + puppychow::OctaveEngine::numVoices;
    std::array<std::atomic<float>*, numLevels> levelDb {};
    std::array<juce::SmoothedValue<float>, numLevels> gains;

    puppychow::OctaveEngine engine;
    float wetHighpassState = 0.0f, wetHighpassCoeff = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PuppyChowProcessor)
};
