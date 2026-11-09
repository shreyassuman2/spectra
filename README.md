
# Spectra

Free spectral freeze / granular texture fx plugin. It captures a moment of sound and stretches it into a pad using FFT.

VST3 and Audio Unit · Mac and Windows · built with [JUCE](https://juce.com)

### [Download Spectra](https://github.com/shreyassuman2/spectra/releases/latest)

## Demo: 

https://github.com/user-attachments/assets/11d4fc5e-46c1-4822-8bb2-39ed0001dfb2

Design spec: [`.plans/spectra-spec.md`](.plans/spectra-spec.md).

## Install

### Mac

1. Download the `.pkg` file and double-click it.
2. macOS will say it *can't verify* Spectra. This is because Spectra isn't signed with a paid Apple developer certificate(it costs 100/yr to get a certificate. the file is safe). Click **Done**.
3. Open **System Settings → Privacy & Security**, scroll down, and click **Open Anyway** next to the message about Spectra.
4. The installer opens. Click **Continue → Install**.

Spectra works on Apple Silicon and Intel Macs running macOS 11 or later.

### Windows

1. Download the `setup.exe` file and double-click it.
2. If Windows shows *"Windows protected your PC"*, click **More info → Run anyway**. Spectra isn't signed w/ a paid certificate(same reason as mac), which is why that appears.
3. Click through the installer. It puts Spectra in `C:\Program Files\Common Files\VST3`.

To remove it later, use **Settings → Apps → Spectra (VST3) → Uninstall**.

### Open it in your DAW

**FL Studio:** go to **Options → Manage plugins** and click **Find plugins**, then **star** Spectra. Search for it in More Plugins afterwards.

Ableton Live, Reaper, Bitwig, and Studio One pick it up on their next plugin scan.

## Build

Requires JUCE (default path `~/JUCE`) and CMake.

```sh
cmake -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release   # add -DJUCE_DIR=/path/to/JUCE if needed
cmake --build build -j8
```

The VST3 is copied to `~/Library/Audio/Plug-Ins/VST3/Spectra.vst3` after building. If JUCE isn't found at `JUCE_DIR`, CMake downloads JUCE 9.0.1 automatically.

### Releases

`packaging/mac/build-installer.sh` builds a universal Mac installer into `dist/`. To publish a release with both the Mac and Windows installers, bump `VERSION` in `CMakeLists.txt`, then push a tag:

```sh
git tag v1.0.0 && git push origin v1.0.0
```

GitHub Actions (`.github/workflows/release.yml`) builds both installers and attaches them to a new GitHub Release.

## Tests

Offline DSP checks cover dry passthrough, pitch stability, shift, stereo width, click-free re-freeze, and CPU:

```sh
cmake --build build --target SpectraTests
./build/SpectraTests_artefacts/Release/SpectraTests
```

To re-render the demo GIF frames: `./build/SpectraTests_artefacts/Release/SpectraTests --demo-frames <dir>` (15 fps PNGs).

To re-render the demo video, run `--demo-video <dir>` (30 fps PNGs at 2× plus `audio.wav`), then combine them with ffmpeg:

```sh
ffmpeg -framerate 30 -i <dir>/%04d.png -i <dir>/audio.wav -c:v libx264 -crf 20 -pix_fmt yuv420p -c:a aac -b:a 192k -shortest demo.mp4
```

## License

Spectra is free software under the [GNU AGPLv3](LICENSE). It is built with JUCE, which is used under the same license. Release notes are in [CHANGELOG.md](CHANGELOG.md).
