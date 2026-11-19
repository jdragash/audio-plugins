// Host-level tests: runs the real plugin processor the way a DAW does, with
// several instances at once, different block sizes, state copied between
// instances, and preparing/processing in awkward orders. Checks every output
// sample is finite and that the plugin is not silent.

#include "PluginProcessor.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

namespace
{
int failures = 0;

void check (bool ok, const juce::String& what)
{
    if (! ok) ++failures;
    std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
}

struct Result { bool finite = true; double peak = 0.0; double rms = 0.0; };

// Plays a plucked low chord through the processor in blocks.
Result play (PuppyChowProcessor& p, double sr, int blockSize, double seconds, int channels = 2, double phaseOffset = 0.0)
{
    juce::AudioBuffer<float> buffer (channels, blockSize);
    juce::MidiBuffer midi;
    Result r;
    double sumSq = 0.0; long count = 0;
    const long total = (long) (seconds * sr);

    for (long pos = 0; pos < total; pos += blockSize)
    {
        const int n = (int) std::min<long> (blockSize, total - pos);
        buffer.setSize (channels, n, false, false, true);
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) (pos + i) / sr;
            const double env = std::exp (-2.0 * std::fmod (t, 1.0));
            const double x = 0.3 * env * (std::sin (2.0 * juce::MathConstants<double>::pi * 82.41 * t + phaseOffset)
                                         + 0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 123.47 * t));
            for (int ch = 0; ch < channels; ++ch)
                buffer.setSample (ch, i, (float) x);
        }

        p.processBlock (buffer, midi);

        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < n; ++i)
            {
                const float s = buffer.getSample (ch, i);
                if (! std::isfinite (s)) r.finite = false;
                r.peak = std::max (r.peak, (double) std::abs (s));
                sumSq += (double) s * s; ++count;
            }
    }
    r.rms = std::sqrt (sumSq / std::max (1L, count));
    return r;
}

void setParam (PuppyChowProcessor& p, const juce::String& id, float value)
{
    if (auto* param = p.getParameters().getParameter (id))
        param->setValueNotifyingHost (param->convertTo0to1 (value));
}

std::unique_ptr<PuppyChowProcessor> makeInstance (double sr, int block, int channels = 2)
{
    auto p = std::make_unique<PuppyChowProcessor>();
    auto layout = p->getBusesLayout();
    layout.inputBuses.getReference (0) = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    layout.outputBuses.getReference (0) = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    p->setBusesLayout (layout);
    p->setRateAndBufferSizeDetails (sr, block);
    p->prepareToPlay (sr, block);
    return p;
}

void expectGood (const Result& r, const juce::String& what)
{
    check (r.finite, what + ": output is always a real number (no NaN/inf)");
    check (r.rms > 0.01, what + ": output is not silent (rms " + juce::String (r.rms, 4) + ")");
    check (r.peak <= 1.0001, what + ": output stays within full scale (peak " + juce::String (r.peak, 3) + ")");
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;

    // 1. One instance, default settings.
    {
        auto a = makeInstance (48000.0, 256);
        expectGood (play (*a, 48000.0, 256, 3.0), "single instance");
    }

    // 2. Two instances side by side, same settings, interleaved block by block
    //    (what Logic does with two tracks).
    {
        auto a = makeInstance (48000.0, 256);
        auto b = makeInstance (48000.0, 256);
        juce::MemoryBlock state;
        a->getStateInformation (state);
        b->setStateInformation (state.getData(), (int) state.getSize());

        Result ra, rb;
        for (int k = 0; k < 20; ++k)
        {
            auto x = play (*a, 48000.0, 256, 0.15);
            auto y = play (*b, 48000.0, 256, 0.15, 2, 0.3);
            ra.finite &= x.finite; rb.finite &= y.finite;
            ra.peak = std::max (ra.peak, x.peak); rb.peak = std::max (rb.peak, y.peak);
            ra.rms = std::max (ra.rms, x.rms); rb.rms = std::max (rb.rms, y.rms);
        }
        expectGood (ra, "two instances, first");
        expectGood (rb, "two instances, second (state copied from first)");
    }

    // 3. Every voice on, detune, swell, all tracking modes, several instances.
    for (int mode = 0; mode < 3; ++mode)
    {
        std::vector<std::unique_ptr<PuppyChowProcessor>> inst;
        for (int i = 0; i < 4; ++i)
        {
            inst.push_back (makeInstance (44100.0, 512));
            auto& p = *inst.back();
            for (const auto& ch : puppychow::channels)
            {
                setParam (p, juce::String (ch.id) + "_on", 1.0f);
                setParam (p, juce::String (ch.id) + "_pan", (float) (i * 30 - 45));
            }
            setParam (p, "detune", 12.0f);
            setParam (p, "swell", 200.0f);
            setParam (p, "tracking", (float) mode);
        }
        for (size_t i = 0; i < inst.size(); ++i)
            expectGood (play (*inst[i], 44100.0, 512, 2.0), "four instances, all voices, tracking mode " + juce::String (mode)
                                                           + ", instance " + juce::String ((int) i + 1));
    }

    // 4. Awkward host behaviour: odd and changing block sizes, blocks bigger
    //    than announced, prepare called twice, sample rate change, mono.
    {
        auto a = makeInstance (48000.0, 128);
        expectGood (play (*a, 48000.0, 1, 0.5), "block size 1");
        expectGood (play (*a, 48000.0, 1000, 2.0), "block size larger than prepared");
        a->prepareToPlay (96000.0, 64);
        expectGood (play (*a, 96000.0, 64, 2.0), "re-prepared at 96 kHz");
        a->prepareToPlay (44100.0, 4096);
        expectGood (play (*a, 44100.0, 4096, 2.0), "re-prepared at 44.1 kHz, 4096 block");

        auto m = makeInstance (48000.0, 256, 1);
        expectGood (play (*m, 48000.0, 256, 2.0, 1), "mono in, mono out");
    }

    // 5. Processing with a zero-length block must not break anything.
    {
        auto a = makeInstance (48000.0, 256);
        juce::AudioBuffer<float> empty (2, 0);
        juce::MidiBuffer midi;
        a->processBlock (empty, midi);
        expectGood (play (*a, 48000.0, 256, 2.0), "after an empty block");
    }

    std::printf (failures == 0 ? "\nAll host tests passed.\n" : "\n%d host checks FAILED.\n", failures);
    return failures == 0 ? 0 : 1;
}
