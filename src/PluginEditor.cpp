#include "PluginEditor.h"

namespace
{
    struct KnobInfo { const char* paramId; const char* title; };

    // Left to right: lowest voice to highest, with Dry in the middle.
    const KnobInfo knobInfo[] = {
        { "down2", "-2 Oct" },
        { "down1", "-1 Oct" },
        { "dry",   "Dry"    },
        { "up1",   "+1 Oct" },
        { "up2",   "+2 Oct" },
    };
}

PuppyChowEditor::PuppyChowEditor (PuppyChowProcessor& p)
    : AudioProcessorEditor (&p)
{
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& k = knobs[i];
        k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 20);
        addAndMakeVisible (k.slider);

        k.label.setText (knobInfo[i].title, juce::dontSendNotification);
        k.label.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (k.label);

        k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            p.getParameters(), knobInfo[i].paramId, k.slider);
    }

    setSize (560, 240);
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
    const int width = area.getWidth() / numKnobs;

    for (auto& k : knobs)
    {
        auto column = area.removeFromLeft (width);
        k.label.setBounds (column.removeFromTop (20));
        k.slider.setBounds (column.withSizeKeepingCentre (juce::jmin (width, 100), 130));
    }
}
