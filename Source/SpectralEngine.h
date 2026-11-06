#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace spectra
{

/** Parameter snapshot, read once per audio block. */
struct EngineParams
{
    bool  freeze     = false;
    float sizeMs     = 400.0f;
    float scan       = 0.5f;
    float drift      = 0.2f;
    float spread     = 0.1f;
    float diffuse    = 0.3f;
    float absorb     = 0.0f;
    float blur       = 0.0f;
    float shiftSemis = 0.0f;
    float shimmer    = 0.0f;
    float tilt       = 0.0f;   // -1..1  (±6 dB/oct around 1 kHz)
    float width      = 0.6f;
    float attackMs   = 400.0f;
    float releaseMs  = 1500.0f;
};

/** Written by the audio thread, read by the GUI. Lock-free (atomics only). */
struct VisualState
{
    static constexpr int numBands  = 128;
    static constexpr int thumbCols = 128;
    static constexpr int thumbRows = 48;
    static constexpr int numVoices = 2;

    VisualState();

    std::array<std::atomic<float>, numBands> inputBands;               // 0..1 (normalised dB)
    std::array<std::atomic<float>, numBands> padBands;                 // 0..1
    std::array<std::atomic<float>, thumbCols * thumbRows> thumb;       // row-major, row 0 = lowest band
    std::atomic<int>   thumbNumCols { 0 };
    std::atomic<int>   thumbVersion { 0 };
    std::atomic<float> regionStart { 0.0f };                           // normalised over thumbnail width
    std::array<std::atomic<float>, numVoices> playhead;                // normalised over thumbnail width
    std::array<std::atomic<float>, numVoices> voiceLevel;              // envelope 0..1
    std::atomic<bool>  frozen { false };
};

/** Tiny xorshift RNG - allocation free and fast enough for per-bin noise. */
struct FastRandom
{
    uint32_t state = 0x9E3779B9u;
    float next() noexcept   // 0..1
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float> (state >> 8) * (1.0f / 16777216.0f);
    }
    float bipolar() noexcept { return next() * 2.0f - 1.0f; }   // -1..1
};

/**
    The spectral freeze engine.

    - Analysis runs continuously into a "live" ring bank of STFT frames (magnitude + true frequency).
    - Freeze swaps the live bank into a voice (no copying) and a free bank becomes the new live bank.
    - Each voice resynthesises frames from its bank with phase-vocoder phases (+ Diffuse noise),
      overlap-added into a shared output buffer.
*/
class SpectralEngine
{
public:
    static constexpr int fftOrder    = 12;
    static constexpr int fftSize     = 1 << fftOrder;   // 4096
    static constexpr int hopSize     = fftSize / 4;     // 1024 (4x overlap)
    static constexpr int numBins     = fftSize / 2 + 1; // 2049
    static constexpr int maxChannels = 2;
    static constexpr int numVoices   = VisualState::numVoices;
    static constexpr float maxCaptureSeconds = 2.0f;

    SpectralEngine();

    void prepare (double sampleRate, int numInputChannels, int numOutputChannels);
    void reset();

    /** input: numInputChannels pointers. wetOutput: numOutputChannels pointers (overwritten).
        duck: per-sample pad presence 0..1, used by the processor to fade the dry signal. */
    void process (const float* const* input, float* const* wetOutput, float* duck,
                  int numSamples, const EngineParams& params) noexcept;

    VisualState& getVisualState() noexcept { return visuals; }

private:
    struct Bank
    {
        std::vector<float> mag, freq;   // [channel][frame][bin]
        int capacity = 0, writeIndex = 0, count = 0;

        void allocate (int frames);
        float*       magAt  (int ch, int frame) noexcept       { return mag.data()  + offset (ch, frame); }
        float*       freqAt (int ch, int frame) noexcept       { return freq.data() + offset (ch, frame); }
        const float* magAt  (int ch, int frame) const noexcept { return mag.data()  + offset (ch, frame); }
        const float* freqAt (int ch, int frame) const noexcept { return freq.data() + offset (ch, frame); }

        /** Ring index of the i-th frame (0 = oldest) of the newest `len` frames. */
        int frameIndex (int i, int len) const noexcept
        {
            int idx = (writeIndex - len + i) % capacity;
            return idx < 0 ? idx + capacity : idx;
        }
        int newestIndex() const noexcept { return frameIndex (count - 1, count); }

    private:
        size_t offset (int ch, int frame) const noexcept
        {
            return (static_cast<size_t> (ch) * static_cast<size_t> (capacity) + static_cast<size_t> (frame))
                   * static_cast<size_t> (numBins);
        }
    };

    struct Voice
    {
        int   bank = -1;
        float env = 0.0f;
        bool  gate = false;
        std::array<std::vector<float>, maxChannels> phase;
        float driftPos = 0.0f, driftTarget = 0.0f;
        int   driftTimer = 0;
        bool active() const noexcept { return gate || env > 0.0f; }
    };

    void processFrame (const EngineParams&) noexcept;
    void analyseChannel (int ch, Bank& live) noexcept;
    void startVoice (const EngineParams&) noexcept;
    void absorbInto (Voice&, float coeff) noexcept;
    void synthesiseVoice (int voiceIndex, const EngineParams&, bool isPrimary) noexcept;
    void remap (const float* srcMag, const float* srcFreq, float ratio, float gain) noexcept;
    void updateTiltTable (float tilt) noexcept;
    void computeBands (const float* mag, float gain, float* dest, bool takeMax) const noexcept;
    void publishThumbnail (const Bank&) noexcept;
    int  sizeInFrames (float sizeMs, const Bank&) const noexcept;

    double sampleRate = 44100.0;
    int numIn = 2, numOut = 2;

    juce::dsp::FFT fft { fftOrder };
    std::vector<float> window, fftData;
    std::array<std::vector<float>, maxChannels> inBuf, outBuf, prevPhase;

    std::array<Bank, numVoices + 1> banks;
    int liveBank = 0;
    std::array<Voice, numVoices> voices;

    // scratch (allocated in prepare, never on the audio thread)
    std::vector<float> interpMag, shiftMag, shiftFreq, winnerMag, blurMag, finalMag, tiltGain, sharedNoise;
    std::vector<double> prefix;
    std::array<int, VisualState::numBands + 1> bandLo {}, bandHi {};
    std::array<int, VisualState::thumbRows> thumbLo {}, thumbHi {};
    std::array<float, VisualState::numBands> bandScratch {};

    float currentTilt = 1.0e9f;
    int   hopCounter = 0;
    bool  wasFrozen = false;
    std::array<float, 3> duckHistory {};
    float duckFrom = 0.0f, duckTo = 0.0f;
    int   framesSinceThumb = 0;
    int   primaryVoice = -1;

    FastRandom rng;
    VisualState visuals;
};

} // namespace spectra
