# Changelog

Versions follow `MAJOR.MINOR.PATCH`. Until 1.0, a new MINOR version means new features or
sound changes, and a PATCH version means fixes only. Settings saved in a session may change
between 0.x versions.

## 0.1.0 (2026-11-18)

First tagged version. Everything built so far:

- Polyphonic pitch engine with no pitch detection: tracks single notes and chords.
- Eight channels, each with level, pan and an on/off switch: -2 octaves, -1 octave, -4th,
  Dry, +5th, +1 octave, +1 octave +5th, +2 octaves.
- Detune (12-string shimmer on the octave-up voices), Swell (voices fade in after each
  pick), Tracking mode (Tight, Balanced, Clean) and Output level with a safety limiter.
- Fixed crackle on low notes and strums.
- Builds as Audio Unit, VST3 and standalone app for macOS (Apple Silicon and Intel).
