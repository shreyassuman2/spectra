#include "SpectralEngine.h"

#include <algorithm>
#include <cmath>

namespace spectra
{

namespace
{
    constexpr float pi     = juce::MathConstants<float>::pi;
    constexpr float twoPi  = juce::MathConstants<float>::twoPi;
    // Phase advance per bin per hop: 2*pi*hop/N. With hop = N/4 this is pi/2.
    constexpr float advancePerBin = twoPi * static_cast<float> (SpectralEngine::hopSize)
                                          / static_cast<float> (SpectralEngine::fftSize);
    // A full-scale sine through a Hann window peaks at N/4 in the DFT.
    constexpr float magNorm = 4.0f / static_cast<float> (SpectralEngine::fftSize);
    // Hann analysis * Hann synthesis at 4x overlap sums to 1.5.
    constexpr float olaGain = 1.0f / 1.5f;
    constexpr float floorDb = -90.0f;

    inline float wrapPhase (float p) noexcept
    {
        return p - twoPi * std::round (p / twoPi);
    }

    inline float toDisplay (float mag) noexcept
    {
        const float db = 20.0f * std::log10 (mag * magNorm + 1.0e-9f);
        return juce::jlimit (0.0f, 1.0f, (db - floorDb) / -floorDb);
    }

    inline float reflect (float x, float lo, float hi) noexcept
    {
        if (hi <= lo) return lo;
        const float range = hi - lo;
        float t = std::fmod (x - lo, 2.0f * range);
        if (t < 0.0f) t += 2.0f * range;
        return lo + (t <= range ? t : 2.0f * range - t);
    }
}

//==============================================================================
VisualState::VisualState()
{
    for (auto& v : inputBands) v.store (0.0f);
    for (auto& v : padBands)   v.store (0.0f);
    for (auto& v : thumb)      v.store (0.0f);
    for (auto& v : playhead)   v.store (0.0f);
    for (auto& v : voiceLevel) v.store (0.0f);
}

void SpectralEngine::Bank::allocate (int frames)
{
    capacity = frames;
    const auto total = static_cast<size_t> (maxChannels) * static_cast<size_t> (frames) * static_cast<size_t> (numBins);
    mag.assign (total, 0.0f);
    freq.assign (total, 0.0f);
    writeIndex = 0;
    count = 0;
}

//==============================================================================
SpectralEngine::SpectralEngine()
{
    window.resize (fftSize);
    for (int n = 0; n < fftSize; ++n)   // periodic Hann
        window[(size_t) n] = 0.5f - 0.5f * std::cos (twoPi * static_cast<float> (n) / static_cast<float> (fftSize));
}

void SpectralEngine::prepare (double newSampleRate, int numInputChannels, int numOutputChannels)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    numIn  = juce::jlimit (1, maxChannels, numInputChannels);
    numOut = juce::jlimit (1, maxChannels, numOutputChannels);

    fftData.assign (2 * fftSize, 0.0f);
    for (int ch = 0; ch < maxChannels; ++ch)
    {
        inBuf[(size_t) ch].assign (fftSize, 0.0f);
        outBuf[(size_t) ch].assign (fftSize, 0.0f);
        prevPhase[(size_t) ch].assign (numBins, 0.0f);
    }

    const int frames = static_cast<int> (std::ceil (maxCaptureSeconds * sampleRate / hopSize)) + 2;
    for (auto& b : banks)
        b.allocate (frames);

    for (auto& v : voices)
        for (auto& p : v.phase)
            p.assign (numBins, 0.0f);

    for (auto* vec : { &interpMag, &shiftMag, &shiftFreq, &winnerMag, &blurMag, &finalMag, &tiltGain, &sharedNoise })
        vec->assign (numBins, 0.0f);
    prefix.assign (numBins + 1, 0.0);

