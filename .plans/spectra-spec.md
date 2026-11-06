# Spectra — Spectral Freeze / Granular Texture Plugin

**Status:** Spec v1 · 2026-11-05
**One-liner:** Spectra captures a moment of sound and stretches it into a pad using the FFT.

---

## 1. Scope

| | Decision |
|---|---|
| Format | **VST3 only** (main target: FL Studio on macOS) |
| Framework | JUCE 9.0.1 at `~/JUCE`, CMake build (CMake installed through pip), Apple CLT clang 14, macOS SDK 13.3. No Xcode. |
| Plugin type | Audio effect, no MIDI in/out |
| Channel layouts | Mono→Stereo, Stereo→Stereo. Mono→Mono is not supported. |
| Sidechain | None. Capture comes only from the main input. |
| Tempo sync | None. All timing is free-running (ms/s). |
| State recall | Parameters only. **Frozen audio is not saved**: after reloading a project you must re-freeze. |

Out of scope for v1: AU, Standalone, MIDI triggering, sidechain, tempo sync, selectable FFT size, saving captured audio.

---

## 2. Core behaviour

### 2.1 Signal flow
```
input ──┬───────────────────────────── dry ──────────────┐
        │                                                ├─► out
        └─► STFT analysis ─► live ring bank ──(freeze)──► Voice A/B ─► wet ─┘
```
- **Unfrozen:** the dry signal passes through at unity gain. Spectra acts as a bypass, but analysis keeps running all the time so a capture is always ready.
- **Frozen:** the pad fades in over Attack. **Mix** sets how much the pad replaces the dry signal:
  - `wetGain = sin(mix · π/2) · Level`
  - `dryGain = lerp(1, cos(mix · π/2), duck)`, where `duck` follows the active voice envelopes.
  - At Mix = 0 you hear only dry, even while frozen. At Mix = 1 the dry signal is fully replaced while frozen.
- **Reported latency: 0.** The dry path is not delayed. The wet path's roughly one-FFT delay doesn't matter because the pad is frozen. The duck envelope is delayed by about 2 hops so dry doesn't drop out before the pad comes in.

### 2.2 Trigger
- **Freeze is a toggle** (a boolean parameter that can be automated).
- Rising edge: capture the last *Size* worth of frames and start a voice (gate on).
- Falling edge: all voices release over Release.
- Presets **never** change the Freeze state.

### 2.3 Capture model: slice and scan
- Analysis runs continuously into a **live ring bank** holding about 2 s of STFT frames.
- Freeze is a **pointer swap** with no copying. The live bank becomes the voice's frozen bank, and a free bank becomes the new live bank. There are three banks: live plus two voices.
- *Size* (20 ms–2 s) picks how many of the newest frames make up the active region. Size can change while frozen, since the full ~2 s stays stored. At the minimum (1 frame) you get the classic single-frame freeze.
- Each frame stores, per channel and per bin, the **magnitude** and the **true frequency** (phase-vocoder instantaneous frequency in fractional bins).

### 2.4 Re-freeze crossfade
- **Two voices.** A new freeze takes the idle voice, or the quieter one if both are busy. The previous voice releases over Release while the new one attacks over Attack.
- If both voices are busy, the quieter one is stolen. A short click is acceptable in that rare case.
- Voices that are idle (env = 0 and gate off) skip synthesis entirely, so they cost no CPU.

### 2.5 Absorb (live input while frozen)
- An **Absorb** knob (0–1, default 0). While frozen, each new live analysis frame is mixed into **every frame** of the active voice's bank:
  `mag = lerp(mag, liveMag, a)`, where `a` is a per-frame coefficient built from an exponential time constant. 0 means off, and 1 corresponds to τ ≈ 1 s. A log-scaled mapping gives roughly 60 s down to 1 s.
- Frequency data is blended the same way for bins that have meaningful energy. Otherwise the existing frequency is kept.
- Only the gated (attacking/sustaining) voice absorbs. A releasing voice stays fixed.

---

## 3. DSP details

| Constant | Value |
|---|---|
| FFT size N | **4096**, fixed at every sample rate |
| Hop | **N/4 = 1024** (4× overlap) |
| Window | Hann for both analysis and synthesis. OLA gain correction is 1/1.5. |
| Bins | 2049 |
| Bank length | `ceil(2.0 · sr / hop) + 2` frames, allocated in `prepareToPlay` |

### 3.1 Analysis (per channel, every hop)
1. Window the last N samples and run a forward real FFT.
2. `mag[k] = |X[k]|`, `φ[k] = arg X[k]`
3. `Δ = wrap(φ[k] − φprev[k] − 2πk·hop/N)` and `freq[k] = k + Δ·N/(2π·hop)`
4. Write the frame into the live bank. With a mono input, the one analysed channel feeds both output channels.

