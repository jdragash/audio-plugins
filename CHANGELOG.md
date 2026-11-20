# Changelog

Versions follow `MAJOR.MINOR.PATCH`. Until 1.0, a new MINOR version means new features or
sound changes, and a PATCH version means fixes only. Settings saved in a session may change
between 0.x versions.

## 0.2.1 (2026-11-20)

- Neutral blue-gray colors with more contrast instead of brown (still a placeholder
  until the real interface design).

## 0.2.0 (2026-11-20)

- Overtones channel ("ghost partials"): the 7th, 9th, 11th and 13th harmonics of whatever
  you play. They fall between the notes of a piano, so they add a shimmer no fretted note
  can make, and they bloom in after each pick. Level, pan and on/off like the other
  channels (off by default).
- Warmth control (0 to 100 %): soft, slightly lopsided tape-style saturation and a darker
  top on the voices above the dry guitar, so they sound less digital. The voices below
  are left alone.
- Solo (S) button on every channel: while any channel is soloed, only soloed channels play,
  even ones that are switched off.
- Fixed a voice that was switched off when the plugin was (re)started coming back far too
  loud when switched on.

## 0.1.0 (2026-11-19)

First tagged version. Everything built so far:

- Polyphonic pitch engine with no pitch detection: tracks single notes and chords.
- Eight channels, each with level, pan and an on/off switch: -2 octaves, -1 octave, -4th,
  Dry, +5th, +1 octave, +1 octave +5th, +2 octaves.
- Detune (12-string shimmer on the octave-up voices), Swell (voices fade in after each
  pick, with a Dry switch to swell the dry guitar too), Tracking mode (Tight, Balanced,
  Clean) and Output level with a safety limiter.
- Fixed crackle on low notes and strums.
- Fixed the plugin going silent (and silencing the track) when the input was nearly silent
  between notes, which could also make plugins after it, like Mastering Assistant, blast
  loud digital noise. The output can no longer carry invalid values.
- Builds as Audio Unit, VST3 and standalone app for macOS (Apple Silicon and Intel).
