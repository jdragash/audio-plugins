// macOS only. Loads the installed Puppy Chow Audio Unit through Apple's AU API
// (the same path Logic uses), runs several instances side by side and checks
// that each one makes sound and never outputs bad values. Run it after
// installing the plugin to ~/Library/Audio/Plug-Ins/Components:
//
//     ./PuppyChowAUHostCheck
//
// It also reports how long each instance takes to start up and how much of
// one CPU core it uses.

#include <juce_audio_processors/juce_audio_processors.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

namespace
{
int failures = 0;

void check (bool ok, const juce::String& what)
{
    if (! ok) ++failures;
    std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
    std::fflush (stdout);
}

double nowSeconds()
{
    using namespace std::chrono;
    return duration<double> (steady_clock::now().time_since_epoch()).count();
}

struct Instance
{
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    juce::AudioBuffer<float> buffer;
    double sumSq = 0.0, peak = 0.0, busySeconds = 0.0;
    long count = 0;
    bool finite = true;
    long firstBad = -1;
};

// Plucked low chord, restarted every second.
float input (long pos, double sr)
{
    const double t = (double) pos / sr;
    const double env = std::exp (-2.0 * std::fmod (t, 1.0));
    return (float) (0.3 * env * (std::sin (juce::MathConstants<double>::twoPi * 82.41 * t)
                                 + 0.5 * std::sin (juce::MathConstants<double>::twoPi * 123.47 * t)
                                 + 0.3 * std::sin (juce::MathConstants<double>::twoPi * 164.81 * t)));
}

// Through the AU API parameters are known by name, not by their plugin ids.
void setParam (juce::AudioPluginInstance& p, const juce::String& name, float normalised)
{
    for (auto* param : p.getParameters())
        if (param->getName (100) == name)
            param->setValueNotifyingHost (normalised);
}

void run (juce::AudioPluginFormat& format, const juce::PluginDescription& desc,
          int numInstances, int inChannels, double sr, int blockSize, bool allVoices,
          bool copyState, const juce::String& label)
{
    std::vector<Instance> instances ((size_t) numInstances);

    for (int k = 0; k < numInstances; ++k)
    {
        juce::String error;
        auto& inst = instances[(size_t) k];
        inst.plugin = format.createInstanceFromDescription (desc, sr, blockSize, error);
        if (inst.plugin == nullptr)
        {
            check (false, label + ": create instance " + juce::String (k + 1) + ": " + error);
            return;
        }

        auto layout = inst.plugin->getBusesLayout();
        layout.inputBuses.getReference (0) = inChannels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
        layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
        check (inst.plugin->setBusesLayout (layout), label + ": instance " + juce::String (k + 1) + " accepts "
                                                     + juce::String (inChannels) + " in / 2 out");

        if (allVoices)
        {
            for (auto id : { "-2 Octaves On", "-4th On", "+5th On", "+1 Octave +5th On", "+2 Octaves On" })
                setParam (*inst.plugin, id, 1.0f);
            setParam (*inst.plugin, "Detune", 0.4f);
            setParam (*inst.plugin, "Swell", 0.2f);
        }

        if (copyState && k > 0)
        {
            juce::MemoryBlock state;
            instances[0].plugin->getStateInformation (state);
            inst.plugin->setStateInformation (state.getData(), (int) state.getSize());
        }

        const double t0 = nowSeconds();
        inst.plugin->prepareToPlay (sr, blockSize);
        std::printf ("      %s: instance %d prepare took %.0f ms\n", label.toRawUTF8(), k + 1, 1000.0 * (nowSeconds() - t0));
        inst.buffer.setSize (juce::jmax (inChannels, 2), blockSize);
    }

    // Process on a separate thread, like an audio render thread, with the
    // instances interleaved block by block as a DAW does.
    const double seconds = 6.0;
    const long total = (long) (seconds * sr);
    juce::MidiBuffer midi;

    std::thread renderThread ([&]
    {
        for (long pos = 0; pos < total; pos += blockSize)
        {
            for (auto& inst : instances)
            {
                auto& b = inst.buffer;
                for (int ch = 0; ch < b.getNumChannels(); ++ch)
                    for (int i = 0; i < blockSize; ++i)
                        b.setSample (ch, i, ch < inChannels ? input (pos + i, sr) : 0.0f);

                const double t0 = nowSeconds();
                inst.plugin->processBlock (b, midi);
                inst.busySeconds += nowSeconds() - t0;

                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < blockSize; ++i)
                    {
                        const float s = b.getSample (ch, i);
                        if (! std::isfinite (s))
                        {
                            if (inst.finite) inst.firstBad = pos + i;
                            inst.finite = false;
                            continue;
                        }
                        inst.peak = std::max (inst.peak, (double) std::abs (s));
                        if (pos > (long) sr / 2) { inst.sumSq += (double) s * s; ++inst.count; }
                    }
            }
        }
    });
    renderThread.join();

    for (int k = 0; k < numInstances; ++k)
    {
        auto& inst = instances[(size_t) k];
        const double rms = std::sqrt (inst.sumSq / (double) juce::jmax (1L, inst.count));
        const juce::String who = label + ": instance " + juce::String (k + 1);
        check (inst.finite, who + " output always finite" + (inst.finite ? juce::String() : " (first bad sample " + juce::String (inst.firstBad) + ")"));
        check (rms > 0.01, who + " makes sound (rms " + juce::String (rms, 4) + ")");
        check (inst.peak <= 1.0, who + " peak " + juce::String (inst.peak, 3));
        std::printf ("      %s uses %.1f %% of one core\n", who.toRawUTF8(), 100.0 * inst.busySeconds / seconds);
        inst.plugin->releaseResources();
    }
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::AudioUnitPluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile (found, "AudioUnit:Effects/aufx,Pchw,Jdrg");
    if (found.isEmpty())
    {
        std::printf ("FAIL  Puppy Chow Audio Unit not found. Install it to ~/Library/Audio/Plug-Ins/Components first.\n");
        return 1;
    }
    const auto desc = *found[0];
    std::printf ("Found %s %s\n", desc.name.toRawUTF8(), desc.version.toRawUTF8());

    run (format, desc, 1, 1, 48000.0, 512, false, false, "1 instance, mono in");
    run (format, desc, 2, 1, 48000.0, 512, false, true,  "2 instances, same settings, mono in");
    run (format, desc, 2, 2, 48000.0, 512, false, true,  "2 instances, same settings, stereo in");
    run (format, desc, 2, 1, 44100.0, 128, false, true,  "2 instances, 44.1 kHz, 128 buffer");
    run (format, desc, 2, 1, 48000.0, 32,  false, true,  "2 instances, 32 buffer");
    run (format, desc, 4, 1, 48000.0, 256, true,  false, "4 instances, all voices");

    std::printf (failures == 0 ? "\nAll AU host checks passed.\n" : "\n%d AU host check(s) FAILED.\n", failures);
    return failures == 0 ? 0 : 1;
}