    // Log-spaced display bands, 20 Hz .. 20 kHz
    const auto binOf = [this] (double hz) { return hz * fftSize / sampleRate; };
    for (int b = 0; b < VisualState::numBands; ++b)
    {
        const double f0 = 20.0 * std::pow (1000.0, (double) b / VisualState::numBands);
        const double f1 = 20.0 * std::pow (1000.0, (double) (b + 1) / VisualState::numBands);
        const int lo = juce::jlimit (1, numBins - 2, (int) std::floor (binOf (f0)));
        const int hi = juce::jlimit (lo, numBins - 2, (int) std::ceil (binOf (f1)) - 1);
        bandLo[(size_t) b] = lo;
        bandHi[(size_t) b] = hi;
    }
    for (int r = 0; r < VisualState::thumbRows; ++r)
    {
        const double f0 = 30.0 * std::pow (600.0, (double) r / VisualState::thumbRows);
        const double f1 = 30.0 * std::pow (600.0, (double) (r + 1) / VisualState::thumbRows);
        const int lo = juce::jlimit (1, numBins - 2, (int) std::floor (binOf (f0)));
        const int hi = juce::jlimit (lo, numBins - 2, (int) std::ceil (binOf (f1)) - 1);
        thumbLo[(size_t) r] = lo;
        thumbHi[(size_t) r] = hi;
    }

    currentTilt = 1.0e9f;
    reset();
}

void SpectralEngine::reset()
{
    for (int ch = 0; ch < maxChannels; ++ch)
    {
        std::fill (inBuf[(size_t) ch].begin(), inBuf[(size_t) ch].end(), 0.0f);
        std::fill (outBuf[(size_t) ch].begin(), outBuf[(size_t) ch].end(), 0.0f);
        std::fill (prevPhase[(size_t) ch].begin(), prevPhase[(size_t) ch].end(), 0.0f);
    }
    for (auto& b : banks) { b.writeIndex = 0; b.count = 0; }
    liveBank = 0;
    for (int v = 0; v < numVoices; ++v)
    {
        voices[(size_t) v].bank = v + 1;
        voices[(size_t) v].env  = 0.0f;
        voices[(size_t) v].gate = false;
        voices[(size_t) v].releaseCapMs = 0.0f;
    }
    hopCounter = 0;
    wasFrozen = false;
    duckHistory = {};
    duckFrom = duckTo = 0.0f;
    primaryVoice = -1;
    visuals.thumbNumCols.store (0);
    visuals.frozen.store (false);
}

int SpectralEngine::sizeInFrames (float sizeMs, const Bank& b) const noexcept
{
    const int frames = (int) std::lround (sizeMs * 0.001 * sampleRate / hopSize);
    return juce::jlimit (1, b.capacity, frames);
}

//==============================================================================
void SpectralEngine::process (const float* const* input, float* const* wetOutput, float* duck,
                              int numSamples, const EngineParams& params) noexcept
{
    if (params.freeze && ! wasFrozen)
        startVoice (params);
    else if (params.freeze && params.retrigger && banks[(size_t) liveBank].count >= 2)   // need new material to capture
        startVoice (params);
    else if (! params.freeze && wasFrozen)
        for (auto& v : voices)
            if (v.gate) { v.gate = false; v.releaseCapMs = 0.0f; }

    wasFrozen = params.freeze;
    visuals.frozen.store (params.freeze);

    const float invHop = 1.0f / static_cast<float> (hopSize);

    for (int i = 0; i < numSamples; ++i)
    {
        const int pos = hopCounter;

        for (int ch = 0; ch < numIn; ++ch)
            inBuf[(size_t) ch][(size_t) (fftSize - hopSize + pos)] = input[ch][i];

        for (int ch = 0; ch < numOut; ++ch)
            wetOutput[ch][i] = outBuf[(size_t) ch][(size_t) pos];

        duck[i] = duckFrom + (duckTo - duckFrom) * (static_cast<float> (pos) * invHop);

        if (++hopCounter == hopSize)
        {
            hopCounter = 0;
            processFrame (params);
        }
    }
}

