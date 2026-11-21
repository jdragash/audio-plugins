# Shared code

Things more than one plugin can use. Nothing in here knows about a specific plugin.

- `dsp/`: plain C++ DSP blocks, header only, no JUCE dependency, real-time safe after
  `prepare()`. Link the `shared_dsp` CMake target and include them as
  `"shared/dsp/<Name>.h"`. Everything is in the `shared` namespace.
  - `Shimmer.h`: a lush stereo feedback-delay reverb (Puppy Chow's Shimmer sticker).
- `ui/`: the drawn-paper web-view interface kit (see `ui/README.md`).

When code moves in here, keep its behaviour identical so a plugin's sound and saved settings
don't change.
