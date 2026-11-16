#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace ParamIDs
{
    static const juce::ParameterID dry { "dry", 1 };
}

PuppyChowProcessor::PuppyChowProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PuppyChow", createParameterLayout())
{
    dryLevelDb = parameters.getRawParameterValue (ParamIDs::dry.getParamID());
}

juce::AudioProcessorValueTreeState::ParameterLayout PuppyChowProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        ParamIDs::dry, "Dry",
        juce::NormalisableRange<float> (-60.0f, 6.0f, 0.1f, 2.0f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    return layout;
}

void PuppyChowProcessor::prepareToPlay (double sampleRate, int)
{
    dryGain.reset (sampleRate, 0.02); // 20 ms smoothing avoids zipper noise
    dryGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (dryLevelDb->load(), -60.0f));
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

    dryGain.setTargetValue (juce::Decibels::decibelsToGain (dryLevelDb->load(), -60.0f));

    if (! dryGain.isSmoothing())
    {
        buffer.applyGain (dryGain.getTargetValue());
        return;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        const auto g = dryGain.getNextValue();
        for (int ch = 0; ch < numOut; ++ch)
            buffer.getWritePointer (ch)[i] *= g;
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