void SpectralEngine::processFrame (const EngineParams& params) noexcept
{
    const float hopSeconds = static_cast<float> (hopSize / sampleRate);

    // 1. Analysis into the live bank -----------------------------------------
    Bank& live = banks[(size_t) liveBank];
    for (int ch = 0; ch < numIn; ++ch)
        analyseChannel (ch, live);

    live.writeIndex = (live.writeIndex + 1) % live.capacity;
    live.count = std::min (live.count + 1, live.capacity);

    for (int ch = 0; ch < numIn; ++ch)
    {
        auto& buf = inBuf[(size_t) ch];
        std::copy (buf.begin() + hopSize, buf.end(), buf.begin());
    }

    computeBands (live.magAt (0, live.newestIndex()), 1.0f, bandScratch.data(), false);
    for (int b = 0; b < VisualState::numBands; ++b)
        visuals.inputBands[(size_t) b].store (bandScratch[(size_t) b], std::memory_order_relaxed);

    // 2. Absorb: fold live input into the gated voice's slice -----------------
    if (params.absorb > 0.001f)
    {
        const float tau = 60.0f * std::pow (1.0f / 60.0f, params.absorb);   // 60 s .. 1 s
        const float coeff = 1.0f - std::exp (-hopSeconds / tau);
        for (auto& v : voices)
            if (v.gate)
                absorbInto (v, coeff);

        if (++framesSinceThumb >= 12 && primaryVoice >= 0)
        {
            framesSinceThumb = 0;
            publishThumbnail (banks[(size_t) voices[(size_t) primaryVoice].bank]);
        }
    }

    // 3. Envelopes -------------------------------------------------------------
    const float attackStep  = hopSeconds / std::max (0.001f, params.attackMs  * 0.001f);
    for (auto& v : voices)
    {
        const float releaseMs = v.releaseCapMs > 0.0f ? std::min (params.releaseMs, v.releaseCapMs) : params.releaseMs;
        v.env = v.gate ? std::min (1.0f, v.env + attackStep)
                       : std::max (0.0f, v.env - hopSeconds / std::max (0.001f, releaseMs * 0.001f));
    }

    // The primary voice drives the visuals: the gated one, else the loudest.
    primaryVoice = -1;
    for (int v = 0; v < numVoices; ++v)
        if (voices[(size_t) v].gate) primaryVoice = v;
    if (primaryVoice < 0)
    {
        float best = 0.0f;
        for (int v = 0; v < numVoices; ++v)
            if (voices[(size_t) v].env > best) { best = voices[(size_t) v].env; primaryVoice = v; }
    }

    // 4. Advance the overlap-add buffer -----------------------------------------
    for (int ch = 0; ch < numOut; ++ch)
    {
        auto& buf = outBuf[(size_t) ch];
        std::copy (buf.begin() + hopSize, buf.end(), buf.begin());
        std::fill (buf.end() - hopSize, buf.end(), 0.0f);
    }

    // 5. Synthesis -----------------------------------------------------------------
    updateTiltTable (params.tilt);
    bandScratch.fill (0.0f);

    for (int v = 0; v < numVoices; ++v)
    {
        visuals.voiceLevel[(size_t) v].store (voices[(size_t) v].env, std::memory_order_relaxed);
        if (voices[(size_t) v].env > 0.0f)
            synthesiseVoice (v, params, v == primaryVoice);
    }

    for (int b = 0; b < VisualState::numBands; ++b)
        visuals.padBands[(size_t) b].store (bandScratch[(size_t) b], std::memory_order_relaxed);

    // 6. Dry duck, delayed ~2 hops so the dry signal doesn't dip before the pad arrives
    float total = 0.0f;
    for (auto& v : voices) total += v.env;
    duckHistory[0] = duckHistory[1];
    duckHistory[1] = duckHistory[2];
    duckHistory[2] = std::min (1.0f, total);
    duckFrom = duckTo;
    duckTo = duckHistory[0];
}

void SpectralEngine::analyseChannel (int ch, Bank& live) noexcept
{
    float* d = fftData.data();
    const float* in = inBuf[(size_t) ch].data();
    for (int n = 0; n < fftSize; ++n)
        d[n] = in[n] * window[(size_t) n];
    std::fill (d + fftSize, d + 2 * fftSize, 0.0f);

    fft.performRealOnlyForwardTransform (d, true);

    float* mag  = live.magAt  (ch, live.writeIndex);
    float* freq = live.freqAt (ch, live.writeIndex);
    float* prev = prevPhase[(size_t) ch].data();

    for (int k = 0; k < numBins; ++k)
    {
        const float re = d[2 * k], im = d[2 * k + 1];
        const float phase = std::atan2 (im, re);
        mag[k] = std::sqrt (re * re + im * im);

        // Phase-vocoder true frequency (in fractional bins)
        const float deviation = wrapPhase (phase - prev[k] - static_cast<float> (k) * advancePerBin);
        prev[k] = phase;
        freq[k] = static_cast<float> (k) + deviation / advancePerBin;
    }
}

