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

// Very quiet hiss (what a monitored guitar input sends between notes), then a
// plucked chord. Returns the result of the part after the pluck, and whether
// anything at all was not a real number.
Result hissThenPluck (PuppyChowProcessor& p, double sr, int blockSize, float hissLevel)
{
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    juce::Random rng (1234);
    Result r;
    double sumSq = 0.0; long count = 0;
    const long pluckAt = (long) (2.0 * sr), total = (long) (4.0 * sr);

    for (long pos = 0; pos < total; pos += blockSize)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            const long n = pos + i;
            double x = hissLevel * (2.0 * rng.nextDouble() - 1.0);
            if (n >= pluckAt)
            {
                const double t = (double) (n - pluckAt) / sr;
                x += 0.3 * std::exp (-2.0 * t) * std::sin (2.0 * juce::MathConstants<double>::pi * 82.41 * t);
            }
            for (int ch = 0; ch < 2; ++ch)
                buffer.setSample (ch, i, (float) x);
        }

        p.processBlock (buffer, midi);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < blockSize; ++i)
            {
                const float s = buffer.getSample (ch, i);
                if (! std::isfinite (s)) { r.finite = false; continue; }
                r.peak = std::max (r.peak, (double) std::abs (s));
                if (pos + i >= pluckAt) { sumSq += (double) s * s; ++count; }
            }
    }
    r.rms = std::sqrt (sumSq / std::max (1L, count));
    return r;
}

std::unique_ptr<PuppyChowProcessor> makeInstance (double sr, int block, int channels = 2);

// Stress test: random signals of every kind (near-silent hiss at many levels,
// digital silence, hot clipped noise, full-scale squares, DC, single spikes,
// tiny denormal values), random block sizes, and random parameter moves, all
// at once. Every output sample must be a real number within full scale, and
// a plucked note at the end must still come out (the plugin recovered).
struct StressResult { long badSamples = 0; double peak = 0.0; double endRms = 0.0; };

StressResult stress (double sr, int channels, double seconds, juce::uint32 seed)
{
    juce::Random rng ((juce::int64) seed);
    auto p = makeInstance (sr, 4096, channels);
    juce::AudioBuffer<float> buffer (channels, 4096);
    juce::MidiBuffer midi;
    StressResult r;

    const long total = (long) (seconds * sr), pluckAt = total - (long) (1.5 * sr);
    long pos = 0;
    int kind = 0; long kindEnds = 0;
    double level = 0.0, phase = 0.0, freq = 100.0;
    double endSumSq = 0.0; long endCount = 0;
    bool defaultsRestored = false;

    while (pos < total)
    {
        // A new random signal every 50 ms to 1 s.
        if (pos >= kindEnds)
        {
            kind = rng.nextInt (7);
            kindEnds = pos + (long) ((0.05 + rng.nextDouble()) * sr);
            level = std::pow (10.0, -10.0 * rng.nextDouble());      // 1 down to 1e-10
            freq = 40.0 + 2000.0 * rng.nextDouble();
        }

        // Back to default settings for the final note, so it must be heard.
        if (pos >= pluckAt && ! defaultsRestored)
        {
            for (auto* param : p->AudioProcessor::getParameters())
                param->setValueNotifyingHost (param->getDefaultValue());
            defaultsRestored = true;
        }

        // Random parameter moves, as automation would.
        if (pos < pluckAt && rng.nextInt (8) == 0)
        {
            auto& params = p->AudioProcessor::getParameters();
            auto* param = params[rng.nextInt (params.size())];
            param->setValueNotifyingHost (rng.nextFloat());
        }

        const int n = (int) std::min<long> (1 + rng.nextInt (rng.nextBool() ? 64 : 4096), total - pos);
        buffer.setSize (channels, n, false, false, true);
        for (int i = 0; i < n; ++i)
        {
            const long t = pos + i;
            double x = 0.0;
            if (t >= pluckAt)
            {
                const double u = (double) (t - pluckAt) / sr;
                x = 0.3 * std::exp (-2.0 * u) * std::sin (2.0 * juce::MathConstants<double>::pi * 110.0 * u);
            }
            else
            {
                phase += freq / sr;
                switch (kind)
                {
                    case 0: x = level * (2.0 * rng.nextDouble() - 1.0); break;                       // hiss at any level
                    case 1: x = 0.0; break;                                                          // digital silence
                    case 2: x = juce::jlimit (-1.0, 1.0, 4.0 * (2.0 * rng.nextDouble() - 1.0)); break; // hot clipped noise
                    case 3: x = std::fmod (phase, 1.0) < 0.5 ? 1.0 : -1.0; break;                    // full-scale square
                    case 4: x = 0.5 * level + 0.2; break;                                            // DC offset
                    case 5: x = rng.nextInt (2000) == 0 ? 1.0 : 0.0; break;                          // lone spikes
                    default: x = (rng.nextBool() ? 1.0 : -1.0) * 1.0e-39 * rng.nextDouble(); break;  // denormals
                }
            }
            for (int ch = 0; ch < channels; ++ch)
                buffer.setSample (ch, i, (float) x);
        }

        p->processBlock (buffer, midi);

        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < n; ++i)
            {
                const float y = buffer.getSample (ch, i);
                if (! std::isfinite (y)) { ++r.badSamples; continue; }
                r.peak = std::max (r.peak, (double) std::abs (y));
                if (pos + i >= pluckAt) { endSumSq += (double) y * y; ++endCount; }
            }
        pos += n;
    }
    r.endRms = std::sqrt (endSumSq / std::max (1L, endCount));
    return r;
}

