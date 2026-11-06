# Spectra

Spectral freeze / granular texture plugin. It captures a moment of sound and stretches it into a pad using the FFT.

VST3 · built with [JUCE](https://juce.com) · macOS

> Work in progress. See the design spec in [`.plans/spectra-spec.md`](.plans/spectra-spec.md).

## Build

Requires JUCE (default path `~/JUCE`) and CMake.

```sh
cmake -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release   # add -DJUCE_DIR=/path/to/JUCE if needed
cmake --build build -j8
```

The VST3 is copied to `~/Library/Audio/Plug-Ins/VST3/Spectra.vst3` after building.