void SpectralEngine::startVoice (const EngineParams& params) noexcept
{
    int vi = -1;
    for (int v = 0; v < numVoices; ++v)
        if (! voices[(size_t) v].active()) { vi = v; break; }

    if (vi < 0)   // steal the quietest
    {
        vi = 0;
        for (int v = 1; v < numVoices; ++v)
            if (voices[(size_t) v].env < voices[(size_t) vi].env) vi = v;
    }

    // The capture that is still sounding, if any: the new one continues its phases.
    int outgoing = -1;
    for (int v = 0; v < numVoices; ++v)
        if (v != vi && voices[(size_t) v].gate && voices[(size_t) v].env > 0.0f) outgoing = v;

    // Everything else crossfades out. On the re-freeze grid the outgoing capture fades within the
    // limit, so with three voices the one stolen next is already silent.
    const float cap = params.retrigger ? std::max (0.0f, params.retriggerReleaseMs) : 0.0f;
    for (auto& v : voices)
        if (v.gate) { v.gate = false; v.releaseCapMs = cap; }

    Voice& voice = voices[(size_t) vi];
    voice.releaseCapMs = 0.0f;
    std::swap (voice.bank, liveBank);         // the live history becomes this voice's slice
    banks[(size_t) liveBank].writeIndex = 0;
    banks[(size_t) liveBank].count = 0;

    voice.gate = true;
    voice.env = 0.0f;
    voice.driftPos = 0.0f;
    voice.driftTarget = 0.0f;
    voice.driftTimer = 0;

    if (outgoing >= 0)
    {
        // Same phases as the capture fading out: when both hold similar sound (a held note re-captured
        // on the grid) they add up instead of partly cancelling, so the crossfade has no dip.
        for (int ch = 0; ch < numOut; ++ch)
            voice.phase[(size_t) ch] = voices[(size_t) outgoing].phase[(size_t) ch];   // same size: no allocation
    }
    else
    {
        for (int k = 0; k < numBins; ++k)
            sharedNoise[(size_t) k] = rng.next();
        for (int ch = 0; ch < numOut; ++ch)
            for (int k = 0; k < numBins; ++k)
            {
                const float s = sharedNoise[(size_t) k];
                voice.phase[(size_t) ch][(size_t) k] = twoPi * (s + params.width * (rng.next() - s));
            }
    }

    primaryVoice = vi;
    framesSinceThumb = 0;
    publishThumbnail (banks[(size_t) voice.bank]);
}

void SpectralEngine::absorbInto (Voice& v, float coeff) noexcept
{
    Bank& b = banks[(size_t) v.bank];
    const Bank& live = banks[(size_t) liveBank];
    if (b.count < 1 || live.count < 1) return;

    const int newest = live.newestIndex();
    for (int ch = 0; ch < numIn; ++ch)
    {
        const float* liveMag  = live.magAt  (ch, newest);
        const float* liveFreq = live.freqAt (ch, newest);
        for (int i = 0; i < b.count; ++i)
        {
            const int idx = b.frameIndex (i, b.count);
            float* m = b.magAt (ch, idx);
            float* f = b.freqAt (ch, idx);
            juce::FloatVectorOperations::multiply (m, 1.0f - coeff, numBins);
            juce::FloatVectorOperations::addWithMultiply (m, liveMag, coeff, numBins);
            juce::FloatVectorOperations::multiply (f, 1.0f - coeff, numBins);
            juce::FloatVectorOperations::addWithMultiply (f, liveFreq, coeff, numBins);
        }
    }
}

void SpectralEngine::remap (const float* srcMag, const float* srcFreq, float ratio, float gain) noexcept
{
    // Shifting down folds about 1/ratio source bins onto each target bin, and their magnitudes add up;
    // shifting up spreads them out and leaves gaps. Compensate both so the level stays put.
    gain *= ratio < 1.0f ? ratio : std::sqrt (ratio);
    for (int j = 1; j < numBins - 1; ++j)
    {
        const int k = static_cast<int> (static_cast<float> (j) * ratio + 0.5f);
        if (k >= numBins - 1) break;
        if (k < 1) continue;

        const float m = srcMag[j] * gain;
        shiftMag[(size_t) k] += m;
        if (m > winnerMag[(size_t) k])
        {
            winnerMag[(size_t) k] = m;
            shiftFreq[(size_t) k] = srcFreq[j] * ratio;
        }
    }
}

