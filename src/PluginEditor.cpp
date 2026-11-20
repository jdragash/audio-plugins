#include "PluginEditor.h"

#if JUCE_WEB_BROWSER

#include "BinaryData.h"

namespace
{
    constexpr int editorWidth = 720;
    constexpr int editorHeight = 480;
    const juce::Identifier darkModeId { "ui_dark" };

    // Short, friendly text for the page: "355 ms", "-3.5 dB", "50 %", "Clean".
    juce::String displayText (juce::RangedAudioParameter& param)
    {
        const auto label = param.getLabel();
        if (dynamic_cast<juce::AudioParameterFloat*> (&param) == nullptr || label.isEmpty())
            return param.getCurrentValueAsText();

        const auto real = param.convertFrom0to1 (param.getValue());
        if (label == "dB")
            return real <= -59.9f ? juce::String ("off") : juce::String (real, 1) + " dB";
        return juce::String (juce::roundToInt (real)) + " " + label;
    }

    juce::WebBrowserComponent::Options makeOptions (std::function<void (const juce::var&)> onUiEvent,
                                                    std::function<std::optional<juce::WebBrowserComponent::Resource> (const juce::String&)> provider)
    {
        return juce::WebBrowserComponent::Options{}
            .withBackend (juce::WebBrowserComponent::Options::Backend::defaultBackend)
            .withNativeIntegrationEnabled()
            .withKeepPageLoadedWhenBrowserIsHidden()
            .withEventListener ("ui", std::move (onUiEvent))
            .withResourceProvider (std::move (provider));
    }
}

PuppyChowEditor::PuppyChowEditor (PuppyChowProcessor& p)
    : AudioProcessorEditor (&p),
      pluginProcessor (p),
      browser (makeOptions ([this] (const juce::var& e) { handleUiEvent (e); },
                            [this] (const juce::String& url) { return getResource (url); }))
{
    for (auto* param : pluginProcessor.AudioProcessor::getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            params.add (ranged);

    sentValues.assign ((size_t) params.size(), -1.0f);

    addAndMakeVisible (browser);
    setSize (editorWidth, editorHeight);
    browser.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
    startTimerHz (30);
}

PuppyChowEditor::~PuppyChowEditor()
{
    stopTimer();
    for (auto& id : heldGestures)
        if (auto* param = pluginProcessor.getParameters().getParameter (id))
            param->endChangeGesture();
}

void PuppyChowEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff2a2622));
}

void PuppyChowEditor::resized()
{
    browser.setBounds (getLocalBounds());
}

std::optional<juce::WebBrowserComponent::Resource> PuppyChowEditor::getResource (const juce::String& url)
{
    const auto path = url.upToFirstOccurrenceOf ("?", false, false);
    if (path == "/" || path == "/index.html" || path.isEmpty())
    {
        const auto* data = reinterpret_cast<const std::byte*> (PuppyChowUI::index_html);
        return juce::WebBrowserComponent::Resource {
            std::vector<std::byte> (data, data + PuppyChowUI::index_htmlSize),
            "text/html" };
    }
    return std::nullopt;
}

void PuppyChowEditor::handleUiEvent (const juce::var& event)
{
    const auto type = event["type"].toString();

    if (type == "ready")
    {
        pageReady = true;
        sendState (true);
        return;
    }

    const auto id = event["id"].toString();

    if (type == "set" && id == darkModeId.toString())   // a look setting, saved with the session but not automatable
    {
        pluginProcessor.getParameters().state.setProperty (darkModeId, (double) event["v"] > 0.5, nullptr);
        return;
    }

    auto* param = pluginProcessor.getParameters().getParameter (id);
    if (param == nullptr)
        return;

    if (type == "begin")
    {
        if (heldGestures.insert (id).second)
            param->beginChangeGesture();
    }
    else if (type == "end")
    {
        if (heldGestures.erase (id) > 0)
            param->endChangeGesture();
    }
    else if (type == "set")
    {
        const auto value = juce::jlimit (0.0f, 1.0f, (float) (double) event["v"]);
        const bool oneShot = heldGestures.count (id) == 0;   // a click, not a drag
        if (oneShot)
            param->beginChangeGesture();
        param->setValueNotifyingHost (value);
        if (oneShot)
            param->endChangeGesture();
    }
}

void PuppyChowEditor::sendState (bool everything)
{
    if (! pageReady)
        return;

    auto* changes = new juce::DynamicObject();
    bool any = false;

    for (int i = 0; i < params.size(); ++i)
    {
        auto* param = params.getUnchecked (i);
        const auto value = param->getValue();
        if (! everything && std::abs (value - sentValues[(size_t) i]) < 1.0e-6f)
            continue;

        sentValues[(size_t) i] = value;
        juce::Array<juce::var> entry { value, displayText (*param) };
        changes->setProperty (param->getParameterID(), entry);
        any = true;
    }

    const bool dark = pluginProcessor.getParameters().state.getProperty (darkModeId, false);
    if (everything || dark != sentDark)
    {
        sentDark = dark;
        changes->setProperty (darkModeId, juce::Array<juce::var> { dark ? 1.0 : 0.0, juce::String() });
        any = true;
    }

    if (any)
        browser.emitEventIfBrowserIsVisible ("state", juce::var (changes));
    else
        delete changes;
}

void PuppyChowEditor::timerCallback()
{
    sendState (false);
}

#endif
