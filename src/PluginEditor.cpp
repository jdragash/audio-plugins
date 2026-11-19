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
    : AudioProcessorEditor (&p), pluginProcessor (p)
{
    auto& params = pluginProcessor.getParameters();

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

        // Solo: hear only this channel (and any other soloed ones).
        strip.soloButton.setButtonText ("S");
        strip.soloButton.setTooltip ("Solo: hear only this channel");
        strip.soloButton.setClickingTogglesState (true);
        strip.soloButton.setColour (juce::TextButton::buttonColourId, panel);
        strip.soloButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe8b04a));
        strip.soloButton.setColour (juce::TextButton::textColourOffId, cream);
        strip.soloButton.setColour (juce::TextButton::textColourOnId, background);
        addAndMakeVisible (strip.soloButton);
        strip.soloAttachment = std::make_unique<ButtonAttachment> (params, juce::String (ch.id) + "_solo", strip.soloButton);

        // Small pan knob. Double-click returns it to the centre.
        strip.pan.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        strip.pan.setTextBoxStyle (juce::Slider::TextBoxBelow, false, stripWidth - 12, 16);
        strip.pan.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        strip.pan.setColour (juce::Slider::textBoxTextColourId, cream);
        strip.pan.setColour (juce::Slider::thumbColourId, cream);
        strip.pan.setPopupDisplayEnabled (false, false, nullptr);
        addAndMakeVisible (strip.pan);
        strip.panAttachment = std::make_unique<SliderAttachment> (params, juce::String (ch.id) + "_pan", strip.pan);
        strip.pan.setDoubleClickReturnValue (true, 0.0);

        // Dim a strip's controls while its channel is switched off.
        strip.onButton.onStateChange = [&strip]
        {
            const float alpha = strip.onButton.getToggleState() ? 1.0f : 0.35f;
            strip.slider.setAlpha (alpha);
            strip.pan.setAlpha (alpha);
        };
        strip.onButton.onStateChange();
    }

    setUpSlider (detuneStrip, "Detune", "detune", " ct");
    setUpSlider (swellStrip, "Swell", "swell", " ms");

    // Swell's switch decides whether the dry guitar swells too.
    swellStrip.onButton.setButtonText ("Dry");
    swellStrip.onButton.setTooltip ("Swell the dry guitar too, not just the voices");
    swellStrip.onButton.setColour (juce::ToggleButton::textColourId, cream);
    swellStrip.onButton.setColour (juce::ToggleButton::tickColourId, cream);
    addAndMakeVisible (swellStrip.onButton);
    swellStrip.buttonAttachment = std::make_unique<ButtonAttachment> (params, "swell_dry", swellStrip.onButton);
    setUpSlider (warmthStrip, "Warmth", "warmth", " %");
    setUpSlider (outputStrip, "Output", "output", " dB");

    trackingLabel.setText ("Tracking", juce::dontSendNotification);
    trackingLabel.setColour (juce::Label::textColourId, cream);
    trackingLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (trackingLabel);

    trackingBox.addItemList ({ "Tight", "Balanced", "Clean" }, 1);
    addAndMakeVisible (trackingBox);
    trackingAttachment = std::make_unique<ComboBoxAttachment> (params, "tracking", trackingBox);

    setSize (2 * margin + stripWidth * (puppychow::numChannels + 4) + gapBeforeExtras, 470);
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
    strip.sliderAttachment = std::make_unique<SliderAttachment> (pluginProcessor.getParameters(), paramId, strip.slider);
    strip.slider.setTextValueSuffix (suffix);
}

void PuppyChowEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);

    g.setColour (cream);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText ("Puppy Chow", juce::Rectangle<int> (margin, 0, 300, headerHeight), juce::Justification::centredLeft);

    g.setColour (cream.withAlpha (0.5f));
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("v" JucePlugin_VersionString, juce::Rectangle<int> (margin + 150, 6, 100, headerHeight), juce::Justification::centredLeft);

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

    auto layOut = [] (Strip& strip, juce::Rectangle<int> column, bool hasPan)
    {
        column = column.reduced (4, 6);
        strip.title.setBounds (column.removeFromTop (22));
        auto buttonRow = column.removeFromTop (26);
        auto panRow = column.removeFromTop (70);
        if (strip.soloButton.isVisible())
        {
            strip.soloButton.setBounds (buttonRow.removeFromRight (24).withSizeKeepingCentre (22, 20));
            strip.onButton.setBounds (buttonRow.withSizeKeepingCentre (44, 24));
        }
        else if (strip.onButton.isVisible())
            strip.onButton.setBounds (buttonRow.withSizeKeepingCentre (56, 24));
        if (hasPan)
            strip.pan.setBounds (panRow.withSizeKeepingCentre (stripWidth - 8, 66));
        strip.slider.setBounds (column);
    };

    for (auto& strip : channelStrips)
        layOut (strip, area.removeFromLeft (stripWidth), true);

    area.removeFromLeft (gapBeforeExtras);
    layOut (detuneStrip, area.removeFromLeft (stripWidth), false);
    layOut (swellStrip, area.removeFromLeft (stripWidth), false);
    layOut (warmthStrip, area.removeFromLeft (stripWidth), false);
    layOut (outputStrip, area.removeFromLeft (stripWidth), false);
}