### 3.2 Synthesis (per voice, per output channel, every hop)
1. **Read position** in the region `[0, len−1]`:
   `pos = scan·(len−1) + drift(t) + spread·jitter`, reflected back into range.
   - **Drift:** a slow, smoothed random walk around Scan (organic and never exactly repeating). Its amplitude is `drift · len/2` and its rate is about 0.1–0.5 Hz.
   - **Spread:** a new random jitter every hop, ±`spread·len/2`. This is the granular texture.
   - For each channel, the jitter is `lerp(shared, independent, width)`.
2. **Magnitude:** linear interpolation between frames ⌊pos⌋ and ⌈pos⌉. **Frequency** is taken from the nearest frame.
3. **Pitch shift** (±24 st, ratio r): remap bins with `k' = round(j·r)`. Magnitudes accumulate and `freq' = freq·r`, in the Bernsee style.
4. **Shimmer** (0–1): run the same remap a second time with r × 2 (+1 octave) and add it to the main remap at amount `shimmer`. The result is normalised so loudness doesn't jump.
5. **Blur** (0–1): smear magnitudes over neighbouring bins with a prefix-sum box filter whose radius grows with frequency (constant-Q-ish), `radius_k = blur·(2 + 0.15·k)`.
6. **Tilt** (−1…+1): ±6 dB/octave pivoting at 1 kHz, from a precomputed per-bin gain table.
7. **Phase:** `φ[k] += 2π·hop·freq'[k]/N + diffuse·π·u`, where u ∈ [−1, 1].
   - Diffuse 0 keeps the phase-vocoder phase coherent, so the pad stays tonal and pitched.
   - Diffuse 1 makes the phase uniformly random, giving an airy wash.
   - u is drawn per bin as `lerp(shared, perChannel, width)`. Initial phases are randomised on voice start the same way.
8. Zero DC and Nyquist, inverse FFT, apply the synthesis window, scale by `env/1.5`, then overlap-add into the shared output buffer.

### 3.3 Envelope
- Each voice has a linear envelope that advances once per hop (one OLA frame). The windows smooth out the steps.
- Attack: 10 ms – 5 s, log skew, default 400 ms.
- Release: 10 ms – 10 s, log skew, default 1.5 s.

### 3.4 Real-time safety
- No allocations, locks or I/O on the audio thread. All buffers are allocated in `prepareToPlay`.
- Fast xorshift RNG on the audio thread.
- Parameters are read once per block. Mix and Level use per-sample `SmoothedValue`.
- Audio→GUI data goes only through atomics (see §5.4).

---

## 4. Parameters

| ID | Name | Range | Default | Group | Randomized |
|---|---|---|---|---|---|
| `freeze` | Freeze | bool | off | — | no |
| `size` | Size | 20 ms – 2000 ms (log) | 400 ms | Capture | no |
| `scan` | Scan | 0 – 100 % | 50 % | Texture | ✅ |
| `drift` | Drift | 0 – 100 % | 20 % | Texture | ✅ |
| `spread` | Spread | 0 – 100 % | 10 % | Texture | ✅ |
| `diffuse` | Diffuse | 0 – 100 % | 30 % | Texture | ✅ |
| `absorb` | Absorb | 0 – 100 % | 0 % | Capture | no |
| `blur` | Blur | 0 – 100 % | 0 % | Tone | ✅ |
| `shift` | Shift | −24 – +24 st (step 0.01, snaps visually to whole semitones) | 0 | Tone | no |
| `shimmer` | Shimmer | 0 – 100 % | 0 % | Tone | ✅ |
| `tilt` | Tilt | −100 – +100 % (±6 dB/oct) | 0 | Tone | ✅ |
| `width` | Width | 0 – 100 % | 60 % | Tone | ✅ |
| `attack` | Attack | 10 ms – 5 s (log) | 400 ms | Envelope | no |
| `release` | Release | 10 ms – 10 s (log) | 1.5 s | Envelope | no |
| `mix` | Mix | 0 – 100 % | 70 % | Output | no |
| `level` | Level | −24 – +12 dB | 0 dB | Output | no |

Each parameter has a one-line tooltip (see §5.5).

---

## 5. UI / UX

### 5.1 Look: "icy dark"
- Background `#0B0D11`, panels `#141821`, hairlines `#232A36`.
- Accents run from cyan `#8FD3FF` to violet `#B69CFF`. Text `#D8DEE9`, dim text `#6B7480`.
- Thin-arc rotary knobs with a gradient value arc and a small dot indicator. The value is shown under the label.
- A large glowing **FREEZE orb**. It is dim and frosted when off. When on, it shows a cyan/violet pulsing glow whose intensity follows the pad envelope.
- Wordmark: **SPECTRA**, uppercase and widely tracked.

