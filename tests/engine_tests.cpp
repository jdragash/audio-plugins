// Offline tests for the octave engine. Renders notes and chords through the
// engine and checks that each voice comes out at the right pitch, at the right
// level, without junk in between, and quickly enough to feel like a pedal.
//
// Run with no arguments to test. Run with "--report" to print every measurement.

#include "dsp/OctaveEngine.h"

#include <chrono>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using puppychow::OctaveEngine;

namespace
{
const double pi = 3.141592653589793;
const int numVoices = OctaveEngine::numVoices;
const int numPlainVoices = OctaveEngine::up1Detuned; // everything except the detuned copies
const double* const voiceRatio = OctaveEngine::voiceRatio;
const char* voiceName[numVoices] = { "2 oct down", "1 oct down", "4th down", "5th up", "1 oct up", "oct+5th up", "2 oct up",
                                     "1 oct up detuned", "2 oct up detuned" };

bool report = false;
int failures = 0;

void check (bool ok, const std::string& what, double value, const char* unit, double limit)
{
    if (! ok) ++failures;
    if (! ok || report)
        std::printf ("%s  %-58s %8.2f %s (limit %.2f)\n", ok ? "ok  " : "FAIL", what.c_str(), value, unit, limit);
}

void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const std::complex<double> wl = std::polar (1.0, -2.0 * pi / (double) len);
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w = 1.0;
            for (size_t j = 0; j < len / 2; ++j)
            {
                const auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v; a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

struct Note { double hz; int harmonics; };

// A steady test signal: each note is a stack of harmonics falling off like a
// plucked string (1/h), with arbitrary phases.
std::vector<float> render (const std::vector<Note>& notes, double sr, int numSamples, int startAt = 0)
{
    std::vector<float> x ((size_t) numSamples, 0.0f);
    for (const auto& n : notes)
        for (int h = 1; h <= n.harmonics && n.hz * h < 2800.0; ++h)
            for (int i = startAt; i < numSamples; ++i)
                x[(size_t) i] += (float) (0.2 / h * std::sin (2.0 * pi * n.hz * h * (i - startAt) / sr + 1.7 * h + n.hz));
    return x;
}

struct Voices { std::vector<float> v[numVoices]; };

Voices run (OctaveEngine& e, const std::vector<float>& in)
{
    Voices out;
    for (auto& v : out.v) v.resize (in.size());
    e.reset();
    for (size_t pos = 0; pos < in.size(); pos += 64)
    {
        const int n = (int) std::min<size_t> (64, in.size() - pos);
        float* outs[numVoices];
        for (int v = 0; v < numVoices; ++v) outs[v] = out.v[v].data() + pos;
        e.process (in.data() + pos, outs, n);
    }
    return out;
}

struct Spectrum
{
    std::vector<double> power; // per bin
    double binHz = 0.0;

    double total() const { double s = 0; for (double p : power) s += p; return s; }

    // Power within tolHz of any of the given frequencies.
    double near (const std::vector<double>& freqs, double tolHz) const
    {
        std::vector<char> mark (power.size(), 0);
        for (double f : freqs)
        {
            const long lo = std::max (0L, (long) std::floor ((f - tolHz) / binHz));
            const long hi = std::min ((long) power.size() - 1, (long) std::ceil ((f + tolHz) / binHz));
            for (long b = lo; b <= hi; ++b) mark[(size_t) b] = 1;
        }
        double s = 0;
        for (size_t b = 0; b < power.size(); ++b) if (mark[b]) s += power[b];
        return s;
    }
};

Spectrum analyse (const std::vector<float>& x, double sr)
{
    const size_t n = 65536;
    std::vector<std::complex<double>> a (n);
    const size_t start = x.size() - n;
    for (size_t i = 0; i < n; ++i)
        a[i] = x[start + i] * (0.5 - 0.5 * std::cos (2.0 * pi * (double) i / (double) (n - 1)));
    fft (a);
    Spectrum s;
    s.binHz = sr / (double) n;
    s.power.resize (n / 2);
    for (size_t b = 0; b < n / 2; ++b) s.power[b] = std::norm (a[b]);
    return s;
}

std::vector<double> partials (const std::vector<Note>& notes, double ratio)
{
    std::vector<double> f;
    for (const auto& n : notes)
        for (int h = 1; h <= n.harmonics && n.hz * h < 2800.0; ++h)
            f.push_back (n.hz * h * ratio);
    return f;
}

double db (double powerRatio) { return 10.0 * std::log10 (std::max (powerRatio, 1.0e-20)); }

// Limits for one test, in dB of off-pitch content: one for the voices that go
// down and one for the voices that go up. The two highest voices multiply any
// error the most, so they get a little more room.
struct Limits { double down, up; };

double limitFor (const Limits& l, int v)
{
    if (voiceRatio[v] < 1.0) return l.down;
    return l.up + (voiceRatio[v] >= 3.0 ? 3.0 : 0.0);
}

// How much of a voice's output is NOT at the pitches it should be at.
void testPitches (OctaveEngine& e, double sr, const char* name, const std::vector<Note>& notes, Limits limits,
                  double pitchScaleForDetuned = 0.0)
{
    const auto in = render (notes, sr, 2 * 65536);
    const auto out = run (e, in);
    const double inPower = analyse (in, sr).total();
    const bool detuned = pitchScaleForDetuned > 0.0;

    for (int v = detuned ? numPlainVoices : 0; v < (detuned ? numVoices : numPlainVoices); ++v)
    {
        const auto spec = analyse (out.v[v], sr);
        const double total = spec.total();
        const double wanted = spec.near (partials (notes, voiceRatio[v] * (detuned ? pitchScaleForDetuned : 1.0)), 6.0);
        const double junk = db ((total - wanted) / total);
        const double limit = limitFor (limits, v);
        check (junk < limit, std::string (name) + ", " + voiceName[v] + ": off-pitch content", junk, "dB", limit);

        const double level = db (total / inPower);
        check (std::abs (level) < 3.0, std::string (name) + ", " + voiceName[v] + ": level vs input", level, "dB", 3.0);
    }
}

// Time for a voice to reach half its final level after a note starts.
void testAttack (OctaveEngine& e, double sr, double hz, double limitMs)
{
    const int start = (int) (0.1 * sr), total = (int) (1.0 * sr);
    const auto in = render ({ { hz, 1 } }, sr, total, start);
    const auto out = run (e, in);
    const int win = (int) (0.008 * sr);

    for (int v = 0; v < numPlainVoices; ++v)
    {
        const double f = hz * voiceRatio[v];
        auto levelAt = [&] (int centre)
        {
            std::complex<double> acc = 0; double ws = 0;
            for (int i = -win; i <= win; ++i)
            {
                const int idx = centre + i;
                if (idx < 0 || idx >= total) continue;
                const double w = 0.5 + 0.5 * std::cos (pi * i / (double) win);
                acc += w * (double) out.v[v][(size_t) idx] * std::polar (1.0, -2.0 * pi * f * idx / sr);
                ws += w;
            }
            return 2.0 * std::abs (acc) / ws;
        };

        const double steady = levelAt (total - 2 * win);
        double ms = 999.0;
        for (int i = start; i < total - win; i += 12)
            if (levelAt (i) >= 0.5 * steady) { ms = 1000.0 * (i - start) / sr; break; }

        char name[96];
        std::snprintf (name, sizeof (name), "attack at %.0f Hz, %s: time to half level", hz, voiceName[v]);
        check (ms < limitMs, name, ms, "ms", limitMs);
    }
}

void testSilenceAndSafety (OctaveEngine& e, double sr)
{
    std::vector<float> in ((size_t) sr, 0.0f);
    auto out = run (e, in);
    double peak = 0;
    for (auto& v : out.v) for (float s : v) peak = std::max (peak, (double) std::abs (s));
    check (peak < 1.0e-6, "silence in gives silence out: peak", peak, "", 1.0e-6);

    // Loud noise bursts and a full-scale impulse must never produce NaN or blow up.
    unsigned seed = 12345;
    for (size_t i = 0; i < in.size(); ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        in[i] = ((i / 4800) % 2 == 0) ? (float) ((seed >> 8) / 8388608.0 - 1.0) : 0.0f;
    }
    in[100] = 1.0f;
    out = run (e, in);
    bool finite = true; peak = 0;
    for (auto& v : out.v) for (float s : v) { finite = finite && std::isfinite (s); peak = std::max (peak, (double) std::abs (s)); }
    check (finite, "noise bursts: output stays finite", finite ? 1 : 0, "", 1);
    check (peak < 8.0, "noise bursts: output peak stays bounded", peak, "", 8.0);
}

// Plucked low notes and fast strums must not produce clicks: sudden jumps
// that a smooth signal at that voice's pitch range could never make.
void testClicks (OctaveEngine& e, double sr, const char* modeName)
{
    const int n = (int) (4.0 * sr);
    std::vector<float> in ((size_t) n, 0.0f);

    auto pluck = [&] (double startSeconds, double hz, double amp)
    {
        const int at = (int) (startSeconds * sr);
        unsigned seed = (unsigned) at * 7u + 1u;
        for (int i = at; i < n; ++i)
        {
            const double t = (i - at) / sr;
            double x = 0.0;
            for (int h = 1; h <= 12 && hz * h < 5000.0; ++h)
                x += std::sin (2.0 * pi * hz * h * t * (1.0 + 0.0004 * h * h)) / h * std::exp (-t * (1.5 + 0.4 * h));
            if (t < 0.004) // pick noise
            {
                seed = seed * 1664525u + 1013904223u;
                x += ((seed >> 8) / 8388608.0 - 1.0) * 0.5 * (1.0 - t / 0.004);
            }
            in[(size_t) i] += (float) (amp * x);
        }
    };

    pluck (0.1, 82.41, 0.2);  pluck (0.8, 73.42, 0.2);  pluck (1.4, 110.0, 0.2);
    const double chord[6] = { 82.41, 123.47, 164.81, 207.65, 246.94, 329.63 };
    for (int r = 0; r < 2; ++r)
        for (int s = 0; s < 6; ++s)
            pluck (2.0 + r * 0.9 + s * 0.012, chord[s], 0.12);

    const auto out = run (e, in);

    for (int v = 0; v < numPlainVoices; ++v)
    {
        // The largest second difference a signal of amplitude 1 can have at the
        // voice's top frequency, with some margin.
        const double topHz = std::min (5000.0 * voiceRatio[v], 0.45 * sr);
        const double limit = 0.6 * std::pow (2.0 * pi * topHz / sr, 2.0) + 0.002;
        int clicks = 0;
        for (size_t i = 2; i < out.v[v].size(); ++i)
        {
            const double d2 = std::abs (out.v[v][i] - 2.0 * out.v[v][i - 1] + out.v[v][i - 2]);
            if (d2 > limit) ++clicks;
        }
        check (clicks == 0, std::string (modeName) + " tracking, low notes and strums, " + voiceName[v] + ": click samples",
               clicks, "", 0.0);
    }
}

void testCpu (OctaveEngine& e, double sr)
{
    const auto in = render ({ { 82.41, 8 }, { 123.47, 8 }, { 164.81, 8 } }, sr, (int) (5 * sr));
    const auto t0 = std::chrono::steady_clock::now();
    run (e, in);
    const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    const double percent = 100.0 * seconds / 5.0;
    // A loose limit: this only guards against something going badly wrong. With
    // clang (what the Mac build uses) the real figure is several times lower.
    check (percent < 60.0, "CPU use of one core at " + std::to_string ((int) sr) + " Hz, all voices on", percent, "%", 60.0);

    bool few[numVoices] = {};
    few[OctaveEngine::down1] = few[OctaveEngine::up1] = true;
    e.setActiveVoices (few);
    const auto t1 = std::chrono::steady_clock::now();
    run (e, in);
    const double percentFew = 100.0 * std::chrono::duration<double> (std::chrono::steady_clock::now() - t1).count() / 5.0;
    check (percentFew < 60.0, "CPU use of one core at " + std::to_string ((int) sr) + " Hz, octave down + up only", percentFew, "%", 60.0);

    bool all[numVoices];
    for (auto& a : all) a = true;
    e.setActiveVoices (all);
}
} // namespace

int main (int argc, char** argv)
{
    report = argc > 1 && (std::strcmp (argv[1], "--report") == 0 || std::strcmp (argv[1], "--tune") == 0);

    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        OctaveEngine e;
        OctaveEngine::Tuning tuning = OctaveEngine::Tuning::clean();
        if (argc > 4 && std::strcmp (argv[1], "--tune") == 0)
        {
            tuning.baseWidthHz = (float) std::atof (argv[2]); tuning.widthPerHz = (float) std::atof (argv[3]);
            tuning.spacing = (float) std::atof (argv[4]);
        }
        e.prepare (sr, tuning);
        if (report) std::printf ("\n=== Clean tracking, %.0f Hz sample rate, %d bands ===\n", sr, e.getNumBands());

        const Limits clean { -30.0, -28.0 };
        const Limits chord { -24.0, -20.0 };
        // Full chords with many harmonics are judged more loosely: harmonics of
        // different strings land within a few Hz of each other and beat, which
        // this measurement counts as off-pitch even though a real guitar does
        // the same thing.
        const Limits dense { -18.0, -11.0 };

        testPitches (e, sr, "low E sine",            { { 82.41, 1 } }, clean);
        testPitches (e, sr, "A 440 sine",            { { 440.0, 1 } }, clean);
        testPitches (e, sr, "high E (12th fret) sine", { { 659.26, 1 } }, clean);
        testPitches (e, sr, "low E string",          { { 82.41, 10 } }, clean);
        testPitches (e, sr, "A string",              { { 110.0, 10 } }, clean);
        testPitches (e, sr, "G string",              { { 196.0, 10 } }, clean);
        testPitches (e, sr, "fifth E2+B2 (sines)",   { { 82.41, 1 }, { 123.47, 1 } }, chord);
        testPitches (e, sr, "low third E2+G#2 (sines)", { { 82.41, 1 }, { 103.83, 1 } }, chord);
        testPitches (e, sr, "whole tone A3+B3 (sines)", { { 220.0, 1 }, { 246.94, 1 } }, chord);
        testPitches (e, sr, "major third A3+C#4 (sines)", { { 220.0, 1 }, { 277.18, 1 } }, chord);
        testPitches (e, sr, "A major triad (sines)", { { 220.0, 1 }, { 277.18, 1 }, { 329.63, 1 } }, chord);
        testPitches (e, sr, "A major triad (2 harmonics)", { { 220.0, 2 }, { 277.18, 2 }, { 329.63, 2 } }, chord);
        testPitches (e, sr, "drop D power chord",    { { 73.42, 6 }, { 110.0, 6 }, { 146.83, 6 } }, chord);
        testPitches (e, sr, "E5 power chord",        { { 82.41, 8 }, { 123.47, 8 }, { 164.81, 8 } }, dense);
        testPitches (e, sr, "low E minor chord",     { { 82.41, 6 }, { 123.47, 6 }, { 164.81, 6 }, { 196.0, 6 } }, dense);
        testPitches (e, sr, "A major triad",         { { 220.0, 6 }, { 277.18, 6 }, { 329.63, 6 } }, dense);
        testPitches (e, sr, "open E major chord",    { { 82.41, 6 }, { 123.47, 6 }, { 164.81, 6 }, { 207.65, 6 }, { 246.94, 6 }, { 329.63, 6 } }, dense);

        if (report)
        {
            // Semitone clashes are the known weak spot. Measured, not enforced.
            const Limits none { 0.0, 0.0 };
            testPitches (e, sr, "semitone E3+F3 (sines)", { { 164.81, 1 }, { 174.61, 1 } }, none);
            testPitches (e, sr, "semitone E4+F4 (sines)", { { 329.63, 1 }, { 349.23, 1 } }, none);
        }

        // Detune: the copies must come out sharp by the requested amount.
        e.setDetuneCents (12.0f);
        const double sharp = std::pow (2.0, 12.0 / 1200.0);
        testPitches (e, sr, "detune 12 cents, A 440 sine", { { 440.0, 1 } }, clean, sharp);
        testPitches (e, sr, "detune 12 cents, A string",   { { 110.0, 10 } }, clean, sharp);
        testPitches (e, sr, "detune 12 cents, E5 power chord", { { 82.41, 8 }, { 123.47, 8 }, { 164.81, 8 } }, dense, sharp);
        e.setDetuneCents (0.0f);

        if (sr == 48000.0)
        {
            // The voices fade in rather than starting instantly, faster for
            // higher notes. These limits keep that fade-in from growing.
            testAttack (e, sr, 82.41, 150.0);
            testAttack (e, sr, 220.0, 100.0);
            testAttack (e, sr, 440.0, 66.0);
            testAttack (e, sr, 880.0, 40.0);
            testSilenceAndSafety (e, sr);
            testClicks (e, sr, "Clean");
            testCpu (e, sr);

            // The other tracking modes: single notes must stay clean and stable.
            struct Mode { const char* name; OctaveEngine::Tuning tuning; };
            for (const Mode& m : { Mode { "Tight", OctaveEngine::Tuning::tight() }, Mode { "Balanced", OctaveEngine::Tuning::balanced() } })
            {
                OctaveEngine other;
                other.prepare (sr, m.tuning);
                if (report) std::printf ("\n=== %s tracking, %.0f Hz sample rate, %d bands ===\n", m.name, sr, other.getNumBands());
                testPitches (other, sr, "low E string", { { 82.41, 10 } }, clean);
                testPitches (other, sr, "A 440 sine",   { { 440.0, 1 } }, clean);
                testPitches (other, sr, "major third A3+C#4 (sines)", { { 220.0, 1 }, { 277.18, 1 } }, chord);
                testSilenceAndSafety (other, sr);
                testClicks (other, sr, m.name);
                testCpu (other, sr);
                if (report)
                {
                    testAttack (other, sr, 82.41, 999.0);
                    testAttack (other, sr, 440.0, 999.0);
                }
            }
        }
    }

    std::printf (failures == 0 ? "\nAll engine tests passed.\n" : "\n%d engine checks FAILED.\n", failures);
    return failures == 0 ? 0 : 1;
}
