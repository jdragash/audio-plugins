#pragma once

#include "PluginProcessor.h"

// Placeholder interface: one knob per voice. The real design comes later.
class PuppyChowEditor final : public juce::AudioProcessorEditor
{
public:
    explicit PuppyChowEditor (PuppyChowProcessor&);
    ~PuppyChowEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    static constexpr int numKnobs = 5;
    std::array<Knob, numKnobs> knobs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PuppyChowEditor)
};