void SpectralEngine::updateTiltTable (float tilt) noexcept
{
    if (std::abs (tilt - currentTilt) < 1.0e-4f) return;
    currentTilt = tilt;

    const float dbPerOct = 6.0f * tilt;
    for (int k = 0; k < numBins; ++k)
    {
        const float hz = std::max (20.0f, static_cast<float> (k * sampleRate / fftSize));
        const float db = juce::jlimit (-48.0f, 18.0f, dbPerOct * std::log2 (hz / 1000.0f));
        tiltGain[(size_t) k] = juce::Decibels::decibelsToGain (db);
    }
}

void SpectralEngine::synthesiseVoice (int voiceIndex, const EngineParams& params, bool isPrimary) noexcept
{
    Voice& v = voices[(size_t) voiceIndex];
    const Bank& b = banks[(size_t) v.bank];
    const int len = std::min (b.count, sizeInFrames (params.sizeMs, b));
    if (len < 1) return;

    const float hopSeconds = static_cast<float> (hopSize / sampleRate);
    const float gain = v.env * olaGain;

    // Drift: slow smoothed random walk around Scan
    if (--v.driftTimer <= 0)
    {
        v.driftTarget = rng.bipolar();
        v.driftTimer = static_cast<int> ((2.0f + 4.0f * rng.next()) / hopSeconds);   // new target every 2-6 s
    }
    v.driftPos += (v.driftTarget - v.driftPos) * (1.0f - std::exp (-hopSeconds / 1.5f));

    const float maxPos  = static_cast<float> (len - 1);
    const float half    = maxPos * 0.5f;
    const float centre  = params.scan * maxPos + params.drift * v.driftPos * half;
    const float sharedJitter = rng.bipolar();

    const float ratio   = std::pow (2.0f, params.shiftSemis / 12.0f);
    const float shimmer = params.shimmer;
    const bool  needsRemap = std::abs (ratio - 1.0f) > 1.0e-4f || shimmer > 1.0e-3f;
    const float width   = params.width;
    const float diffuse = params.diffuse * pi;

    for (int k = 0; k < numBins; ++k)
        sharedNoise[(size_t) k] = rng.bipolar();

    float* d = fftData.data();

    for (int c = 0; c < numOut; ++c)
    {
        const int src = std::min (c, numIn - 1);

        // Frame position: Scan + Drift + Spread jitter (decorrelated per channel by Width)
        const float jitter = sharedJitter + width * (rng.bipolar() - sharedJitter);
        const float pos = reflect (centre + params.spread * jitter * half, 0.0f, maxPos);
        const int i0 = std::min (static_cast<int> (pos), len - 1);
        const int i1 = std::min (i0 + 1, len - 1);
        const float frac = pos - static_cast<float> (i0);

        const int idx0 = b.frameIndex (i0, len), idx1 = b.frameIndex (i1, len);
        const float* m0 = b.magAt (src, idx0);
        const float* m1 = b.magAt (src, idx1);
        const float* freq = b.freqAt (src, frac < 0.5f ? idx0 : idx1);

        for (int k = 0; k < numBins; ++k)
            interpMag[(size_t) k] = m0[k] + frac * (m1[k] - m0[k]);

        const float* mag = interpMag.data();

        // Pitch shift + shimmer (+1 octave) by bin remapping
        if (needsRemap)
        {
            std::fill (shiftMag.begin(), shiftMag.end(), 0.0f);
            std::fill (winnerMag.begin(), winnerMag.end(), 0.0f);
            for (int k = 0; k < numBins; ++k)
                shiftFreq[(size_t) k] = static_cast<float> (k);

            const float g1 = 1.0f / std::sqrt (1.0f + shimmer * shimmer);
            remap (interpMag.data(), freq, ratio, g1);
            if (shimmer > 1.0e-3f)
                remap (interpMag.data(), freq, ratio * 2.0f, shimmer * g1);

            mag = shiftMag.data();
            freq = shiftFreq.data();
        }

        // Blur: constant-Q-ish box smear via prefix sums
        if (params.blur > 0.001f)
        {
            prefix[0] = 0.0;
            for (int k = 0; k < numBins; ++k)
                prefix[(size_t) k + 1] = prefix[(size_t) k] + mag[k];

            for (int k = 0; k < numBins; ++k)
            {
                const int r = std::min (256, static_cast<int> (params.blur * (2.0f + 0.15f * static_cast<float> (k))));
                const int lo = std::max (0, k - r), hi = std::min (numBins - 1, k + r);
                blurMag[(size_t) k] = static_cast<float> ((prefix[(size_t) hi + 1] - prefix[(size_t) lo]) / (hi - lo + 1));
            }
            mag = blurMag.data();
        }

        // Tilt, loudness-matched: Tone changes the balance, not the level (limited to +-12 dB of correction).
        float tiltNorm = 1.0f;
        if (std::abs (currentTilt) > 1.0e-4f)
        {
            double before = 0.0, after = 0.0;
            for (int k = 1; k < numBins - 1; ++k)
            {
                const double m = mag[k], mt = m * tiltGain[(size_t) k];
                before += m * m;
                after += mt * mt;
            }
            if (after > 1.0e-20)
                tiltNorm = juce::jlimit (0.25f, 4.0f, static_cast<float> (std::sqrt (before / after)));
        }

        // Phases, spectrum
        float* ph = v.phase[(size_t) c].data();
        for (int k = 0; k < numBins; ++k)
        {
            const float m = (k == 0 || k == numBins - 1) ? 0.0f : mag[k] * tiltGain[(size_t) k] * tiltNorm;
            finalMag[(size_t) k] = m;

            const float s = sharedNoise[(size_t) k];
            const float noise = s + width * (rng.bipolar() - s);
            float p = ph[k] + advancePerBin * freq[k] + diffuse * noise;
            p -= twoPi * std::floor (p / twoPi);
            ph[k] = p;

            d[2 * k]     = m * std::cos (p);
            d[2 * k + 1] = m * std::sin (p);
        }

        fft.performRealOnlyInverseTransform (d);

        float* out = outBuf[(size_t) c].data();
        for (int n = 0; n < fftSize; ++n)
            out[n] += d[n] * window[(size_t) n] * gain;

        if (c == 0)
        {
            visuals.playhead[(size_t) voiceIndex].store (
                (static_cast<float> (b.count - len) + pos + 0.5f) / static_cast<float> (b.count),
                std::memory_order_relaxed);

            if (isPrimary)
                visuals.regionStart.store (static_cast<float> (b.count - len) / static_cast<float> (b.count),
                                           std::memory_order_relaxed);
        }

        if (c == 0)
            computeBands (finalMag.data(), v.env, bandScratch.data(), true);
    }
}

