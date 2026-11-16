# Puppy Chow

A polyphonic octave pedal for guitar, as a Mac Audio Unit (plus VST3 and a standalone app).
It's meant to sit in front of your amp sim and add clean, chord-friendly octaves up and down.

## Status

**Milestone 2: the octave engine.** Five knobs: Dry, one and two octaves down, one and two
octaves up. Turn a knob fully down to switch that voice off. The interface is a placeholder.

Still to come: a Tone filter, and a proper interface.

Design goals:
- Feels like a pedal, not a delay. Your dry signal is never delayed. The octave voices fade in
  over a few milliseconds for high notes and a few tens of milliseconds for the lowest notes.
- Tracks chords cleanly, with no pitch detection.
- Light on CPU, and safe to run on the real-time audio thread.

## How it works

The guitar signal is split into about 140 very narrow frequency bands, so each band holds
roughly one pure tone from one string. Each tone is then moved up or down by octaves on its
own and the results are added back together. Because nothing ever has to decide "which note
is this?", chords work as well as single notes. The engine lives in `src/dsp/OctaveEngine.h`
and has no dependencies. `tests/engine_tests.cpp` checks pitch accuracy, chord cleanliness,
attack time and CPU use.

## Building on a Mac

You need Xcode (or the Xcode command line tools), CMake and Ninja (`brew install cmake ninja`).

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build          # engine tests; add --verbose to see every measurement
```

The first configure downloads JUCE, so it takes a minute. Plugins end up in
`build/PuppyChow_artefacts/Release/`. To use the Audio Unit in your DAW, copy
`AU/Puppy Chow.component` into `~/Library/Audio/Plug-Ins/Components/`, then check it with:

```sh
auval -v aufx Pchw Jdrg
```

## Getting a build without compiling

Every push to `main` builds the plugin on GitHub Actions, runs the engine tests, and validates
it with `auval` and `pluginval`. Download the `PuppyChow-macOS` artifact from the latest run on
the Actions tab. Those builds are not code-signed, so macOS may block them the first time; run
`xattr -dr com.apple.quarantine "Puppy Chow.component"` after copying it in.

Built with [JUCE](https://juce.com).
