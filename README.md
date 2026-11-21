# Audio plugins

Jake's audio plugins for the Mac, as Audio Units (plus VST3 and standalone apps), built with
[JUCE](https://juce.com) and CMake.

| Plugin | What it is | Latest |
| --- | --- | --- |
| [Puppy Chow](plugins/puppy-chow/README.md) | A polyphonic octave pedal for guitar, to sit in front of an amp sim | [Releases](https://github.com/jdragash/audio-plugins/releases) |

## Layout

```
plugins/<name>/     one folder per plugin: its source, tests, docs, CHANGELOG.md, version
shared/             code more than one plugin uses (DSP blocks, the drawn-paper UI kit)
cmake/              build helpers, including the one copy of JUCE every plugin uses
.github/workflows/  CI that builds whichever plugin changed, and per-plugin releases
```

Each plugin keeps its own version, changelog, plugin codes and parameter ids. Moving code into
`shared/` never changes a plugin's sound or its saved settings.

## Building on a Mac

You need Xcode (or the Xcode command line tools), CMake and Ninja (`brew install cmake ninja`).

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release    # every plugin
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPLUGINS=puppy-chow   # just one
cmake --build build
ctest --test-dir build
```

The first configure downloads JUCE, so it takes a minute. Each plugin ends up in
`build/plugins/<name>/<Target>_artefacts/Release/`.

## Checks and releases

- Every pull request and push to `main` builds, tests and validates (auval, pluginval) only
  the plugins it touches. A change to `shared/`, `cmake/`, the root `CMakeLists.txt` or the
  workflows checks every plugin. Each plugin's details for CI are in its `plugin.json`.
- A release is a tag named after the plugin's folder and version, such as
  `puppy-chow-v0.5.0`. Pushing it builds that plugin and publishes it on the Releases page,
  after checking the tag matches the version in the plugin's `CMakeLists.txt`.
- New work goes on a branch. Nothing is merged or tagged until Jake has played the build on
  his Mac.

## Adding a plugin

1. Make `plugins/<name>/` with a `CMakeLists.txt` that starts with
   `project(<Target> VERSION 0.1.0 ...)` and calls `juce_add_plugin`, plus a `plugin.json`
   (`name`, `target`, `zip`, and the `au` type, subtype and manufacturer for auval).
2. Give it its own four-letter `PLUGIN_CODE` and bundle id. The manufacturer code stays `Jdrg`.
3. Name its tests `<name>.<test>` so `ctest -R <name>` picks them out.
