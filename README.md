# Spectra

Spectral freeze / granular texture plugin. It captures a moment of sound and stretches it into a pad using the FFT.

VST3 · built with [JUCE](https://juce.com) · macOS

Design spec: [`.plans/spectra-spec.md`](.plans/spectra-spec.md).

## Controls

| Section | Controls |
|---|---|
| Capture | **Size** (20 ms–2 s slice), **Absorb** (blend live input into the frozen pad) |
| Texture | **Scan**, **Drift** (random wander), **Spread** (grain jitter), **Diffuse** (tonal → wash) |
| Tone | **Blur**, **Shift** (±24 st), **Shimmer** (+1 oct), **Tone** (dark ↔ bright tilt), **Width** |
| Envelope | **Attack** (10 ms–5 s), **Release** (10 ms–10 s) |
| Output | **Mix** (how much the pad replaces the dry signal while frozen), **Level** |

The UI is a night-themed hardware panel. The glass screen holds Scan, Spread, Absorb, Blur and Shimmer as drag tiles, and Attack and Release as tick bars. Three module cards hold the knobs: **Freeze** (the moon is the freeze button, with Size and Drift), **Haze** (Diffuse, Shift, Tone) and **Orbit** (Mix, Width, Level). Set the window size from the % pill in the footer (75–150%).

Click the **moon** to capture the last moment of sound, and click it again to release. While unfrozen, audio passes through untouched.

## Build

Requires JUCE (default path `~/JUCE`) and CMake.

```sh
cmake -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release   # add -DJUCE_DIR=/path/to/JUCE if needed
cmake --build build -j8
```

The VST3 is copied to `~/Library/Audio/Plug-Ins/VST3/Spectra.vst3` after building.

## Tests

Offline DSP checks cover dry passthrough, pitch stability, shift, stereo width, click-free re-freeze, and CPU:

```sh
cmake --build build --target SpectraTests
./build/SpectraTests_artefacts/Release/SpectraTests
```
