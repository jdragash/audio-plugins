# Puppy Chow

A polyphonic octave pedal for guitar, as a Mac Audio Unit (plus VST3 and a standalone app).
It's meant to sit in front of your amp sim and add clean, chord-friendly octaves up and down.

## Status

**Version 0.2.1.** Download it from the [Releases](https://github.com/jdragash/puppy-chow/releases)
page. The sound engine and controls are working; the interface is still a placeholder until
the real design.

![Placeholder interface](docs/interface.png)

- **Nine channels**, each with an on/off switch, a solo button, a pan knob and a level slider: two octaves down, one
  octave down, a fourth down, Dry, a fifth up, one octave up, an octave plus a fifth up,
  two octaves up, and Overtones.
- **Overtones** ("ghost partials") adds the 7th, 9th, 11th and 13th harmonics of what you play.
  They sit between the notes of a piano, so they shimmer in a way no fretted note can, and
  they bloom in after each pick.
- **Options under the faders**: *Fuzz* on the channels below Dry; *Shimmer* (a lush reverb
  tail) and *Crush* (lo-fi bit and sample-rate reduction) on the channels above it.
- **Pan** places each channel left or right (constant-power, so it stays equally loud as it
  moves; double-click a knob to centre it). Everything starts centred, which sounds exactly like
  mono. On a stereo input the Dry pan works as a balance control.
- **Detune** adds slightly sharp copies of the two octave-up voices for a 12-string shimmer.
- **Swell** fades the voices in after each picked note, like a volume swell or a bowed string.
  Its **Dry** switch makes the dry guitar swell too.
- **Warmth** adds soft tape-style saturation and a darker top to the voices above the dry
  guitar, so they sound less digital.
- **Output** sets the overall level. A gentle limiter rounds off peaks above about -2 dBFS so
  stacked voices on a hard strum never overload whatever comes next.
- **Tracking** trades speed for cleanliness. *Tight* responds fastest, *Clean* keeps low and
  close-voiced chords clearest but lets the voices bloom in more slowly, and *Balanced* sits
  between them.

Still to come: a Tone filter, and a proper interface.

Design goals:
- Feels like a pedal, not a delay. Your dry signal is never delayed. The voices fade in over
  a few tens of milliseconds, faster for high notes and in Tight mode.
- Tracks chords cleanly, with no pitch detection. Notes a semitone apart are the known weak
  spot.
- Light on CPU, and safe to run on the real-time audio thread.

## How it works

The guitar signal is split into 140 to 220 very narrow frequency bands (depending on the
Tracking mode), so each band holds roughly one pure tone from one string. Each tone is then
moved to its new pitch on its own and the results are added back together. Because nothing ever has to decide "which note
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

Every push to `main` and every pull request builds the plugin on GitHub Actions, runs the engine,
host and stress tests, and validates
it with `auval` and `pluginval`. Download the `PuppyChow-macOS` artifact from the latest run on
the Actions tab. Those builds are not code-signed, so macOS may block them the first time; run
`xattr -dr com.apple.quarantine "Puppy Chow.component"` after copying it in.

## Versions and testing

Each version is listed in [CHANGELOG.md](CHANGELOG.md), and the version number shows in the plugin
window. Finished, tested versions are on the [Releases](https://github.com/jdragash/puppy-chow/releases)
page as a zip with the Audio Unit, VST3 and standalone app.

How a change gets there:

1. New work goes on its own branch, never straight to `main`. GitHub builds and checks every
   branch, but nothing on a branch is a release.
2. That branch is built and installed on Jake's Mac so he can play it.
3. Only once Jake says it's good: the branch is merged into `main`, the version number is
   raised in `CMakeLists.txt` and `CHANGELOG.md`, and a `vX.Y.Z` tag is pushed. The tag
   builds the release automatically.

Built with [JUCE](https://juce.com).
