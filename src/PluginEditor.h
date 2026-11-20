#pragma once

#include "PluginProcessor.h"

#if JUCE_WEB_BROWSER

// The Puppy Chow interface: a drawn page (src/ui/index.html) shown in a web
// view. The page sends "ui" events (ready / set / begin / end) and receives
// "state" events holding {paramId: [normalised value, display text]}.
class PuppyChowEditor final : public juce::AudioProcessorEditor,
                              private juce::Timer
{
public:
    explicit PuppyChowEditor (PuppyChowProcessor&);
    ~PuppyChowEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void handleUiEvent (const juce::var& event);
    void sendState (bool everything);
    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);

    PuppyChowProcessor& pluginProcessor;
    juce::Array<juce::RangedAudioParameter*> params;
    std::vector<float> sentValues;
    std::set<juce::String> heldGestures;
    bool pageReady = false;
    bool sentDark = false;

    juce::WebBrowserComponent browser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PuppyChowEditor)
};

#endif
