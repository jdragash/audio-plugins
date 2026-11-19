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

// Tells the compiler the per-band loops have no overlapping arrays, so it can
// process several bands per instruction.
#if defined (__clang__)
 #define PUPPYCHOW_VECTORISE _Pragma ("clang loop vectorize(assume_safety)")
#elif defined (__GNUC__)
 #define PUPPYCHOW_VECTORISE _Pragma ("GCC ivdep")
#else
 #define PUPPYCHOW_VECTORISE
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
    static constexpr double turnGlideSeconds = 0.003;   // how fast a band re-aligns with its neighbour

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
        overtones,      // "ghost partials": harmonics 7, 9, 11 and 13, blooming in (see below)
        numVoices
    };

    static constexpr double voiceRatio[numVoices] = { 0.25, 0.5, 0.75, 1.5, 2.0, 3.0, 4.0, 2.0, 4.0, 13.0 };

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

            // Overtones ("ghost partials"): the 7th, 9th, 11th and 13th
            // harmonics of every note. Three of them fall between the notes of
            // a piano (the 7th is a flat "blue" seventh, the 11th sits between
            // a fourth and a tritone, the 13th near a flat sixth), so they add
            // a shimmer no fretted note can make.
            overW7[k]  = (7.0 * f < nyquistGuard)  ? overtoneWeight[0] : 0.0f;
            overW9[k]  = (9.0 * f < nyquistGuard)  ? overtoneWeight[1] : 0.0f;
            overW11[k] = (11.0 * f < nyquistGuard) ? overtoneWeight[2] : 0.0f;
            overW13[k] = (13.0 * f < nyquistGuard) ? overtoneWeight[3] : 0.0f;

            f += t.spacing * width;
        }

        hilbert.prepare (sampleRate);
        for (int v = 0; v < numVoices; ++v)
            smoothing[(size_t) v].design (std::min (1.2 * (double) t.highestHz * voiceRatio[v], 0.45 * sampleRate), sampleRate);
        turnGlide = (float) (1.0 - std::exp (-1.0 / (turnGlideSeconds * sampleRate)));
        bloomRise = (float) (1.0 - std::exp (-1.0 / (bloomRiseSeconds * sampleRate)));
        bloomFall = (float) (1.0 - std::exp (-1.0 / (bloomFallSeconds * sampleRate)));
        detuneCents = -1.0f;
        setDetuneCents (0.0f);
        calibrate();
        reset();
    }

    void reset()
    {
        hilbert.reset();
        cur = 0;
        for (auto& lp : smoothing) lp.reset();

        for (auto& voice : outs_)
            for (auto& o : voice) o = 0.0f;

        for (int st = 0; st < numStages; ++st)
            for (int k = 0; k < maxBands; ++k)
                sRe[st][k] = sIm[st][k] = 0.0f;
        for (auto& b : overBloom) b = 0.0f;

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

        for (int k = 0; k < maxBands; ++k)
        {
            for (int b = 0; b < 2; ++b)
            {
                ownHalfCos[b][k] = 1.0f; ownHalfSin[b][k] = 0.0f;
                ownQuartCos[b][k] = 1.0f; ownQuartSin[b][k] = 0.0f;
                ownDetCos[b][k] = 1.0f; ownDetSin[b][k] = 0.0f;
            }
            turnHalf[k] = 1.0f;
            turnQuartRe[k] = 1.0f; turnQuartIm[k] = 0.0f;
            turnDetRe[k] = 1.0f; turnDetIm[k] = 0.0f;
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
        const bool over = active[overtones];

        // Clear what a group left behind so a skipped voice is truly silent.
        if (needHalf && ! (half || quarter)) { clearVoice (down1); clearVoice (fifthUp); }
        if (needQuarter && ! quarter)        { clearVoice (down2); clearVoice (fourthDown); }
        if (needDetune && ! detune)          { clearVoice (up1Detuned); clearVoice (up2Detuned); }
        if (needOvertones && ! over)         { clearVoice (overtones); }

        needHalf = half || quarter;   // the quarter phase is built from the half phase
        needQuarter = quarter;
        needDetune = detune;
        needOvertones = over;
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
                // A band that has faded to (almost) nothing gets a plain unit
                // phase and no level. This must not divide by its tiny level:
                // with denormals flushed to zero (as in every DAW), the squares
                // in mag underflow to 0 while the band itself is still around
                // 1e-20, so "unit" phases came out huge, overflowed in the x4
                // voice and turned into NaN, silencing the whole output.
                const bool live = mag[k] > 1.0e-15f;
                const float inv = live ? 1.0f / mag[k] : 0.0f;
                cosA[k] = live ? bandRe[k] * inv : 1.0f;
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

            // Overtones: cos(n x phase) for odd n up to 13, built two steps at
            // a time from the x2 phase: T(n+2) = 2 T2 T(n) - T(n-2). Each band's
            // level "blooms": it rises over about 150 ms and falls quickly, so
            // the partials grow out of a note instead of arriving with the pick.
            if (needOvertones)
            {
                float* PUPPYCHOW_RESTRICT const oOver = outs_[overtones];
                float* PUPPYCHOW_RESTRICT const bloom = overBloom;
                const float* PUPPYCHOW_RESTRICT const cP = cosA;
                const float* PUPPYCHOW_RESTRICT const c2P = cos2A;
                const float* PUPPYCHOW_RESTRICT const aP = ampA;
                const float* PUPPYCHOW_RESTRICT const w7 = overW7;
                const float* PUPPYCHOW_RESTRICT const w9 = overW9;
                const float* PUPPYCHOW_RESTRICT const w11 = overW11;
                const float* PUPPYCHOW_RESTRICT const w13 = overW13;
                const float rise = bloomRise, fall = bloomFall;

                PUPPYCHOW_VECTORISE
                for (int k = 0; k < nb; ++k)
                {
                    const float c = cP[k], t2x = 2.0f * c2P[k];
                    const float t3 = (t2x - 1.0f) * c;            // T3 = (2 T2 - 1) c
                    const float t5 = t2x * t3 - c;
                    const float t7 = t2x * t5 - t3;
                    const float t9 = t2x * t7 - t5;
                    const float t11 = t2x * t9 - t7;
                    const float t13 = t2x * t11 - t9;

                    const float a = aP[k], b = bloom[k];
                    float nb_ = b + (a > b ? rise : fall) * (a - b);
                    nb_ = nb_ < 1.0e-12f ? 0.0f : nb_;
                    bloom[k] = nb_;
                    oOver[k] = nb_ * (w7[k] * t7 + w9[k] * t9 + w11[k] * t11 + w13[k] * t13);
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

            // The voices below need memory, and each has an ambiguity: half a
            // phase is only known up to a sign, a quarter up to a quarter turn,
            // and the detune offset is arbitrary. Each band handles this in two
            // layers:
            //   own:  the band's own phasor, always continuous with its own
            //         previous sample, so it never jumps.
            //   turn: a rotation applied on top so the band agrees with its
            //         louder neighbour (a note is spread over a few neighbouring
            //         bands and they must add up, not cancel). When the wanted
            //         rotation changes, the band glides to it over a few ms
            //         instead of jumping, which would click.
            // eff (own x wanted rotation) is what neighbours look at.

            // Octave down: halve the phase. A fifth up is three halves.
            if (needHalf)
            {
                const float* PUPPYCHOW_RESTRICT const pEffC = halfCos[prv] + pad;
                const float* PUPPYCHOW_RESTRICT const pEffS = halfSin[prv] + pad;
                const float* PUPPYCHOW_RESTRICT const pOwnC = ownHalfCos[prv];
                const float* PUPPYCHOW_RESTRICT const pOwnS = ownHalfSin[prv];
                float* PUPPYCHOW_RESTRICT const nEffC = halfCos[nxt] + pad;
                float* PUPPYCHOW_RESTRICT const nEffS = halfSin[nxt] + pad;
                float* PUPPYCHOW_RESTRICT const nOwnC = ownHalfCos[nxt];
                float* PUPPYCHOW_RESTRICT const nOwnS = ownHalfSin[nxt];
                float* PUPPYCHOW_RESTRICT const turn = turnHalf;
                float* PUPPYCHOW_RESTRICT const oDown1 = outs_[down1];
                float* PUPPYCHOW_RESTRICT const oFifth = outs_[fifthUp];
                const float glide = turnGlide;

                for (int k = 0; k < nb; ++k)
                {
                    const float c = cosA[k], s = sinA[k], amp = ampA[k];
                    const float hc0 = std::sqrt (std::max (0.0f, 0.5f * (1.0f + c)));
                    const float hs0 = (s >= 0.0f ? 1.0f : -1.0f) * std::sqrt (std::max (0.0f, 0.5f * (1.0f - c)));
                    const float ownSign = (hc0 * pOwnC[k] + hs0 * pOwnS[k]) >= 0.0f ? 1.0f : -1.0f;
                    const float oc = ownSign * hc0, os = ownSign * hs0;
                    nOwnC[k] = oc; nOwnS[k] = os;

                    const float keep = 1.0f - followL[k] - followR[k];
                    const float refC = followL[k] * pEffC[k - 1] + followR[k] * pEffC[k + 1] + keep * pEffC[k];
                    const float refS = followL[k] * pEffS[k - 1] + followR[k] * pEffS[k + 1] + keep * pEffS[k];
                    const float want = (oc * refC + os * refS) >= 0.0f ? 1.0f : -1.0f;
                    nEffC[k] = want * oc; nEffS[k] = want * os;

                    const float t = turn[k] + glide * (want - turn[k]);
                    turn[k] = t;
                    const float hc = t * oc;
                    oDown1[k] = amp * hc;
                    oFifth[k] = gainFifth[k] * amp * t * t * t * (4.0f * oc * oc * oc - 3.0f * oc);
                }
            }

            // Two octaves down: halve the band's own half phase again. A fourth
            // down is three quarters. Here the rotation is a quarter turn step.
            if (needQuarter)
            {
                const float* PUPPYCHOW_RESTRICT const hOwnC = ownHalfCos[nxt];
                const float* PUPPYCHOW_RESTRICT const hOwnS = ownHalfSin[nxt];
                const float* PUPPYCHOW_RESTRICT const pEffC = quartCos[prv] + pad;
                const float* PUPPYCHOW_RESTRICT const pEffS = quartSin[prv] + pad;
                const float* PUPPYCHOW_RESTRICT const pOwnC = ownQuartCos[prv];
                const float* PUPPYCHOW_RESTRICT const pOwnS = ownQuartSin[prv];
                float* PUPPYCHOW_RESTRICT const nEffC = quartCos[nxt] + pad;
                float* PUPPYCHOW_RESTRICT const nEffS = quartSin[nxt] + pad;
                float* PUPPYCHOW_RESTRICT const nOwnC = ownQuartCos[nxt];
                float* PUPPYCHOW_RESTRICT const nOwnS = ownQuartSin[nxt];
                float* PUPPYCHOW_RESTRICT const turnRe = turnQuartRe;
                float* PUPPYCHOW_RESTRICT const turnIm = turnQuartIm;
                float* PUPPYCHOW_RESTRICT const oDown2 = outs_[down2];
                float* PUPPYCHOW_RESTRICT const oFourth = outs_[fourthDown];
                const float glide = turnGlide;

                for (int k = 0; k < nb; ++k)
                {
                    const float hc = hOwnC[k], hs = hOwnS[k], amp = ampA[k];
                    const float qc0 = std::sqrt (std::max (0.0f, 0.5f * (1.0f + hc)));
                    const float qs0 = (hs >= 0.0f ? 1.0f : -1.0f) * std::sqrt (std::max (0.0f, 0.5f * (1.0f - hc)));
                    const float ownSign = (qc0 * pOwnC[k] + qs0 * pOwnS[k]) >= 0.0f ? 1.0f : -1.0f;
                    const float qc = ownSign * qc0, qs = ownSign * qs0;
                    nOwnC[k] = qc; nOwnS[k] = qs;

                    const float keep = 1.0f - followL[k] - followR[k];
                    const float refC = followL[k] * pEffC[k - 1] + followR[k] * pEffC[k + 1] + keep * pEffC[k];
                    const float refS = followL[k] * pEffS[k - 1] + followR[k] * pEffS[k + 1] + keep * pEffS[k];

                    // Wanted rotation: ref / own, snapped to the nearest quarter turn.
                    const float zr = refC * qc + refS * qs, zi = refS * qc - refC * qs;
                    const float useRe = std::abs (zr) >= std::abs (zi) ? 1.0f : 0.0f;
                    const float wr = useRe * (zr >= 0.0f ? 1.0f : -1.0f);
                    const float wi = (1.0f - useRe) * (zi >= 0.0f ? 1.0f : -1.0f);
                    nEffC[k] = qc * wr - qs * wi; nEffS[k] = qc * wi + qs * wr;

                    // Glide towards the wanted rotation. Values that are gliding to
                    // zero are snapped once tiny, to avoid slow denormal numbers.
                    float tr = turnRe[k] + glide * (wr - turnRe[k]);
                    float ti = turnIm[k] + glide * (wi - turnIm[k]);
                    tr = std::abs (tr) < 1.0e-6f ? 0.0f : tr;
                    ti = std::abs (ti) < 1.0e-6f ? 0.0f : ti;
                    turnRe[k] = tr; turnIm[k] = ti;
                    const float oc = qc * tr - qs * ti, os = qc * ti + qs * tr;
                    oDown2[k] = amp * oc;
                    oFourth[k] = amp * (oc * oc * oc - 3.0f * oc * os * os);
                }
            }

            // Detune: a slowly turning extra phase makes slightly sharp copies of
            // the octave-up voices. Same own/turn idea, with a free rotation.
            if (needDetune)
            {
                const float* PUPPYCHOW_RESTRICT const pEffC = detCos[prv] + pad;
                const float* PUPPYCHOW_RESTRICT const pEffS = detSin[prv] + pad;
                const float* PUPPYCHOW_RESTRICT const pOwnC = ownDetCos[prv];
                const float* PUPPYCHOW_RESTRICT const pOwnS = ownDetSin[prv];
                float* PUPPYCHOW_RESTRICT const nEffC = detCos[nxt] + pad;
                float* PUPPYCHOW_RESTRICT const nEffS = detSin[nxt] + pad;
                float* PUPPYCHOW_RESTRICT const nOwnC = ownDetCos[nxt];
                float* PUPPYCHOW_RESTRICT const nOwnS = ownDetSin[nxt];
                float* PUPPYCHOW_RESTRICT const turnRe = turnDetRe;
                float* PUPPYCHOW_RESTRICT const turnIm = turnDetIm;
                float* PUPPYCHOW_RESTRICT const oUp1Det = outs_[up1Detuned];
                float* PUPPYCHOW_RESTRICT const oUp2Det = outs_[up2Detuned];
                const float* PUPPYCHOW_RESTRICT const rotC = detRotCos;
                const float* PUPPYCHOW_RESTRICT const rotS = detRotSin;
                const float* PUPPYCHOW_RESTRICT const g1 = gainUp1;
                const float* PUPPYCHOW_RESTRICT const g2 = gainUp2;
                const float* PUPPYCHOW_RESTRICT const fL = followL;
                const float* PUPPYCHOW_RESTRICT const fR = followR;
                const float* PUPPYCHOW_RESTRICT const ampP = ampA;
                const float* PUPPYCHOW_RESTRICT const c2P = cos2A;
                const float* PUPPYCHOW_RESTRICT const s2P = sin2A;
                const float glide = turnGlide;

                PUPPYCHOW_VECTORISE
                for (int k = 0; k < nb; ++k)
                {
                    const float pc = pOwnC[k], ps = pOwnS[k];
                    float dc = pc * rotC[k] - ps * rotS[k];
                    float ds = pc * rotS[k] + ps * rotC[k];
                    const float fix = 1.5f - 0.5f * (dc * dc + ds * ds);         // keep its length at 1
                    dc *= fix; ds *= fix;
                    nOwnC[k] = dc; nOwnS[k] = ds;

                    // Wanted rotation: neighbour's previous eff / own previous phasor.
                    const float keep = 1.0f - fL[k] - fR[k];
                    const float refC = fL[k] * pEffC[k - 1] + fR[k] * pEffC[k + 1] + keep * pEffC[k];
                    const float refS = fL[k] * pEffS[k - 1] + fR[k] * pEffS[k + 1] + keep * pEffS[k];
                    float wr = refC * pc + refS * ps, wi = refS * pc - refC * ps;
                    const float wInv = 1.0f / (std::sqrt (wr * wr + wi * wi) + 1.0e-12f);
                    wr *= wInv; wi *= wInv;
                    nEffC[k] = dc * wr - ds * wi; nEffS[k] = dc * wi + ds * wr;

                    float tr = turnRe[k] + glide * (wr - turnRe[k]);
                    float ti = turnIm[k] + glide * (wi - turnIm[k]);
                    tr = std::abs (tr) < 1.0e-6f ? 0.0f : tr;
                    ti = std::abs (ti) < 1.0e-6f ? 0.0f : ti;
                    turnRe[k] = tr; turnIm[k] = ti;
                    const float hc = dc * tr - ds * ti, hs = dc * ti + ds * tr;

                    const float c2 = c2P[k], s2 = s2P[k], amp = ampP[k];
                    const float c4 = 2.0f * c2 * c2 - 1.0f, s4 = 2.0f * c2 * s2;
                    oUp1Det[k] = g1[k] * amp * (c2 * hc - s2 * hs);
                    oUp2Det[k] = g2[k] * amp * (c4 * (hc * hc - hs * hs) - s4 * (2.0f * hc * hs));
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
                const float sum = (((acc[0] + acc[1]) + (acc[2] + acc[3])) + ((acc[4] + acc[5]) + (acc[6] + acc[7]))) * norm[(size_t) v];
                outs[v][i] = smoothing[(size_t) v].process (sum);
            }
        }
    }

private:
    // Fourth-order low-pass (two Butterworth sections). Each voice can only
    // legitimately contain frequencies up to (highest band) x (its pitch
    // ratio). Anything above that is fizz from fast phase wobbles during pick
    // attacks and beating notes, which a high-gain amp turns into crackle.
    struct LowPass
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z[2][2] {};

        void design (double cutoffHz, double sampleRate)
        {
            const double w0 = 6.283185307179586 * cutoffHz / sampleRate;
            const double alpha = std::sin (w0) / (2.0 * 0.7071067811865476);
            const double cw = std::cos (w0), a0 = 1.0 + alpha;
            b0 = (float) ((1.0 - cw) * 0.5 / a0); b1 = (float) ((1.0 - cw) / a0); b2 = b0;
            a1 = (float) (-2.0 * cw / a0); a2 = (float) ((1.0 - alpha) / a0);
            reset();
        }

        void reset() { z[0][0] = z[0][1] = z[1][0] = z[1][1] = 0.0f; }

        float process (float x) noexcept
        {
            for (auto& st : z)
            {
                // Transposed direct form II.
                const float y = b0 * x + st[0];
                st[0] = b1 * x - a1 * y + st[1];
                st[1] = b2 * x - a2 * y;
                x = y;
            }
            return x;
        }
    };

    std::array<LowPass, numVoices> smoothing;

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

        // Measure every voice, even ones switched off right now (a voice that
        // was skipped here would get a huge level once switched on).
        const bool keepHalf = needHalf, keepQuarter = needQuarter, keepDetune = needDetune, keepOver = needOvertones;
        needHalf = needQuarter = needDetune = needOvertones = true;
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

        needHalf = keepHalf; needQuarter = keepQuarter; needDetune = keepDetune; needOvertones = keepOver;
    }

    double sr = 48000.0;
    int numBands = 0;
    int cur = 0;
    float turnGlide = 0.01f;   // per-sample glide factor for band rotations
    bool needHalf = true, needQuarter = true, needDetune = true, needOvertones = true;
    float detuneCents = -1.0f;
    std::array<float, numVoices> norm {};

    alignas (32) float poleRe[maxBands] {}, poleIm[maxBands] {}, inRe[maxBands] {}, inIm[maxBands] {};
    static constexpr float overtoneWeight[4] = { 0.5f, 0.35f, 0.3f, 0.2f };   // harmonics 7, 9, 11, 13
    static constexpr double bloomRiseSeconds = 0.15, bloomFallSeconds = 0.03;
    float bloomRise = 0.0f, bloomFall = 0.0f;
    alignas (32) float overW7[maxBands] {}, overW9[maxBands] {}, overW11[maxBands] {}, overW13[maxBands] {};
    alignas (32) float overBloom[maxBands] {};
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
    alignas (32) float ownHalfCos[2][maxBands] {}, ownHalfSin[2][maxBands] {};
    alignas (32) float ownQuartCos[2][maxBands] {}, ownQuartSin[2][maxBands] {};
    alignas (32) float ownDetCos[2][maxBands] {}, ownDetSin[2][maxBands] {};
    alignas (32) float turnHalf[maxBands] {}, turnQuartRe[maxBands] {}, turnQuartIm[maxBands] {};
    alignas (32) float turnDetRe[maxBands] {}, turnDetIm[maxBands] {};
    alignas (32) float outs_[numVoices][maxBands] {};
};

} // namespace puppychow