void SpectralEngine::computeBands (const float* mag, float gain, float* dest, bool takeMax) const noexcept
{
    for (int b = 0; b < VisualState::numBands; ++b)
    {
        float m = 0.0f;
        for (int k = bandLo[(size_t) b]; k <= bandHi[(size_t) b]; ++k)
            m = std::max (m, mag[k]);

        const float v = m * gain > 0.0f ? toDisplay (m * gain) : 0.0f;
        dest[b] = takeMax ? std::max (dest[b], v) : v;
    }
}

void SpectralEngine::publishThumbnail (const Bank& b) noexcept
{
    if (b.count < 1)
    {
        visuals.thumbNumCols.store (0);
        visuals.thumbVersion.fetch_add (1);
        return;
    }

    const int cols = std::min (b.count, VisualState::thumbCols);
    for (int c = 0; c < cols; ++c)
    {
        const int frame = std::min (b.count - 1, static_cast<int> ((c + 0.5f) * b.count / cols));
        const float* mag = b.magAt (0, b.frameIndex (frame, b.count));

        for (int r = 0; r < VisualState::thumbRows; ++r)
        {
            float m = 0.0f;
            for (int k = thumbLo[(size_t) r]; k <= thumbHi[(size_t) r]; ++k)
                m = std::max (m, mag[k]);
            visuals.thumb[(size_t) (r * VisualState::thumbCols + c)].store (toDisplay (m), std::memory_order_relaxed);
        }
    }

    visuals.thumbNumCols.store (cols);
    visuals.thumbVersion.fetch_add (1);
}

} // namespace spectra
