#include "PluginEditor.h"

PuppyChowEditor::PuppyChowEditor (PuppyChowProcessor& p)
    : AudioProcessorEditor (&p),
      dryAttachment (p.getParameters(), "dry", drySlider)
{
    drySlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    drySlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 20);
    addAndMakeVisible (drySlider);

    dryLabel.setText ("Dry", juce::dontSendNotification);
    dryLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (dryLabel);

    setSize (360, 240);
}

void PuppyChowEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff2b2118));
    g.setColour (juce::Colour (0xfff3e6cf));
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText ("Puppy Chow", getLocalBounds().removeFromTop (60), juce::Justification::centred);
}

void PuppyChowEditor::resized()
{
    auto area = getLocalBounds().reduced (20);
    area.removeFromTop (50);
    dryLabel.setBounds (area.removeFromTop (20));
    drySlider.setBounds (area.withSizeKeepingCentre (120, 130));
}