### 5.2 Layout (base size 860 × 560)
```
┌──────────────────────────────────────────────────────────────┐
│ SPECTRA                      [Preset ▾] [⟳ Randomize]        │
├──────────────────────────────────────────────────────────────┤
│  MAIN DISPLAY                                                │
│   · ambient particles (back layer)                           │
│   · faint live-input spectrum + bright pad spectrum curves   │
│  ──────────────────────────────────────────────────────────  │
│  captured-slice spectrogram strip ▕region▏ with playheads    │
├───────────┬──────────────────────────────────────────────────┤
│           │ CAPTURE   TEXTURE                 TONE           │
│  ( ORB )  │ Size Absorb  Scan Drift Spread Diffuse  Blur ... │
│  FREEZE   │ TONE (cont.)        ENVELOPE        OUTPUT       │
│           │ Shift Shimmer Tilt Width  Attack Release  Mix Lvl│
└───────────┴──────────────────────────────────────────────────┘
```
- Knobs are grouped under small dim section headers: Capture · Texture · Tone · Envelope · Output.

### 5.3 Main display: stacked layers
1. **Particles (back):** about 150 soft glowing motes drifting upward and sideways. Their density and brightness follow the pad's energy and their hue follows the spectral centroid. When unfrozen they idle faintly.
2. **Spectrum curves (middle):** the live input as a faint filled curve and the pad output as a bright gradient line. 128 log-spaced bands, 20 Hz – 20 kHz, with a dB floor of −90.
3. **Spectrogram strip (bottom, about 60 px):** a sonogram of the captured bank (up to 128 columns × 48 log bands), with a bracket marking the active Size region and a playhead line for each active voice. It is empty or dim until the first freeze.
- The GUI timer runs at 30 Hz.

### 5.4 Audio → GUI data (lock-free)
- `std::atomic<float>` arrays for input bands[128], pad bands[128] and the thumbnail[128×48]. Also atomics for the thumbnail column count, region start/end, the playhead per voice, total envelope, centroid, and a version counter.
- The thumbnail is computed on the audio thread **only at freeze time** (about 400k flops). With Absorb on, it is refreshed every N hops.

### 5.5 Extras
- **Factory presets** (dropdown, about 8). Loading one never touches `freeze`. Planned presets:
  Glass (high Shift/Shimmer, low Diffuse) · Choir Wash (high Diffuse, Blur, Width) · Sub Drone (−12 st, dark Tilt) · Frozen Bell (Size min, Diffuse 0) · Grain Cloud (high Spread/Drift) · Shimmer Hall · Tape Memory (Absorb, low Tilt) · Init.
- **Randomize:** rolls fully random values for the texture group only (Scan, Drift, Spread, Diffuse, Blur, Tilt, Width, Shimmer). Each change is a single undoable gesture per parameter.
- **Tooltips:** a one-line explanation per knob, shown through `TooltipWindow`.
- **Resizable:** fixed aspect ratio, 75 %–150 % of the base size. Everything is laid out proportionally.

---

## 6. Files

```
CMakeLists.txt            VST3 target, juce_audio_utils + juce_dsp
Source/SpectralEngine.h/.cpp   STFT analysis, banks, voices, synthesis
Source/PluginProcessor.h/.cpp  APVTS, layouts, dry/wet mix, GUI atomics, presets
Source/PluginEditor.h/.cpp     LookAndFeel, knobs, orb, display, layout
Source/Presets.h          factory preset table
```

Build:
```
~/Library/Python/3.9/bin/cmake -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
~/Library/Python/3.9/bin/cmake --build build -j8
```
With `COPY_PLUGIN_AFTER_BUILD`, the built plugin is copied to `~/Library/Audio/Plug-Ins/VST3/Spectra.vst3`.

---

## 7. Acceptance criteria
1. It builds with no warnings-as-errors failures using only the CLT, then loads and rescans in FL Studio.
2. Unfrozen output is bit-identical to the input.
3. Freezing a sustained chord with Diffuse 0 gives a stable, clearly pitched pad with no audible flutter at Spread 0 / Drift 0.
4. Re-freezing while frozen crossfades between pads with no clicks under normal use.
5. Shift ±12 st keeps the pad's duration and texture, and only moves the pitch.
6. Mono→stereo with Width 100 % sounds clearly wide. With Width 0 it is mono-compatible.
7. CPU stays below about 3 % of one core per instance at 48 kHz, stereo, two voices active, on an M-series Mac.
8. There are no allocations on the audio thread. A debug build checked with a ScopedNoDenormals and allocation tracker shows none.
9. The GUI resizes smoothly from 75 % to 150 %, all three display layers animate, and tooltips appear.
10. Loading a preset while frozen changes the sound without releasing the pad.

## 8. Open risks
- Pitch-shifting by remapping bins smears transients and can sound "phasey" at large downward shifts. That is acceptable for pads.
- Absorb at high values can wash a pad out into whatever is playing now. That's by design, but the curve may need tuning by ear.
- The FFT path depends on JUCE's vDSP backend scaling the inverse FFT by 1/N. Verify this with a unit-impulse test during bring-up.
