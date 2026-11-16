# Puppy Chow

A polyphonic octave pedal for guitar, as a Mac Audio Unit (plus VST3 and a standalone app).
It's meant to sit in front of your amp sim and add clean, chord-friendly octaves up and down.

## Status

**Milestone 1: pass-through.** The plugin loads, passes audio through unchanged, and has a single
Dry level knob. The octave engine comes next.

Planned controls: Dry, -1 octave, -2 octaves, +1 octave, +2 octaves, and a Tone filter.

Design goals:
- Low latency (under about 10 ms), so it feels like a pedal, not a delay.
- Tracks chords cleanly, with no pitch detection.
- Light on CPU, and safe to run on the real-time audio thread.

## Building on a Mac

You need Xcode (or the Xcode command line tools) and CMake (`brew install cmake ninja`).

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The first configure downloads JUCE, so it takes a minute. Plugins end up in
`build/PuppyChow_artefacts/Release/`. To use the Audio Unit in your DAW, copy
`AU/Puppy Chow.component` into `~/Library/Audio/Plug-Ins/Components/`, then check it with:

```sh
auval -v aufx Pchw Jdrg
```

## Getting a build without compiling

Every push to `main` builds the plugin on GitHub Actions and validates it with `auval` and
`pluginval`. Download the `PuppyChow-macOS` artifact from the latest run on the Actions tab.
Those builds are not code-signed, so macOS may block them the first time; run
`xattr -dr com.apple.quarantine "Puppy Chow.component"` after copying it in.

Built with [JUCE](https://juce.com).
