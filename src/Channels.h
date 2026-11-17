#pragma once

#include "dsp/OctaveEngine.h"

// The eight mixer channels, in the order they appear on screen (lowest pitch
// on the left). engineVoice is -1 for Dry, which bypasses the engine.
namespace puppychow
{
    struct Channel
    {
        const char* id;        // level parameter id; the on/off parameter is id + "_on"
        const char* name;      // name shown to the host
        const char* title;     // short title on the interface
        int engineVoice;
        float defaultDb;
        bool defaultOn;
    };

    inline constexpr int numChannels = 8;
    inline constexpr int dryChannel = 3;

    inline constexpr Channel channels[numChannels] = {
        { "down2",    "-2 Octaves",        "-2 Oct",   OctaveEngine::down2,      -6.0f, false },
        { "down1",    "-1 Octave",         "-1 Oct",   OctaveEngine::down1,      -6.0f, true  },
        { "fourth",   "-4th",              "-4th",     OctaveEngine::fourthDown, -6.0f, false },
        { "dry",      "Dry",               "Dry",      -1,                        0.0f, true  },
        { "fifth",    "+5th",              "+5th",     OctaveEngine::fifthUp,    -6.0f, false },
        { "up1",      "+1 Octave",         "+1 Oct",   OctaveEngine::up1,        -6.0f, true  },
        { "octfifth", "+1 Octave +5th",    "+Oct 5th", OctaveEngine::octFifthUp, -6.0f, false },
        { "up2",      "+2 Octaves",        "+2 Oct",   OctaveEngine::up2,        -6.0f, false },
    };

    inline constexpr float minLevelDb = -60.0f;   // bottom of a level slider is silent
    inline constexpr float maxDetuneCents = 30.0f;
    inline constexpr float maxSwellMs = 1000.0f;
    inline constexpr float minOutputDb = -24.0f, maxOutputDb = 6.0f;
}
