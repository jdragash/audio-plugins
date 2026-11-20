#pragma once

// Puppy Chow shimmer: a small, lush stereo reverb that channels can send to
// (the Shimmer option under a channel's fader). Four delay lines feed back
// into each other through a Householder mix, with a gentle darkening filter
// in the loop, after two all-pass diffusers that smear the attack. The tail
// lasts about two and a half seconds. Plain C++, real-time safe after
// prepare().

#include <cmath>
#include <vector>

namespace puppychow
{
class Shimmer
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        static constexpr double lineMs[numLines] = { 67.3, 79.1, 89.7, 101.3 };
        static constexpr double diffMs[2] = { 4.7, 7.1 };
        for (int l = 0; l < numLines; ++l)
            lines[l].assign ((size_t) std::lround (lineMs[l] * 0.001 * sr), 0.0f);
        for (int d = 0; d < 2; ++d)
            for (int ch = 0; ch < 2; ++ch)
                diffusers[ch][d].assign ((size_t) std::lround (diffMs[d] * (ch == 0 ? 1.0 : 1.13) * 0.001 * sr), 0.0f);

        dampCoeff = (float) std::exp (-6.283185307179586 * 6000.0 / sr);
        lowCutCoeff = (float) std::exp (-6.283185307179586 * 200.0 / sr);
        reset();
    }

    void reset()
    {
        for (auto& l : lines) std::fill (l.begin(), l.end(), 0.0f);
        for (auto& ch : diffusers) for (auto& d : ch) std::fill (d.begin(), d.end(), 0.0f);
        for (int l = 0; l < numLines; ++l) { pos[l] = 0; damp[l] = 0.0f; }
        for (auto& ch : diffPos) for (auto& p : ch) p = 0;
        lowCut[0] = lowCut[1] = 0.0f;
    }

    // Takes one stereo sample (what the channels send) and returns the reverb.
    void process (float inL, float inR, float& outL, float& outR) noexcept
    {
        // Low cut so the tail never gets muddy, then diffuse.
        float in[2] = { inL, inR };
        for (int ch = 0; ch < 2; ++ch)
        {
            lowCut[ch] = in[ch] + lowCutCoeff * (lowCut[ch] - in[ch]);
            float x = in[ch] - lowCut[ch];
            for (int d = 0; d < 2; ++d)
                x = allpass (diffusers[ch][d], diffPos[ch][d], x, 0.6f);
            in[ch] = x;
        }

        // Read the delay lines, mix them (Householder: each output is the line
        // minus half the sum) and write back with feedback and damping.
        float y[numLines];
        float sum = 0.0f;
        for (int l = 0; l < numLines; ++l)
        {
            y[l] = lines[l][(size_t) pos[l]];
            sum += y[l];
        }
        const float half = 0.5f * sum;
        for (int l = 0; l < numLines; ++l)
        {
            float fb = (y[l] - half) * feedback;
            damp[l] = fb + dampCoeff * (damp[l] - fb);
            fb = damp[l];
            fb = std::abs (fb) < 1.0e-12f ? 0.0f : fb;
            lines[l][(size_t) pos[l]] = fb + in[l & 1];
            if (++pos[l] >= (int) lines[l].size()) pos[l] = 0;
        }

        outL = 0.35f * (y[0] + y[2]);
        outR = 0.35f * (y[1] + y[3]);
    }

private:
    static constexpr int numLines = 4;
    static constexpr float feedback = 0.8f;

    static float allpass (std::vector<float>& buf, int& p, float x, float g) noexcept
    {
        const float delayed = buf[(size_t) p];
        const float v = x + g * delayed;
        buf[(size_t) p] = std::abs (v) < 1.0e-12f ? 0.0f : v;
        if (++p >= (int) buf.size()) p = 0;
        return delayed - g * v;
    }

    double sr = 48000.0;
    std::vector<float> lines[numLines];
    int pos[numLines] {};
    float damp[numLines] {};
    std::vector<float> diffusers[2][2];
    int diffPos[2][2] {};
    float dampCoeff = 0.0f, lowCutCoeff = 0.0f, lowCut[2] {};
};
} // namespace puppychow