// Power of one frequency in a signal (Goertzel), as a fraction of a full-scale sine's.
double tonePower (const std::vector<float>& x, double freq, double sr)
{
    const double w = 2.0 * juce::MathConstants<double>::pi * freq / sr, k = 2.0 * std::cos (w);
    double s1 = 0.0, s2 = 0.0;
    for (float v : x) { const double s0 = v + k * s1 - s2; s2 = s1; s1 = s0; }
    const double p = s1 * s1 + s2 * s2 - k * s1 * s2;
    return 4.0 * p / ((double) x.size() * (double) x.size());
}

double meanPower (const std::vector<float>& x, size_t from, size_t to)
{
    double sum = 0.0;
    for (size_t i = from; i < to; ++i) sum += (double) x[i] * x[i];
    return sum / (double) std::max<size_t> (1, to - from);
}

// Plays a steady sine (after a short silence) through only the given channel
// and returns the left output.
std::vector<float> playSine (PuppyChowProcessor& p, double sr, double freq, double seconds, long silence)
{
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    std::vector<float> out;
    const long total = (long) (seconds * sr);
    for (long pos = 0; pos < total; pos += 256)
    {
        for (int i = 0; i < 256; ++i)
        {
            const long n = pos + i;
            const float x = n >= silence ? 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * freq * (double) n / sr) : 0.0f;
            buffer.setSample (0, i, x); buffer.setSample (1, i, x);
        }
        p.processBlock (buffer, midi);
        for (int i = 0; i < 256; ++i) out.push_back (buffer.getSample (0, i));
    }
    return out;
}

void soloChannel (PuppyChowProcessor& p, const juce::String& id);

void setParam (PuppyChowProcessor& p, const juce::String& id, float value)
{
    if (auto* param = p.getParameters().getParameter (id))
        param->setValueNotifyingHost (param->convertTo0to1 (value));
}

std::unique_ptr<PuppyChowProcessor> makeInstance (double sr, int block, int channels)
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

void soloChannel (PuppyChowProcessor& p, const juce::String& id)
{
    for (const auto& ch : puppychow::channels)
    {
        setParam (p, juce::String (ch.id) + "_on", juce::String (ch.id) == id ? 1.0f : 0.0f);
        setParam (p, ch.id, 0.0f);
    }
}

