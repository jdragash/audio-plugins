#pragma once

// Puppy Chow polyphonic octave engine.
//
// No pitch detection. The input is split by a bank of narrow complex band-pass
// filters. Each band then holds (roughly) one sine wave, described by an
// amplitude and a rotating phase. Multiplying that phase gives a new pitch:
// x2 is an octave up, x1/2 an octave down, x3/2 a fifth up, and so on. The
// amplitude is left alone, so the voices follow the playing dynamics exactly.
// Because each band is shifted independently, chords stay chords.
//
// Plain C++ with no JUCE dependency so it can be unit tested anywhere.
// Real-time safe: no allocation, locks or system calls after prepare().

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

#if defined (_MSC_VER)
 #define PUPPYCHOW_RESTRICT __restrict
#else
 #define PUPPYCHOW_RESTRICT __restrict__
#endif

namespace puppychow
{

class OctaveEngine
{
public:
    static constexpr int maxBands = 240;
    static constexpr int numStages = 4;
    static constexpr int pad = 3;            // spare slots either side of per-band arrays
    static constexpr int sharpenReach = pad;

    // Outputs of process(), lowest pitch first. The last two are slightly
    // sharp copies of the octave-up voices, used for the Detune control.
    enum Voice
    {
        down2 = 0,      // two octaves down   (phase x 1/4)
        down1,          // one octave down    (phase x 1/2)
        fourthDown,     // a fourth down      (phase x 3/4)
        fifthUp,        // a fifth up         (phase x 3/2)
        up1,            // one octave up      (phase x 2)
        octFifthUp,     // octave + fifth up  (phase x 3)
        up2,            // two octaves up     (phase x 4)
        up1Detuned,
        up2Detuned,
        numVoices
    };

    static constexpr double voiceRatio[numVoices] = { 0.25, 0.5, 0.75, 1.5, 2.0, 3.0, 4.0, 2.0, 4.0 };

    // Narrower bands separate close notes better (cleaner low and dissonant
    // chords) but take longer to respond (the voices fade in more slowly).
    struct Tuning
    {
        float lowestHz      = 40.0f;   // centre of the first band
        float highestHz     = 3200.0f; // no bands above this
        float baseWidthHz   = 4.0f;    // band width at low frequencies
        float widthPerHz    = 0.02f;   // extra width per Hz of centre frequency
        float spacing       = 0.6f;    // band spacing as a fraction of band width

        static Tuning tight()      { Tuning t; t.baseWidthHz = 8.0f; t.widthPerHz = 0.03f;  return t; }
        static Tuning balanced()   { Tuning t; t.baseWidthHz = 6.0f; t.widthPerHz = 0.025f; return t; }
        static Tuning clean()      { Tuning t; t.baseWidthHz = 4.0f; t.widthPerHz = 0.02f;  return t; }
    };

    void prepare (double sampleRate) { prepare (sampleRate, Tuning::clean()); }

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
            // about a note's phase and partly cancel once that phase is scaled.
            // Giving every band a fixed phase offset (the running total of the
            // filter phase slope up to that band) lines neighbours up again.
            const double halfWidth = 0.5 * poleWidth;
            if (k > 0)
                alignPhase -= numStages * (f - prevCentre) * 0.5 * (1.0 / halfWidth + 1.0 / prevHalfWidth);
            prevCentre = f; prevHalfWidth = halfWidth;

            const double g = std::pow (1.0 - a, (double) numStages);     // unity for a sine at the band centre
            inRe[k] = (float) (g * std::cos (alignPhase));
            inIm[k] = (float) (g * std::sin (alignPhase));
            centreHz[k] = (float) f;

            // Voices that go up must stay below Nyquist or they fold back as noise.
            gainFifth[k]    = (1.5 * f < nyquistGuard) ? 1.0f : 0.0f;
            gainUp1[k]      = (2.0 * f < nyquistGuard) ? 1.0f : 0.0f;
            gainOctFifth[k] = (3.0 * f < nyquistGuard) ? 1.0f : 0.0f;
            gainUp2[k]      = (4.0 * f < nyquistGuard) ? 1.0f : 0.0f;

            f += t.spacing * width;
        }

