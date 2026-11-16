#pragma once

// Puppy Chow polyphonic octave engine.
//
// No pitch detection. The input is split by a bank of narrow complex band-pass
// filters. Each band then holds (roughly) one sine wave, described by an
// amplitude and a rotating phase. Doubling that phase gives the octave up,
// halving it gives the octave down, and the amplitude is left alone, so the
// octaves follow the playing dynamics exactly. Because each band is shifted
// independently, chords stay chords.
//
// Plain C++ with no JUCE dependency so it can be unit tested anywhere.
// Real-time safe: no allocation, locks or system calls after prepare().

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace puppychow
{

class OctaveEngine
{
public:
    static constexpr int maxBands = 160;
    static constexpr int numStages = 4;
    static constexpr int sharpenReach = 3;

    enum Voice { down2 = 0, down1 = 1, up1 = 2, up2 = 3, numVoices = 4 };

    struct Tuning
    {
        float lowestHz      = 40.0f;   // centre of the first band
        float highestHz     = 3200.0f; // no bands above this
        float baseWidthHz   = 8.0f;    // band width at low frequencies
        float widthPerHz    = 0.03f;   // extra width per Hz of centre frequency
        float spacing       = 0.6f;    // band spacing as a fraction of band width
    };

    void prepare (double sampleRate) { prepare (sampleRate, Tuning{}); }

    void prepare (double sampleRate, const Tuning& t)
    {
        sr = sampleRate;
        numBands = 0;

        const double twoPi = 6.283185307179586;
        const double nyquistGuard = 0.45 * sampleRate;
        double f = t.lowestHz;
        double alignPhase = 0.0, prevCentre = 0.0, prevHalfWidth = 1.0;

        while (f <= t.highestHz && f < nyquistGuard && numBands < maxBands)
        {
            const double width = t.baseWidthHz + t.widthPerHz * f;      // -3 dB width of the band
            const double poleWidth = width / 0.435;                      // four cascaded poles narrow the band
            const double a = std::exp (-3.141592653589793 * poleWidth / sampleRate);
            const double w = twoPi * f / sampleRate;

            const int k = numBands++;
            poleRe[k] = (float) (a * std::cos (w));
            poleIm[k] = (float) (a * std::sin (w));
            // Each filter delays the phase by an amount that depends on how far a
            // note sits from the band centre, so neighbouring bands would disagree
            // about a note's phase and partly cancel once that phase is doubled.
            // Giving every band a fixed phase offset (the running total of the
            // filter phase slope up to that band) lines neighbours up again.
            const double halfWidth = 0.5 * poleWidth;
            if (k > 0)
                alignPhase -= numStages * (f - prevCentre) * 0.5 * (1.0 / halfWidth + 1.0 / prevHalfWidth);
            prevCentre = f; prevHalfWidth = halfWidth;

            const double g = std::pow (1.0 - a, (double) numStages);     // unity for a sine at the band centre
            inRe[k] = (float) (g * std::cos (-alignPhase));
            inIm[k] = (float) (g * std::sin (-alignPhase));
            centreHz[k] = (float) f;

            // Octaves up must stay below Nyquist or they fold back as noise.
            up1Gain[k] = (2.0 * f < nyquistGuard) ? 1.0f : 0.0f;
            up2Gain[k] = (4.0 * f < nyquistGuard) ? 1.0f : 0.0f;

            f += t.spacing * width;
        }

        hilbert.prepare (sampleRate);
        calibrate();
        reset();
    }

    void reset()
    {
        hilbert.reset();
        for (auto& m : magPadded) m = 0.0f;

        for (int k = 0; k < maxBands; ++k)
        {
            for (int st = 0; st < numStages; ++st) sRe[st][k] = sIm[st][k] = 0.0f;
            flip1[k] = flip2[k] = 1.0f;
            prevSin[k] = prevSinD1[k] = 1.0f;
        }
    }

    int getNumBands() const noexcept { return numBands; }
    float getBandCentre (int k) const noexcept { return centreHz[k]; }

    // Processes one mono block. outs[v] receives voice v (see Voice enum).
    void process (const float* in, float* const* outs, int numSamples) noexcept
    {
        const int nb = numBands;

        for (int i = 0; i < numSamples; ++i)
        {
            // Turn the real input into a rotating (analytic) signal xr + j*xi.
            float xr, xi;
            hilbert.process (in[i], xr, xi);

            // Run every band filter. Each band is four identical complex one-pole
            // resonators in a row (a "gammatone" shape: steep sides, but still
            // quick to respond). The loops are written band-by-band over plain
            // arrays so the compiler can process several bands per instruction.
            for (int k = 0; k < nb; ++k)
            {
                bandRe[k] = xr * inRe[k] - xi * inIm[k];
                bandIm[k] = xr * inIm[k] + xi * inRe[k];
            }

            for (int st = 0; st < numStages; ++st)
            {
                float* const stRe = sRe[st];
                float* const stIm = sIm[st];

                for (int k = 0; k < nb; ++k)
                {
                    const float nr = bandRe[k] + poleRe[k] * stRe[k] - poleIm[k] * stIm[k];
                    const float ni = bandIm[k] + poleRe[k] * stIm[k] + poleIm[k] * stRe[k];
                    stRe[k] = nr; stIm[k] = ni;
                    bandRe[k] = nr; bandIm[k] = ni;
                }
            }

            // Amplitude of each band. (Stored with a few empty slots either side
            // so the neighbour search below needs no edge cases.)
            float* const mag = magPadded + sharpenReach;
            for (int k = 0; k < nb; ++k)
                mag[k] = std::sqrt (bandRe[k] * bandRe[k] + bandIm[k] * bandIm[k]);

            // A note shows up strongly in the band nearest to it and weakly in the
            // bands around it. Only the strong bands have a trustworthy phase, so
            // fade each band out relative to its loudest neighbour. This is a big
            // part of what keeps chords clean.
            for (int k = 0; k < nb; ++k)
            {
                float peak = magPadded[k];
                for (int j = 1; j <= 2 * sharpenReach; ++j)
                    peak = std::max (peak, magPadded[k + j]);
                peakMag[k] = peak;
            }

            for (int k = 0; k < nb; ++k)
            {
                const float inv = 1.0f / (mag[k] + 1.0e-30f);
                const float c = bandRe[k] * inv, s = bandIm[k] * inv;

                float rel = mag[k] / (peakMag[k] + 1.0e-30f);
                rel *= rel; rel *= rel;
                const float amp = mag[k] * rel;

                // Octave up: double the phase. Two up: double it again.
                const float c2 = c * c - s * s;
                outU1[k] = up1Gain[k] * amp * c2;
                outU2[k] = up2Gain[k] * amp * (2.0f * c2 * c2 - 1.0f);

                // Octave down: halve the phase. The half-angle formula only knows
                // the phase within one turn, so flip the sign each time the input
                // phase wraps around to keep the output continuous.
                const float sgn = (s >= 0.0f) ? 1.0f : -1.0f;
                const float wrap1 = (c < 0.0f ? 1.0f : 0.0f) * (sgn * prevSin[k] < 0.0f ? 1.0f : 0.0f);
                const float f1 = flip1[k] * (1.0f - 2.0f * wrap1);
                flip1[k] = f1;
                prevSin[k] = sgn;
                const float hc = f1 * std::sqrt (std::max (0.0f, 0.5f * (1.0f + c)));
                const float hs = f1 * sgn * std::sqrt (std::max (0.0f, 0.5f * (1.0f - c)));
                outD1[k] = amp * hc;

                // Two octaves down: halve again.
                const float sgnD = (hs >= 0.0f) ? 1.0f : -1.0f;
                const float wrap2 = (hc < 0.0f ? 1.0f : 0.0f) * (sgnD * prevSinD1[k] < 0.0f ? 1.0f : 0.0f);
                const float f2 = flip2[k] * (1.0f - 2.0f * wrap2);
                flip2[k] = f2;
                prevSinD1[k] = sgnD;
                outD2[k] = amp * f2 * std::sqrt (std::max (0.0f, 0.5f * (1.0f + hc)));
            }

            float sumD2 = 0.0f, sumD1 = 0.0f, sumU1 = 0.0f, sumU2 = 0.0f;
            for (int k = 0; k < nb; ++k)
            {
                sumD2 += outD2[k]; sumD1 += outD1[k];
                sumU1 += outU1[k]; sumU2 += outU2[k];
            }

            outs[down2][i] = sumD2 * norm[down2];
            outs[down1][i] = sumD1 * norm[down1];
            outs[up1][i]   = sumU1 * norm[up1];
            outs[up2][i]   = sumU2 * norm[up2];
        }
    }

    void setNorm (Voice v, float n) noexcept { norm[(size_t) v] = n; }
    float getNorm (Voice v) const noexcept { return norm[(size_t) v]; }

private:
    // 90-degree phase splitter (two chains of all-pass filters, coefficients by
    // Olli Niemitalo). Removes the mirror-image negative frequencies with about
    // 45 dB rejection from 30 Hz up, and adds no meaningful delay.
    struct Hilbert
    {
        static constexpr int maxDelay = 8;

        void prepare (double sampleRate)
        {
            // The design is for 44.1/48 kHz. At higher rates, stretch the delays
            // so it covers the same range in Hz.
            const int mult = std::clamp ((int) std::lround (sampleRate / 48000.0), 1, maxDelay / 2);
            delay = 2 * mult;
            reset();
        }

        void reset()
        {
            for (auto& sec : a) sec = {};
            for (auto& sec : b) sec = {};
            for (auto& d : outDelay) d = 0.0f;
            pos = 0;
        }

        void process (float x, float& re, float& im) noexcept
        {
            static constexpr float ca[4] = { 0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f };
            static constexpr float cb[4] = { 0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f };

            float u = x, v = x;
            for (int n = 0; n < 4; ++n)
            {
                u = a[n].tick (u, ca[n] * ca[n], pos, delay);
                v = b[n].tick (v, cb[n] * cb[n], pos, delay);
            }

            // Chain A needs half a section's extra delay.
            const int half = delay / 2;
            const int idx = pos % half;
            re = outDelay[idx];
            outDelay[idx] = u;
            im = -v;

            pos = (pos + 1) % (maxDelay * 2);
        }

        struct Section
        {
            float xs[maxDelay] {}, ys[maxDelay] {};

            float tick (float x, float a2, int p, int d) noexcept
            {
                const int i = p % d;
                const float y = a2 * (x + ys[i]) - xs[i];
                xs[i] = x; ys[i] = y;
                return y;
            }
        };

        Section a[4], b[4];
        float outDelay[maxDelay] {};
        int delay = 2, pos = 0;
    };

    Hilbert hilbert;

    // Neighbouring bands overlap, so a single sine lands in several of them.
    // Measure how much they add up to for a mid-range note and scale every
    // voice so its level matches the input level.
    void calibrate()
    {
        for (auto& n : norm) n = 1.0f;
        reset();

        // Play a steady 330 Hz sine through the engine and measure what comes out.
        const int total = (int) (0.4 * sr), settle = total / 2;
        float buf[numVoices][64];
        float* outs[numVoices] = { buf[0], buf[1], buf[2], buf[3] };
        float in[64];
        double peak[numVoices] = {};

        for (int n = 0; n < total; n += 64)
        {
            for (int i = 0; i < 64; ++i)
                in[i] = (float) std::sin (6.283185307179586 * 330.0 * (n + i) / sr);
            process (in, outs, 64);
            if (n >= settle)
                for (int v = 0; v < numVoices; ++v)
                    for (int i = 0; i < 64; ++i)
                        peak[v] = std::max (peak[v], (double) std::abs (buf[v][i]));
        }

        for (int v = 0; v < numVoices; ++v)
            norm[(size_t) v] = (float) (1.0 / std::max (peak[v], 1.0e-6));
    }

    double sr = 48000.0;
    int numBands = 0;
    std::array<float, numVoices> norm { 1.0f, 1.0f, 1.0f, 1.0f };

    alignas (32) float poleRe[maxBands] {}, poleIm[maxBands] {}, inRe[maxBands] {}, inIm[maxBands] {};
    alignas (32) float up1Gain[maxBands] {}, up2Gain[maxBands] {}, centreHz[maxBands] {};
    alignas (32) float sRe[numStages][maxBands] {}, sIm[numStages][maxBands] {};
    alignas (32) float bandRe[maxBands] {}, bandIm[maxBands] {}, peakMag[maxBands] {};
    alignas (32) float magPadded[maxBands + 2 * sharpenReach] {};
    alignas (32) float outD2[maxBands] {}, outD1[maxBands] {}, outU1[maxBands] {}, outU2[maxBands] {};
    alignas (32) float flip1[maxBands] {}, flip2[maxBands] {}, prevSin[maxBands] {}, prevSinD1[maxBands] {};
};

} // namespace puppychow
