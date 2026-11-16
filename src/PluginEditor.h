#pragma once

#include "PluginProcessor.h"

// Placeholder interface. The real design comes later.
class PuppyChowEditor final : public juce::AudioProcessorEditor
{
public:
    explicit PuppyChowEditor (PuppyChowProcessor&);
    ~PuppyChowEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Slider drySlider;
    juce::Label dryLabel;
    juce::AudioProcessorValueTreeState::SliderAttachment dryAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PuppyChowEditor)
};