        hilbert.prepare (sampleRate);
        detuneCents = -1.0f;
        setDetuneCents (0.0f);
        calibrate();
        reset();
    }

    void reset()
    {
        hilbert.reset();
        cur = 0;

        for (auto& voice : outs_)
            for (auto& o : voice) o = 0.0f;

        for (int st = 0; st < numStages; ++st)
            for (int k = 0; k < maxBands; ++k)
                sRe[st][k] = sIm[st][k] = 0.0f;

        for (int k = 0; k < maxBands + 2 * pad; ++k)
        {
            magP[k] = 0.0f;
            for (int b = 0; b < 2; ++b)
            {
                halfCos[b][k] = 1.0f; halfSin[b][k] = 0.0f;
                quartCos[b][k] = 1.0f; quartSin[b][k] = 0.0f;
                detCos[b][k] = 1.0f; detSin[b][k] = 0.0f;
            }
        }
    }

    // How sharp the detuned copies of the octave-up voices are, in cents.
    // Cheap enough to call once per audio block.
    void setDetuneCents (float cents) noexcept
    {
        if (std::abs (cents - detuneCents) < 1.0e-4f)
            return;
        detuneCents = cents;

        const double ratio = std::pow (2.0, (double) cents / 1200.0) - 1.0;
        for (int k = 0; k < numBands; ++k)
        {
            // Extra rotation per sample that turns the octave-up pitch (twice
            // the band's own pitch) sharp by the requested amount.
            const double w = 6.283185307179586 * 2.0 * centreHz[k] * ratio / sr;
            detRotCos[k] = (float) std::cos (w);
            detRotSin[k] = (float) std::sin (w);
        }
    }

    // Tell the engine which voices are actually being listened to, so it can
    // skip the work for the rest. Skipped voices output silence.
    void setActiveVoices (const bool active[numVoices]) noexcept
    {
        const bool half = active[down1] || active[fifthUp];
        const bool quarter = active[down2] || active[fourthDown];
        const bool detune = active[up1Detuned] || active[up2Detuned];

        // Clear what a group left behind so a skipped voice is truly silent.
        if (needHalf && ! (half || quarter)) { clearVoice (down1); clearVoice (fifthUp); }
        if (needQuarter && ! quarter)        { clearVoice (down2); clearVoice (fourthDown); }
        if (needDetune && ! detune)          { clearVoice (up1Detuned); clearVoice (up2Detuned); }

        needHalf = half || quarter;   // the quarter phase is built from the half phase
        needQuarter = quarter;
        needDetune = detune;
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
            // arrays, without branches, so the compiler can process several
            // bands per instruction. Keep them that way: it is a 4x speed-up.
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

            // Amplitude of each band.
            float* const mag = magP + pad;
            for (int k = 0; k < nb; ++k)
                mag[k] = std::sqrt (bandRe[k] * bandRe[k] + bandIm[k] * bandIm[k]);

            // A note shows up strongly in the band nearest to it and weakly in the
            // bands around it. Only the strong bands have a trustworthy phase, so
            // fade each band out relative to its loudest neighbour. This is a big
            // part of what keeps chords clean.
            for (int k = 0; k < nb; ++k)
            {
                float peak = magP[k];
                for (int j = 1; j <= 2 * sharpenReach; ++j)
                    peak = std::max (peak, magP[k + j]);
                peakMag[k] = peak;
            }

            // Unit phase (cos, sin) and sharpened amplitude of each band.
            for (int k = 0; k < nb; ++k)
            {
                const float inv = 1.0f / (mag[k] + 1.0e-30f);
                cosA[k] = bandRe[k] * inv;
                sinA[k] = bandIm[k] * inv;
                float rel = mag[k] / (peakMag[k] + 1.0e-30f);
                rel *= rel; rel *= rel;
                ampA[k] = mag[k] * rel;
            }

            // Whole-number multiples of the phase need no memory at all.
            {
                float* PUPPYCHOW_RESTRICT const oUp1 = outs_[up1];
                float* PUPPYCHOW_RESTRICT const oUp2 = outs_[up2];
                float* PUPPYCHOW_RESTRICT const oOctFifth = outs_[octFifthUp];

                for (int k = 0; k < nb; ++k)
                {
                    const float c = cosA[k], s = sinA[k], amp = ampA[k];
                    const float c2 = c * c - s * s, s2 = 2.0f * c * s;           // x2: octave up
                    cos2A[k] = c2; sin2A[k] = s2;
                    oUp1[k] = gainUp1[k] * amp * c2;
                    oUp2[k] = gainUp2[k] * amp * (2.0f * c2 * c2 - 1.0f);         // x4: two octaves up
                    oOctFifth[k] = gainOctFifth[k] * amp * (4.0f * c * c * c - 3.0f * c); // x3
                }
            }

            // A note is spread over a few neighbouring bands (a "hill"). For the
            // voices below that need memory, every band copies the choice made by
            // its louder neighbour, so the whole hill agrees and adds up instead
            // of cancelling. followA says which neighbour: -1 left, +1 right, 0 none.
            for (int k = 0; k < nb; ++k)
            {
                const float left = mag[k - 1], right = mag[k + 1], self = mag[k];
                const float goLeft = (left > self ? 1.0f : 0.0f) * (left >= right ? 1.0f : 0.0f);
                const float goRight = (right > self ? 1.0f : 0.0f) * (1.0f - goLeft);
                followL[k] = goLeft;
                followR[k] = goRight;
            }

            // Per-band state from the previous sample (read) and for this one (write).
            const int prv = cur, nxt = 1 - cur;
            cur = nxt;

            // Octave down: halve the phase. Half a phase is only known up to a
            // sign, so keep the sign that continues smoothly from the previous
            // sample (or from the louder neighbour). A fifth up is three halves.
            if (needHalf)
            {
                const float* PUPPYCHOW_RESTRICT const pC = halfCos[prv] + pad;
                const float* PUPPYCHOW_RESTRICT const pS = halfSin[prv] + pad;
                float* PUPPYCHOW_RESTRICT const nC = halfCos[nxt] + pad;
                float* PUPPYCHOW_RESTRICT const nS = halfSin[nxt] + pad;
                float* PUPPYCHOW_RESTRICT const oDown1 = outs_[down1];
                float* PUPPYCHOW_RESTRICT const oFifth = outs_[fifthUp];

                for (int k = 0; k < nb; ++k)
                {
                    const float c = cosA[k], s = sinA[k], amp = ampA[k];
                    const float hc0 = std::sqrt (std::max (0.0f, 0.5f * (1.0f + c)));
                    const float hs0 = (s >= 0.0f ? 1.0f : -1.0f) * std::sqrt (std::max (0.0f, 0.5f * (1.0f - c)));
                    const float keep = 1.0f - followL[k] - followR[k];
                    const float refC = followL[k] * pC[k - 1] + followR[k] * pC[k + 1] + keep * pC[k];
                    const float refS = followL[k] * pS[k - 1] + followR[k] * pS[k + 1] + keep * pS[k];
                    const float sign = (hc0 * refC + hs0 * refS) >= 0.0f ? 1.0f : -1.0f;
                    const float hc = sign * hc0, hs = sign * hs0;
                    nC[k] = hc; nS[k] = hs;
                    oDown1[k] = amp * hc;
                    oFifth[k] = gainFifth[k] * amp * (4.0f * hc * hc * hc - 3.0f * hc);
                }
            }

            // Two octaves down: halve again, same idea. A fourth down is three quarters.
            if (needQuarter)
            {
                const float* PUPPYCHOW_RESTRICT const hC = halfCos[nxt] + pad;
                const float* PUPPYCHOW_RESTRICT const hS = halfSin[nxt] + pad;
                const float* PUPPYCHOW_RESTRICT const pC = quartCos[prv] + pad;
                const float* PUPPYCHOW_RESTRICT const pS = quartSin[prv] + pad;
                float* PUPPYCHOW_RESTRICT const nC = quartCos[nxt] + pad;
                float* PUPPYCHOW_RESTRICT const nS = quartSin[nxt] + pad;
                float* PUPPYCHOW_RESTRICT const oDown2 = outs_[down2];
                float* PUPPYCHOW_RESTRICT const oFourth = outs_[fourthDown];

                for (int k = 0; k < nb; ++k)
                {
                    const float hc = hC[k], hs = hS[k], amp = ampA[k];
                    const float qc0 = std::sqrt (std::max (0.0f, 0.5f * (1.0f + hc)));
                    const float qs0 = (hs >= 0.0f ? 1.0f : -1.0f) * std::sqrt (std::max (0.0f, 0.5f * (1.0f - hc)));
                    const float keep = 1.0f - followL[k] - followR[k];
                    const float refC = followL[k] * pC[k - 1] + followR[k] * pC[k + 1] + keep * pC[k];
                    const float refS = followL[k] * pS[k - 1] + followR[k] * pS[k + 1] + keep * pS[k];
                    const float sign = (qc0 * refC + qs0 * refS) >= 0.0f ? 1.0f : -1.0f;
                    const float qc = sign * qc0, qs = sign * qs0;
                    nC[k] = qc; nS[k] = qs;
                    oDown2[k] = amp * qc;
                    oFourth[k] = amp * (4.0f * qc * qc * qc - 3.0f * qc);
                }
            }

            // Detune: a slowly turning extra phase makes slightly sharp copies of
            // the octave-up voices.
            if (needDetune)
            {
                const float* PUPPYCHOW_RESTRICT const pC = detCos[prv] + pad;
                const float* PUPPYCHOW_RESTRICT const pS = detSin[prv] + pad;
                float* PUPPYCHOW_RESTRICT const nC = detCos[nxt] + pad;
                float* PUPPYCHOW_RESTRICT const nS = detSin[nxt] + pad;
                float* PUPPYCHOW_RESTRICT const oUp1Det = outs_[up1Detuned];
                float* PUPPYCHOW_RESTRICT const oUp2Det = outs_[up2Detuned];

                for (int k = 0; k < nb; ++k)
                {
                    const float keep = 1.0f - followL[k] - followR[k];
                    const float baseC = followL[k] * pC[k - 1] + followR[k] * pC[k + 1] + keep * pC[k];
                    const float baseS = followL[k] * pS[k - 1] + followR[k] * pS[k + 1] + keep * pS[k];
                    float dc = baseC * detRotCos[k] - baseS * detRotSin[k];
                    float ds = baseC * detRotSin[k] + baseS * detRotCos[k];
                    const float fix = 1.5f - 0.5f * (dc * dc + ds * ds);         // keep its length at 1
                    dc *= fix; ds *= fix;
                    nC[k] = dc; nS[k] = ds;

                    const float c2 = cos2A[k], s2 = sin2A[k], amp = ampA[k];
                    const float c4 = 2.0f * c2 * c2 - 1.0f, s4 = 2.0f * c2 * s2;
                    oUp1Det[k] = gainUp1[k] * amp * (c2 * dc - s2 * ds);
                    oUp2Det[k] = gainUp2[k] * amp * (c4 * (dc * dc - ds * ds) - s4 * (2.0f * dc * ds));
                }
            }

            // Add the bands up, eight at a time (slots past the last band are zero).
            const int nb8 = (nb + 7) & ~7;
            for (int v = 0; v < numVoices; ++v)
            {
                const float* const o = outs_[v];
                float acc[8] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
                for (int k = 0; k < nb8; k += 8)
                    for (int j = 0; j < 8; ++j)
                        acc[j] += o[k + j];
                outs[v][i] = (((acc[0] + acc[1]) + (acc[2] + acc[3])) + ((acc[4] + acc[5]) + (acc[6] + acc[7]))) * norm[(size_t) v];
            }
        }
    }

