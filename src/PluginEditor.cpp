#include "PluginEditor.h"

namespace
{
    const juce::Colour background { 0xff2b2118 };
    const juce::Colour cream { 0xfff3e6cf };
    const juce::Colour panel { 0xff3a2d21 };

    constexpr int stripWidth = 74;
    constexpr int headerHeight = 56;
    constexpr int margin = 16;
    constexpr int gapBeforeExtras = 24;
}

PuppyChowEditor::PuppyChowEditor (PuppyChowProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    auto& params = processor.getParameters();

    for (size_t i = 0; i < channelStrips.size(); ++i)
    {
        auto& strip = channelStrips[i];
        const auto& ch = puppychow::channels[i];
        setUpSlider (strip, ch.title, ch.id, " dB");

        strip.onButton.setButtonText ("On");
        strip.onButton.setColour (juce::ToggleButton::textColourId, cream);
        strip.onButton.setColour (juce::ToggleButton::tickColourId, cream);
        addAndMakeVisible (strip.onButton);
        strip.buttonAttachment = std::make_unique<ButtonAttachment> (params, juce::String (ch.id) + "_on", strip.onButton);

        // Dim a strip's slider while its channel is switched off.
        strip.onButton.onStateChange = [&strip] { strip.slider.setAlpha (strip.onButton.getToggleState() ? 1.0f : 0.35f); };
        strip.onButton.onStateChange();
    }

    setUpSlider (detuneStrip, "Detune", "detune", " ct");
    setUpSlider (swellStrip, "Swell", "swell", " ms");

    trackingLabel.setText ("Tracking", juce::dontSendNotification);
    trackingLabel.setColour (juce::Label::textColourId, cream);
    trackingLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (trackingLabel);

    trackingBox.addItemList ({ "Tight", "Balanced", "Clean" }, 1);
    addAndMakeVisible (trackingBox);
    trackingAttachment = std::make_unique<ComboBoxAttachment> (params, "tracking", trackingBox);

    setSize (2 * margin + stripWidth * (puppychow::numChannels + 2) + gapBeforeExtras, 400);
}

void PuppyChowEditor::setUpSlider (Strip& strip, const juce::String& title, const juce::String& paramId, const juce::String& suffix)
{
    strip.title.setText (title, juce::dontSendNotification);
    strip.title.setJustificationType (juce::Justification::centred);
    strip.title.setColour (juce::Label::textColourId, cream);
    addAndMakeVisible (strip.title);

    strip.slider.setSliderStyle (juce::Slider::LinearVertical);
    strip.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, stripWidth - 8, 20);
    strip.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    strip.slider.setColour (juce::Slider::textBoxTextColourId, cream);
    strip.slider.setColour (juce::Slider::thumbColourId, cream);
    addAndMakeVisible (strip.slider);
    strip.sliderAttachment = std::make_unique<SliderAttachment> (processor.getParameters(), paramId, strip.slider);
    strip.slider.setTextValueSuffix (suffix);
}

void PuppyChowEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);

    g.setColour (cream);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText ("Puppy Chow", juce::Rectangle<int> (margin, 0, 300, headerHeight), juce::Justification::centredLeft);

    // A slightly lighter panel behind the Dry strip so the eye finds the middle.
    const auto dry = juce::Rectangle<int> (margin + stripWidth * puppychow::dryChannel, headerHeight,
                                           stripWidth, getHeight() - headerHeight - margin);
    g.setColour (panel);
    g.fillRoundedRectangle (dry.toFloat().reduced (2.0f), 6.0f);
}

void PuppyChowEditor::resized()
{
    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (margin, 14);
    trackingBox.setBounds (header.removeFromRight (120));
    trackingLabel.setBounds (header.removeFromRight (80));

    auto area = getLocalBounds().withTrimmedTop (headerHeight).reduced (margin, 0).withTrimmedBottom (margin);

    auto layOut = [] (Strip& strip, juce::Rectangle<int> column, bool hasButton)
    {
        column = column.reduced (4, 6);
        strip.title.setBounds (column.removeFromTop (22));
        auto buttonRow = column.removeFromTop (26);
        if (hasButton)
            strip.onButton.setBounds (buttonRow.withSizeKeepingCentre (56, 24));
        strip.slider.setBounds (column);
    };

    for (auto& strip : channelStrips)
        layOut (strip, area.removeFromLeft (stripWidth), true);

    area.removeFromLeft (gapBeforeExtras);
    layOut (detuneStrip, area.removeFromLeft (stripWidth), false);
    layOut (swellStrip, area.removeFromLeft (stripWidth), false);
}
