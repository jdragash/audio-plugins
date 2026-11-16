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
const double voiceRatio[4] = { 0.25, 0.5, 2.0, 4.0 };
const char* voiceName[4] = { "2 oct down", "1 oct down", "1 oct up", "2 oct up" };

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

struct Voices { std::vector<float> v[4]; };

Voices run (OctaveEngine& e, const std::vector<float>& in)
{
    Voices out;
    for (auto& v : out.v) v.resize (in.size());
    e.reset();
    for (size_t pos = 0; pos < in.size(); pos += 64)
    {
        const int n = (int) std::min<size_t> (64, in.size() - pos);
        float* outs[4] = { out.v[0].data() + pos, out.v[1].data() + pos, out.v[2].data() + pos, out.v[3].data() + pos };
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

// How much of a voice's output is NOT at the pitches it should be at.
void testPitches (OctaveEngine& e, double sr, const char* name, const std::vector<Note>& notes, const double limitDb[4])
{
    const auto in = render (notes, sr, 2 * 65536);
    const auto out = run (e, in);
    const double inPower = analyse (in, sr).total();

    for (int v = 0; v < 4; ++v)
    {
        const auto spec = analyse (out.v[v], sr);
        const double total = spec.total();
        const double wanted = spec.near (partials (notes, voiceRatio[v]), 6.0);
        const double junk = db ((total - wanted) / total);
        check (junk < limitDb[v], std::string (name) + ", " + voiceName[v] + ": off-pitch content", junk, "dB", limitDb[v]);

        const double level = db (total / inPower);
        check (std::abs (level) < 3.0, std::string (name) + ", " + voiceName[v] + ": level vs input", level, "dB", 3.0);
    }
}

// Time for a voice to reach half its final level after a note starts.
void testAttack (OctaveEngine& e, double sr, double hz, const double limitMs[4])
{
    const int start = (int) (0.1 * sr), total = (int) (1.0 * sr);
    const auto in = render ({ { hz, 1 } }, sr, total, start);
    const auto out = run (e, in);
    const int win = (int) (0.008 * sr);

    for (int v = 0; v < 4; ++v)
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
        check (ms < limitMs[v], name, ms, "ms", limitMs[v]);
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

void testCpu (OctaveEngine& e, double sr)
{
    const auto in = render ({ { 82.41, 8 }, { 123.47, 8 }, { 164.81, 8 } }, sr, (int) (5 * sr));
    const auto t0 = std::chrono::steady_clock::now();
    run (e, in);
    const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    const double percent = 100.0 * seconds / 5.0;
    check (percent < 25.0, "CPU use of one core at " + std::to_string ((int) sr) + " Hz", percent, "%", 25.0);
}
} // namespace

int main (int argc, char** argv)
{
    report = argc > 1 && std::strcmp (argv[1], "--report") == 0;

    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        OctaveEngine e;
        OctaveEngine::Tuning tuning;
        if (argc > 4 && std::strcmp (argv[1], "--tune") == 0)
        {
            report = true;
            tuning.baseWidthHz = (float) std::atof (argv[2]); tuning.widthPerHz = (float) std::atof (argv[3]);
            tuning.spacing = (float) std::atof (argv[4]);
        }
        e.prepare (sr, tuning);
        if (report) std::printf ("\n=== %.0f Hz sample rate, %d bands ===\n", sr, e.getNumBands());

        const double clean[4]  = { -30.0, -30.0, -30.0, -26.0 };
        const double chord[4]  = { -20.0, -20.0, -20.0, -18.0 };
        // Full chords with many harmonics are judged more loosely: harmonics of
        // different strings land within a few Hz of each other and beat, which
        // this measurement counts as off-pitch even though a real guitar does
        // the same thing.
        const double dense[4]  = { -17.0, -17.0, -10.0, -10.0 };

        testPitches (e, sr, "low E sine",            { { 82.41, 1 } }, clean);
        testPitches (e, sr, "A 440 sine",            { { 440.0, 1 } }, clean);
        testPitches (e, sr, "high E (12th fret) sine", { { 659.26, 1 } }, clean);
        testPitches (e, sr, "low E string",          { { 82.41, 10 } }, clean);
        testPitches (e, sr, "A string",              { { 110.0, 10 } }, clean);
        testPitches (e, sr, "G string",              { { 196.0, 10 } }, clean);
        testPitches (e, sr, "fifth E2+B2 (sines)",   { { 82.41, 1 }, { 123.47, 1 } }, chord);
        testPitches (e, sr, "major third A3+C#4 (sines)", { { 220.0, 1 }, { 277.18, 1 } }, chord);
        testPitches (e, sr, "A major triad (sines)", { { 220.0, 1 }, { 277.18, 1 }, { 329.63, 1 } }, chord);
        testPitches (e, sr, "A major triad (2 harmonics)", { { 220.0, 2 }, { 277.18, 2 }, { 329.63, 2 } }, chord);
        testPitches (e, sr, "E5 power chord",        { { 82.41, 8 }, { 123.47, 8 }, { 164.81, 8 } }, dense);
        testPitches (e, sr, "A major triad",         { { 220.0, 6 }, { 277.18, 6 }, { 329.63, 6 } }, dense);
        testPitches (e, sr, "open E major chord",    { { 82.41, 6 }, { 123.47, 6 }, { 164.81, 6 }, { 207.65, 6 }, { 246.94, 6 }, { 329.63, 6 } }, dense);

        if (sr == 48000.0)
        {
            // The octaves fade in rather than starting instantly, faster for
            // higher notes. These limits keep that fade-in from growing.
            const double lowE[4] = { 45.0, 45.0, 45.0, 45.0 };
            const double a3[4]   = { 30.0, 30.0, 30.0, 30.0 };
            const double a4[4]   = { 20.0, 20.0, 20.0, 20.0 };
            const double a5[4]   = { 14.0, 14.0, 14.0, 14.0 };
            testAttack (e, sr, 82.41, lowE);
            testAttack (e, sr, 220.0, a3);
            testAttack (e, sr, 440.0, a4);
            testAttack (e, sr, 880.0, a5);
            testSilenceAndSafety (e, sr);
            testCpu (e, sr);
        }
    }

    std::printf (failures == 0 ? "\nAll engine tests passed.\n" : "\n%d engine checks FAILED.\n", failures);
    return failures == 0 ? 0 : 1;
}