void expectGood (const Result& r, const juce::String& what)
{
    check (r.finite, what + ": output is always a real number (no NaN/inf)");
    check (r.rms > 0.01, what + ": output is not silent (rms " + juce::String (r.rms, 4) + ")");
    check (r.peak <= 1.0001, what + ": output stays within full scale (peak " + juce::String (r.peak, 3) + ")");
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juce;

    // --stress <minutes>: run only the stress test, for that long in total.
    double stressMinutes = 0.0;
    for (int i = 1; i + 1 < argc; ++i)
        if (juce::String (argv[i]) == "--stress")
            stressMinutes = juce::String (argv[i + 1]).getDoubleValue();

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

    // 6. Very quiet input, then a note. With denormals flushed to zero (as in
    //    every DAW) hiss around -120 dBFS used to turn the output into NaN,
    //    which silenced the whole track, and Logic's output with it, until the
    //    plugin was bypassed.
    for (float hiss : { 1.0e-5f, 1.0e-6f, 1.0e-7f, 1.0e-8f, 0.0f })
        for (int mode = 0; mode < 3; ++mode)
        {
            auto a = makeInstance (48000.0, 128);
            for (const auto& ch : puppychow::channels)
                setParam (*a, juce::String (ch.id) + "_on", 1.0f);
            setParam (*a, "detune", 12.0f);
            setParam (*a, "tracking", (float) mode);
            expectGood (hissThenPluck (*a, 48000.0, 128, hiss),
                        "hiss at " + juce::String (hiss) + " then a note, tracking mode " + juce::String (mode));
        }

    // 7. Stress test (see stress()). Short in the normal run; --stress runs it
    //    for as many minutes as asked.
    {
        const bool longRun = stressMinutes > 0.0;
        const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0 };
        const double runSeconds = longRun ? 60.0 : 8.0;
        const int runs = longRun ? juce::jmax (1, (int) (stressMinutes * 60.0 / runSeconds + 0.5)) : 4;
        for (int k = 0; k < runs; ++k)
        {
            const double sr = rates[k % 4];
            const int channels = (k / 4) % 2 == 0 ? 2 : 1;
            const auto r = stress (sr, channels, runSeconds, 1000u + (juce::uint32) k);
            const juce::String what = "stress run " + juce::String (k + 1) + " (" + juce::String (sr / 1000.0) + " kHz, "
                                    + (channels == 2 ? "stereo" : "mono") + ", " + juce::String (runSeconds, 0) + " s)";
            check (r.badSamples == 0, what + ": no NaN/inf (" + juce::String (r.badSamples) + " bad samples)");
            check (r.peak <= 1.0001, what + ": within full scale (peak " + juce::String (r.peak, 3) + ")");
            check (r.endRms > 0.005, what + ": a note afterwards still comes out (rms " + juce::String (r.endRms, 4) + ")");
        }
    }

    // 8. Swell Dry: with it on, the dry guitar fades in after a pick too;
    //    with it off, the dry guitar is at full level straight away.
    for (bool swellDryOn : { false, true })
    {
        auto a = makeInstance (48000.0, 256);
        for (const auto& ch : puppychow::channels)
            setParam (*a, juce::String (ch.id) + "_on", juce::String (ch.id) == "dry" ? 1.0f : 0.0f);
        setParam (*a, "swell", 500.0f);
        setParam (*a, "swell_dry", swellDryOn ? 1.0f : 0.0f);

        // 0.5 s of silence, then a steady note. Compare 10 to 40 ms in with 0.6 s later.
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        double early = 0.0, late = 0.0;
        const long noteAt = 24000;
        for (long pos = 0; pos < 96000; pos += 256)
        {
            for (int i = 0; i < 256; ++i)
            {
                const long n = pos + i;
                const float x = n >= noteAt ? 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 110.0 * (double) n / 48000.0) : 0.0f;
                buffer.setSample (0, i, x); buffer.setSample (1, i, x);
            }
            a->processBlock (buffer, midi);
            for (int i = 0; i < 256; ++i)
            {
                const long n = pos + i - noteAt;
                const double y = buffer.getSample (0, i);
                if (n >= 480 && n < 1920) early += y * y;   // 10 to 40 ms after the pick
                if (n >= 28800 && n < 30240) late += y * y;
            }
        }
        const double ratioDb = 10.0 * std::log10 ((early + 1.0e-20) / (late + 1.0e-20));
        if (swellDryOn)
            check (ratioDb < -12.0, "Swell Dry on: dry starts quiet after a pick (" + juce::String (ratioDb, 1) + " dB vs later)");
        else
            check (std::abs (ratioDb) < 1.0, "Swell Dry off: dry is at full level right away (" + juce::String (ratioDb, 1) + " dB vs later)");
    }

    // 9. Overtones: the 7th, 9th, 11th and 13th harmonics of the note, and
    //    they bloom in rather than arriving with the pick.
    {
        const double sr = 48000.0, f = 110.0;
        auto a = makeInstance (sr, 256);
        soloChannel (*a, "overtones");
        const long silence = 12000;
        const auto out = playSine (*a, sr, f, 2.0, silence);
        std::vector<float> steady (out.begin() + silence + 48000, out.begin() + silence + 76800);
        double partials = 0.0;
        for (int h : { 7, 9, 11, 13 }) partials += tonePower (steady, h * f, sr);
        const double total = meanPower (steady, 0, steady.size()) * 2.0;   // a sine's power is half its peak squared
        check (std::isfinite (total) && total > 1.0e-4, "Overtones make sound (" + juce::String (10.0 * std::log10 (total + 1e-20), 1) + " dB)");
        check (partials > 0.7 * total, "Overtones are mostly harmonics 7, 9, 11 and 13 ("
                                       + juce::String (100.0 * partials / (total + 1e-20), 0) + " %)");
        const double early = meanPower (out, (size_t) silence + 480, (size_t) silence + 1920);
        const double later = meanPower (out, (size_t) silence + 48000, (size_t) silence + 76800);
        const double bloomDb = 10.0 * std::log10 ((early + 1e-20) / (later + 1e-20));
        check (bloomDb < -6.0, "Overtones bloom in after the pick (" + juce::String (bloomDb, 1) + " dB at 10-40 ms vs later)");
    }

    // 10. Warmth: adds harmonics to the octave up, keeps its level roughly the
    //     same, and leaves the octave down alone.
    {
        const double sr = 48000.0, f = 220.0;
        double level[2] {}, distortion[2] {};
        for (int w = 0; w < 2; ++w)
        {
            auto a = makeInstance (sr, 256);
            soloChannel (*a, "up1");
            setParam (*a, "warmth", w == 0 ? 0.0f : 100.0f);
            const auto out = playSine (*a, sr, f, 1.5, 0);
            std::vector<float> steady (out.begin() + 36000, out.end());
            level[w] = meanPower (steady, 0, steady.size()) * 2.0;
            const double fundamental = tonePower (steady, 2.0 * f, sr);
            distortion[w] = (level[w] - fundamental) / (level[w] + 1e-20);
        }
        const double levelDb = 10.0 * std::log10 ((level[1] + 1e-20) / (level[0] + 1e-20));
        check (std::abs (levelDb) < 3.0, "Warmth keeps the level within 3 dB (" + juce::String (levelDb, 1) + " dB)");
        check (distortion[1] > 2.0 * distortion[0] + 0.001, "Warmth adds harmonics to +1 octave ("
                                                           + juce::String (100.0 * distortion[0], 2) + " % -> " + juce::String (100.0 * distortion[1], 2) + " %)");

        auto down = makeInstance (sr, 256);
        soloChannel (*down, "down1");
        auto plain = playSine (*down, sr, f, 1.0, 0);
        auto down2 = makeInstance (sr, 256);
        soloChannel (*down2, "down1");
        setParam (*down2, "warmth", 100.0f);
        auto warm = playSine (*down2, sr, f, 1.0, 0);
        double diff = 0.0;   // after 0.1 s, once the switched-off channels have faded out
        for (size_t i = 4800; i < plain.size(); ++i) diff = std::max (diff, (double) std::abs (plain[i] - warm[i]));
        check (diff < 1.0e-6, "Warmth leaves -1 octave untouched (max difference " + juce::String (diff, 8) + ")");
    }

    // 11. Solo: only soloed channels play, even one that is switched off.
    {
        const double sr = 48000.0, f = 220.0;
        auto a = makeInstance (sr, 256);   // defaults: -1 Oct, Dry and +1 Oct on
        setParam (*a, "up2_solo", 1.0f);   // +2 Oct is off, but soloed
        const auto out = playSine (*a, sr, f, 1.5, 0);
        std::vector<float> steady (out.begin() + 36000, out.end());
        const double total = meanPower (steady, 0, steady.size()) * 2.0;
        const double up2 = tonePower (steady, 4.0 * f, sr);
        check (total > 1.0e-4 && up2 > 0.9 * total, "Solo +2 Oct: only +2 Oct is heard (" + juce::String (100.0 * up2 / (total + 1e-20), 0) + " % of the output)");
    }

    // 12. Options under the faders.
    {
        const double sr = 48000.0, f = 110.0;

        // Fuzz on -1 Oct: much more distortion, still within full scale.
        double distortion[2] {}, level[2] {};
        for (int on = 0; on < 2; ++on)
        {
            auto a = makeInstance (sr, 256);
            soloChannel (*a, "down1");
            setParam (*a, "down1_fuzz", (float) on);
            const auto out = playSine (*a, sr, f, 1.5, 0);
            std::vector<float> steady (out.begin() + 36000, out.end());
            level[on] = meanPower (steady, 0, steady.size()) * 2.0;
            distortion[on] = (level[on] - tonePower (steady, 0.5 * f, sr)) / (level[on] + 1e-20);
        }
        check (distortion[1] > 0.1 && distortion[1] > 5.0 * distortion[0],
               "Fuzz distorts -1 Oct (" + juce::String (100.0 * distortion[0], 1) + " % -> " + juce::String (100.0 * distortion[1], 1) + " %)");
        const double fuzzDb = 10.0 * std::log10 ((level[1] + 1e-20) / (level[0] + 1e-20));
        check (std::abs (fuzzDb) < 8.0, "Fuzz keeps the level within 8 dB (" + juce::String (fuzzDb, 1) + " dB)");

        // Crush on +1 Oct: the output moves in steps, so it has far more
        // sample-to-sample jumps (high-frequency grit) than without.
        double roughness[2] {};
        for (int on = 0; on < 2; ++on)
        {
            auto a = makeInstance (sr, 256);
            soloChannel (*a, "up1");
            setParam (*a, "up1_crush", (float) on);
            const auto out = playSine (*a, sr, f, 1.0, 0);
            double diff = 0.0, power = 0.0;
            for (size_t i = 24000; i + 1 < out.size(); ++i)
            {
                diff += (double) (out[i + 1] - out[i]) * (out[i + 1] - out[i]);
                power += (double) out[i] * out[i];
            }
            roughness[on] = diff / (power + 1e-20);
        }
        check (roughness[1] > 5.0 * roughness[0], "Crush adds lo-fi steps (roughness x" + juce::String (roughness[1] / (roughness[0] + 1e-20), 1) + ")");

        // Shimmer on +1 Oct: a tail rings on after the note stops; without it, silence.
        double tail[2] {};
        for (int on = 0; on < 2; ++on)
        {
            auto a = makeInstance (sr, 256);
            soloChannel (*a, "up1");
            setParam (*a, "up1_shimmer", (float) on);
            juce::AudioBuffer<float> buffer (2, 256);
            juce::MidiBuffer midi;
            std::vector<float> out;
            for (long pos = 0; pos < 96000; pos += 256)
            {
                for (int i = 0; i < 256; ++i)
                {
                    const long n = pos + i;
                    const float x = n < 48000 ? 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * f * (double) n / sr) : 0.0f;
                    buffer.setSample (0, i, x); buffer.setSample (1, i, x);
                }
                a->processBlock (buffer, midi);
                for (int i = 0; i < 256; ++i) out.push_back (buffer.getSample (0, i));
            }
            tail[on] = meanPower (out, 48000 + 24000, 48000 + 36000);   // 0.5 to 0.75 s after the note stops
            bool finite = true;
            for (float v : out) finite = finite && std::isfinite (v);
            check (finite, juce::String ("Shimmer ") + (on ? "on" : "off") + ": output always finite");
        }
        check (tail[1] > 1.0e-5 && tail[1] > 1000.0 * (tail[0] + 1e-12),
               "Shimmer leaves a tail after the note (" + juce::String (10.0 * std::log10 (tail[1] + 1e-20), 1) + " dB vs "
               + juce::String (10.0 * std::log10 (tail[0] + 1e-20), 1) + " dB without)");
    }

    std::printf (failures == 0 ? "\nAll host tests passed.\n" : "\n%d host checks FAILED.\n", failures);
    return failures == 0 ? 0 : 1;
}