private:
    void clearVoice (int v) noexcept
    {
        for (auto& o : outs_[v]) o = 0.0f;
    }

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
    // Play a steady sine through the engine, measure each voice, and scale it
    // so its level matches the input level.
    void calibrate()
    {
        for (auto& n : norm) n = 1.0f;
        reset();

        const int total = (int) (0.8 * sr), settle = total / 2;
        float buf[numVoices][64];
        float* outs[numVoices];
        for (int v = 0; v < numVoices; ++v) outs[v] = buf[v];
        float in[64];
        double power[numVoices] = {};
        int counted = 0;

        for (int n = 0; n < total; n += 64)
        {
            for (int i = 0; i < 64; ++i)
                in[i] = (float) std::sin (6.283185307179586 * 330.0 * (n + i) / sr);
            process (in, outs, 64);
            if (n >= settle)
            {
                counted += 64;
                for (int v = 0; v < numVoices; ++v)
                    for (int i = 0; i < 64; ++i)
                        power[v] += (double) buf[v][i] * (double) buf[v][i];
            }
        }

        // A sine of amplitude 1 has an average power of one half.
        for (int v = 0; v < numVoices; ++v)
            norm[(size_t) v] = (float) (1.0 / std::max (std::sqrt (2.0 * power[v] / counted), 1.0e-6));
    }

    double sr = 48000.0;
    int numBands = 0;
    int cur = 0;
    bool needHalf = true, needQuarter = true, needDetune = true;
    float detuneCents = -1.0f;
    std::array<float, numVoices> norm {};

    alignas (32) float poleRe[maxBands] {}, poleIm[maxBands] {}, inRe[maxBands] {}, inIm[maxBands] {};
    alignas (32) float gainFifth[maxBands] {}, gainUp1[maxBands] {}, gainOctFifth[maxBands] {}, gainUp2[maxBands] {};
    alignas (32) float centreHz[maxBands] {}, detRotCos[maxBands] {}, detRotSin[maxBands] {};
    alignas (32) float sRe[numStages][maxBands] {}, sIm[numStages][maxBands] {};
    alignas (32) float bandRe[maxBands] {}, bandIm[maxBands] {}, peakMag[maxBands] {};
    alignas (32) float cosA[maxBands] {}, sinA[maxBands] {}, ampA[maxBands] {}, cos2A[maxBands] {}, sin2A[maxBands] {};
    alignas (32) float followL[maxBands] {}, followR[maxBands] {};
    alignas (32) float magP[maxBands + 2 * pad] {};
    alignas (32) float halfCos[2][maxBands + 2 * pad] {}, halfSin[2][maxBands + 2 * pad] {};
    alignas (32) float quartCos[2][maxBands + 2 * pad] {}, quartSin[2][maxBands + 2 * pad] {};
    alignas (32) float detCos[2][maxBands + 2 * pad] {}, detSin[2][maxBands + 2 * pad] {};
    alignas (32) float outs_[numVoices][maxBands] {};
};

} // namespace puppychow
