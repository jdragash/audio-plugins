#pragma once

#include "PluginProcessor.h"

// Placeholder interface: a mixer strip per channel (on/off switch and a
// vertical level slider), plus Detune, Swell, Warmth and the Tracking mode.
// The real design comes later.
class PuppyChowEditor final : public juce::AudioProcessorEditor
{
public:
    explicit PuppyChowEditor (PuppyChowProcessor&);
    ~PuppyChowEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Strip
    {
        juce::Label title;
        juce::ToggleButton onButton;
        juce::TextButton soloButton;
        juce::Slider slider;
        juce::Slider pan;
        std::unique_ptr<SliderAttachment> panAttachment;
        std::unique_ptr<SliderAttachment> sliderAttachment;
        std::unique_ptr<ButtonAttachment> buttonAttachment;
        std::unique_ptr<ButtonAttachment> soloAttachment;
    };

    void setUpSlider (Strip&, const juce::String& title, const juce::String& paramId, const juce::String& suffix);

    PuppyChowProcessor& pluginProcessor;
    juce::LookAndFeel_V4 lookAndFeel;

    std::array<Strip, puppychow::numChannels> channelStrips;
    Strip detuneStrip, swellStrip, warmthStrip, outputStrip;

    juce::Label trackingLabel;
    juce::ComboBox trackingBox;
    std::unique_ptr<ComboBoxAttachment> trackingAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PuppyChowEditor)
};
