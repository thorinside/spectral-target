#include "spectral_target.hpp"

#include <algorithm>
#include <math.h>
#include <string.h>

#if !SPECTRAL_TARGET_USE_CMSIS
int arm_rfft_fast_init_f32(arm_rfft_fast_instance_f32* s, uint16_t fftLen) {
    s->fftLenRFFT = fftLen;
    return 0;
}

void arm_rfft_fast_f32(arm_rfft_fast_instance_f32* s, float* p, float* pOut, uint8_t ifftFlag) {
    const int n = s->fftLenRFFT;
    const int half = n / 2;
    const float twoPi = 6.2831853071795864769f;

    if (!ifftFlag) {
        for (int k = 0; k <= half; ++k) {
            float real = 0.0f;
            float imag = 0.0f;
            for (int i = 0; i < n; ++i) {
                float phase = twoPi * (float)k * (float)i / (float)n;
                real += p[i] * cosf(phase);
                imag -= p[i] * sinf(phase);
            }
            if (k == 0)
                pOut[0] = real;
            else if (k == half)
                pOut[1] = real;
            else {
                pOut[2 * k] = real;
                pOut[2 * k + 1] = imag;
            }
        }
        return;
    }

    for (int i = 0; i < n; ++i) {
        float value = p[0] + ((i & 1) ? -p[1] : p[1]);
        for (int k = 1; k < half; ++k) {
            float phase = twoPi * (float)k * (float)i / (float)n;
            float real = p[2 * k];
            float imag = p[2 * k + 1];
            value += 2.0f * (real * cosf(phase) - imag * sinf(phase));
        }
        pOut[i] = value / (float)n;
    }
}

void arm_biquad_cascade_df2T_init_f32(arm_biquad_cascade_df2T_instance_f32* s,
                                      uint8_t numStages,
                                      float* pCoeffs,
                                      float* pState) {
    s->numStages = numStages;
    s->pCoeffs = pCoeffs;
    s->pState = pState;
    memset(pState, 0, sizeof(float) * 2 * numStages);
}

void arm_biquad_cascade_df2T_f32(const arm_biquad_cascade_df2T_instance_f32* s,
                                 const float* pSrc,
                                 float* pDst,
                                 uint32_t blockSize) {
    for (uint32_t n = 0; n < blockSize; ++n) {
        float x = pSrc[n];
        for (uint32_t stage = 0; stage < s->numStages; ++stage) {
            const float* c = s->pCoeffs + stage * 5;
            float* st = s->pState + stage * 2;
            float y = c[0] * x + st[0];
            st[0] = c[1] * x + c[3] * y + st[1];
            st[1] = c[2] * x + c[4] * y;
            x = y;
        }
        pDst[n] = x;
    }
}
#endif

const int SpectralTarget::kFftSize;
const int SpectralTarget::kNumBins;
const int SpectralTarget::kNumBands;
const float SpectralTarget::kMaxGainDb = 12.0f;

SpectralTarget::SpectralTarget()
    : mode_(kBypass),
      sampleRate_(48000.0f),
      analysisHop_(4800),
      captureHop_(24000),
      samplesUntilAnalysis_(4800),
      lifterCutoff_(32),
      learningRate_(0.0f),
      analysisSmoothing_(0.85f),
      amount_(1.0f),
      targetCaptureCount_(0),
      targetReady_(false),
      watchReady_(false),
      matchReady_(false) {
    memset(window_, 0, sizeof(window_));
    memset(fftInput_, 0, sizeof(fftInput_));
    memset(fftOutput_, 0, sizeof(fftOutput_));
    memset(cepstrum_, 0, sizeof(cepstrum_));
    memset(bandsDb_, 0, sizeof(bandsDb_));
    memset(analysisEnergy_, 0, sizeof(analysisEnergy_));
    memset(watchDb_, 0, sizeof(watchDb_));
    memset(targetDb_, 0, sizeof(targetDb_));
    memset(targetM2Db_, 0, sizeof(targetM2Db_));
    memset(targetMinDb_, 0, sizeof(targetMinDb_));
    memset(targetMaxDb_, 0, sizeof(targetMaxDb_));
    memset(desiredGainDb_, 0, sizeof(desiredGainDb_));
    memset(appliedGainDb_, 0, sizeof(appliedGainDb_));
    memset(bandEdges_, 0, sizeof(bandEdges_));
    memset(bandCenters_, 0, sizeof(bandCenters_));
    memset(biquadCoeffs_, 0, sizeof(biquadCoeffs_));
    memset(biquadState_, 0, sizeof(biquadState_));
    memset(biquadStateR_, 0, sizeof(biquadStateR_));
    memset(analysisCoeffs_, 0, sizeof(analysisCoeffs_));
    memset(analysisState_, 0, sizeof(analysisState_));
    analysisSamples_ = 0;
    arm_rfft_fast_init_f32(&fft_, kFftSize);
    biquad_.numStages = kNumBands;
    biquad_.pCoeffs = biquadCoeffs_;
    biquad_.pState = biquadState_;
    biquadR_.numStages = kNumBands;
    biquadR_.pCoeffs = biquadCoeffs_;
    biquadR_.pState = biquadStateR_;
    prepare(sampleRate_);
}

void SpectralTarget::prepare(float sampleRate) {
    sampleRate_ = sampleRate > 1000.0f ? sampleRate : 48000.0f;
    analysisHop_ = std::max(1, (int)(sampleRate_ * 0.50f + 0.5f));
    captureHop_ = analysisHop_;
    samplesUntilAnalysis_ = analysisHop_;

    for (int i = 0; i < kFftSize; ++i)
        window_[i] = 0.5f - 0.5f * cosf(6.2831853071795864769f * (float)i / (float)(kFftSize - 1));

    const float minFrequency = 31.25f;
    const float maxFrequency = std::min(20000.0f, sampleRate_ * 0.45f);
    const float logMin = logf(minFrequency);
    const float logMax = logf(maxFrequency);
    for (int i = 0; i <= kNumBands; ++i) {
        float t = (float)i / (float)kNumBands;
        bandEdges_[i] = expf(logMin + (logMax - logMin) * t);
    }
    for (int i = 0; i < kNumBands; ++i)
        bandCenters_[i] = sqrtf(bandEdges_[i] * bandEdges_[i + 1]);

    resetFilterState();
    resetAnalysisState();
    updateBiquadCoefficients();
    updateAnalysisCoefficients();
}

void SpectralTarget::reset() {
    memset(bandsDb_, 0, sizeof(bandsDb_));
    memset(analysisEnergy_, 0, sizeof(analysisEnergy_));
    memset(watchDb_, 0, sizeof(watchDb_));
    memset(targetDb_, 0, sizeof(targetDb_));
    memset(targetM2Db_, 0, sizeof(targetM2Db_));
    memset(targetMinDb_, 0, sizeof(targetMinDb_));
    memset(targetMaxDb_, 0, sizeof(targetMaxDb_));
    memset(desiredGainDb_, 0, sizeof(desiredGainDb_));
    memset(appliedGainDb_, 0, sizeof(appliedGainDb_));
    targetCaptureCount_ = 0;
    targetReady_ = false;
    watchReady_ = false;
    matchReady_ = false;
    samplesUntilAnalysis_ = analysisHop_;
    resetFilterState();
    resetAnalysisState();
    updateBiquadCoefficients();
    updateAnalysisCoefficients();
}

void SpectralTarget::setMode(Mode mode) {
    if (mode_ == mode)
        return;

    mode_ = mode;
    if (mode_ == kMatch)
        matchReady_ = false;

    if (mode_ == kCapture) {
        targetCaptureCount_ = 0;
        targetReady_ = false;
        watchReady_ = false;
        matchReady_ = false;
        memset(targetDb_, 0, sizeof(targetDb_));
        memset(targetM2Db_, 0, sizeof(targetM2Db_));
        memset(targetMinDb_, 0, sizeof(targetMinDb_));
        memset(targetMaxDb_, 0, sizeof(targetMaxDb_));
        memset(watchDb_, 0, sizeof(watchDb_));
        memset(analysisEnergy_, 0, sizeof(analysisEnergy_));
        memset(desiredGainDb_, 0, sizeof(desiredGainDb_));
        memset(appliedGainDb_, 0, sizeof(appliedGainDb_));
        resetFilterState();
        resetAnalysisState();
        updateBiquadCoefficients();
    }
}

void SpectralTarget::setLearningRate(float value01) {
    learningRate_ = clamp(value01, 0.0f, 1.0f) * 0.10f;
}

void SpectralTarget::setLifterCutoff(int coefficientsToKeep) {
    lifterCutoff_ = (int)clamp((float)coefficientsToKeep, 2.0f, (float)(kFftSize / 2 - 1));
}

void SpectralTarget::setAnalysisSmoothing(float value01) {
    analysisSmoothing_ = clamp(value01, 0.0f, 1.0f);
}

void SpectralTarget::setAmount(float value01) {
    amount_ = clamp(value01, 0.0f, 1.0f);
}

void SpectralTarget::processBlock(const float* input, float* output, int numFrames, bool replaceOutput) {
    if (mode_ == kBypass) {
        for (int i = 0; i < numFrames; ++i)
            output[i] = replaceOutput ? input[i] : (output[i] + input[i]);
        return;
    }

    for (int i = 0; i < numFrames; ++i)
        pushAnalysisSample(input[i]);

    if (mode_ == kMatch && targetReady_ && amount_ > 0.0f) {
        int offset = 0;
        while (offset < numFrames) {
            const int chunk = std::min(kFftSize, numFrames - offset);
            arm_biquad_cascade_df2T_f32(&biquad_, const_cast<float*>(input + offset), fftInput_, (uint32_t)chunk);
            for (int i = 0; i < chunk; ++i) {
                float dry = input[offset + i];
                float wet = dry + (fftInput_[i] - dry) * amount_;
                output[offset + i] = replaceOutput ? wet : (output[offset + i] + wet);
            }
            offset += chunk;
        }
    } else {
        for (int i = 0; i < numFrames; ++i)
            output[i] = replaceOutput ? input[i] : (output[i] + input[i]);
    }

    runAnalysisIfDue();
}

void SpectralTarget::processBlockStereo(const float* inputL,
                                        const float* inputR,
                                        float* outputL,
                                        float* outputR,
                                        int numFrames,
                                        bool replaceOutputL,
                                        bool replaceOutputR) {
    if (mode_ == kBypass) {
        for (int i = 0; i < numFrames; ++i) {
            outputL[i] = replaceOutputL ? inputL[i] : (outputL[i] + inputL[i]);
            outputR[i] = replaceOutputR ? inputR[i] : (outputR[i] + inputR[i]);
        }
        return;
    }

    for (int i = 0; i < numFrames; ++i)
        pushAnalysisSample(0.5f * (inputL[i] + inputR[i]));

    if (mode_ == kMatch && targetReady_ && amount_ > 0.0f) {
        int offset = 0;
        while (offset < numFrames) {
            const int chunk = std::min(kFftSize, numFrames - offset);
            arm_biquad_cascade_df2T_f32(&biquad_, const_cast<float*>(inputL + offset), fftInput_, (uint32_t)chunk);
            arm_biquad_cascade_df2T_f32(&biquadR_, const_cast<float*>(inputR + offset), fftOutput_, (uint32_t)chunk);
            for (int i = 0; i < chunk; ++i) {
                const float dryL = inputL[offset + i];
                const float dryR = inputR[offset + i];
                const float wetL = dryL + (fftInput_[i] - dryL) * amount_;
                const float wetR = dryR + (fftOutput_[i] - dryR) * amount_;
                outputL[offset + i] = replaceOutputL ? wetL : (outputL[offset + i] + wetL);
                outputR[offset + i] = replaceOutputR ? wetR : (outputR[offset + i] + wetR);
            }
            offset += chunk;
        }
    } else {
        for (int i = 0; i < numFrames; ++i) {
            outputL[i] = replaceOutputL ? inputL[i] : (outputL[i] + inputL[i]);
            outputR[i] = replaceOutputR ? inputR[i] : (outputR[i] + inputR[i]);
        }
    }

    runAnalysisIfDue();
}

float SpectralTarget::bandGainDb(int band) const {
    if (band < 0 || band >= kNumBands)
        return 0.0f;
    return desiredGainDb_[band];
}

float SpectralTarget::correctionBandDb(int band) const {
    if (band < 0 || band >= kNumBands)
        return 0.0f;
    return appliedGainDb_[band];
}

float SpectralTarget::targetToleranceBandDb(int band) const {
    return targetToleranceDb(band);
}

float SpectralTarget::targetLowBandDb(int band) const {
    if (band < 0 || band >= kNumBands)
        return 0.0f;
    return targetMinDb_[band];
}

float SpectralTarget::targetHighBandDb(int band) const {
    if (band < 0 || band >= kNumBands)
        return 0.0f;
    return targetMaxDb_[band];
}

float SpectralTarget::targetM2BandDb(int band) const {
    if (band < 0 || band >= kNumBands)
        return 0.0f;
    return targetM2Db_[band];
}

void SpectralTarget::restoreTargetForPreset(int count, const float* mean, const float* m2, const float* minDb, const float* maxDb) {
    targetCaptureCount_ = std::max(0, count);
    targetReady_ = targetCaptureCount_ > 0;
    watchReady_ = false;
    matchReady_ = false;

    for (int i = 0; i < kNumBands; ++i) {
        const float centre = mean ? mean[i] : 0.0f;
        targetDb_[i] = centre;
        targetM2Db_[i] = m2 ? std::max(0.0f, m2[i]) : 0.0f;
        targetMinDb_[i] = minDb ? minDb[i] : centre;
        targetMaxDb_[i] = maxDb ? maxDb[i] : centre;
    }

    memset(desiredGainDb_, 0, sizeof(desiredGainDb_));
    memset(appliedGainDb_, 0, sizeof(appliedGainDb_));
    resetFilterState();
    updateBiquadCoefficients();
}

float SpectralTarget::liveBandDb(int band) const {
    if (band < 0 || band >= kNumBands)
        return 0.0f;
    return watchDb_[band];
}

float SpectralTarget::targetBandDb(int band) const {
    if (band < 0 || band >= kNumBands)
        return 0.0f;
    return targetDb_[band];
}

float SpectralTarget::excessBandDb(int band) const {
    if (band < 0 || band >= kNumBands || !targetReady_ || !watchReady_)
        return 0.0f;

    const float tolerance = targetToleranceDb(band);
    const float lower = targetDb_[band] - tolerance;
    const float upper = targetDb_[band] + tolerance;
    if (watchDb_[band] > upper)
        return watchDb_[band] - upper;
    if (watchDb_[band] < lower)
        return watchDb_[band] - lower;
    return 0.0f;
}

float SpectralTarget::averageExcessDb() const {
    if (!targetReady_ || !watchReady_)
        return 0.0f;

    float sum = 0.0f;
    for (int i = 0; i < kNumBands; ++i)
        sum += fabsf(excessBandDb(i));
    return sum / (float)kNumBands;
}

void SpectralTarget::setTargetBandsForTest(const float* db) {
    for (int i = 0; i < kNumBands; ++i) {
        targetDb_[i] = db[i];
        targetM2Db_[i] = 0.0f;
        targetMinDb_[i] = db[i];
        targetMaxDb_[i] = db[i];
    }
    targetCaptureCount_ = 1;
    targetReady_ = true;
    matchReady_ = false;
}

void SpectralTarget::updateMatchingForTest(const float* liveDb) {
    updateMatching(liveDb);
}

void SpectralTarget::analyseFrameForTest(const float* frame, float* outBandsDb) {
    analyseFrame(frame, outBandsDb);
}

void SpectralTarget::pushAnalysisSample(float sample) {
    for (int band = 0; band < kNumBands; ++band) {
        const float* c = analysisCoeffs_ + band * 5;
        float* st = analysisState_ + band * 2;
        const float y = c[0] * sample + st[0];
        st[0] = c[1] * sample + c[3] * y + st[1];
        st[1] = c[2] * sample + c[4] * y;
        analysisEnergy_[band] += y * y;
    }

    ++analysisSamples_;
    if (samplesUntilAnalysis_ > 0)
        --samplesUntilAnalysis_;
}

void SpectralTarget::runAnalysisIfDue() {
    if (samplesUntilAnalysis_ > 0 || analysisSamples_ <= 0)
        return;

    const float invSamples = 1.0f / (float)analysisSamples_;
    const float floorEnergy = 1.0e-12f;
    for (int i = 0; i < kNumBands; ++i)
        bandsDb_[i] = 10.0f * log10f(analysisEnergy_[i] * invSamples + floorEnergy);

    resetAnalysisState();
    samplesUntilAnalysis_ = (mode_ == kCapture) ? captureHop_ : analysisHop_;

    if (mode_ == kCapture) {
        captureTarget(bandsDb_);
    } else if (targetReady_ && (mode_ == kWatch || mode_ == kMatch)) {
        const float smoothing = watchReady_ ? (1.0f - analysisSmoothing_) : 1.0f;
        for (int i = 0; i < kNumBands; ++i)
            watchDb_[i] += smoothing * (bandsDb_[i] - watchDb_[i]);
        watchReady_ = true;

        if (mode_ == kMatch)
            updateMatching(watchDb_);
    }
}

void SpectralTarget::analyseFrameFast(const float* frame, float* outBandsDb) {
    for (int i = 0; i < kFftSize; ++i)
        fftInput_[i] = frame[i] * window_[i];

#if SPECTRAL_TARGET_USE_CMSIS
    arm_rfft_fast_f32(&fft_, fftInput_, fftOutput_, 0);

    const float floorMag = 1.0e-9f;
    cepstrum_[0] = log10f(fabsf(fftOutput_[0]) + floorMag);
    cepstrum_[kFftSize / 2] = log10f(fabsf(fftOutput_[1]) + floorMag);
    for (int bin = 1; bin < kFftSize / 2; ++bin) {
        float real = fftOutput_[2 * bin];
        float imag = fftOutput_[2 * bin + 1];
        cepstrum_[bin] = log10f(sqrtf(real * real + imag * imag) + floorMag);
    }
#else
    memset(fftOutput_, 0, sizeof(fftOutput_));
    complexFft(fftInput_, fftOutput_, false);

    const float floorMag = 1.0e-9f;
    cepstrum_[0] = log10f(fabsf(fftInput_[0]) + floorMag);
    cepstrum_[kFftSize / 2] = log10f(fabsf(fftInput_[kFftSize / 2]) + floorMag);
    for (int bin = 1; bin < kFftSize / 2; ++bin) {
        float real = fftInput_[bin];
        float imag = fftOutput_[bin];
        cepstrum_[bin] = log10f(sqrtf(real * real + imag * imag) + floorMag);
    }
#endif

    bandEnvelope(cepstrum_, outBandsDb);
}

void SpectralTarget::analyseFrame(const float* frame, float* outBandsDb) {
    analyseFrameFast(frame, outBandsDb);

    for (int bin = kFftSize / 2 + 1; bin < kFftSize; ++bin)
        cepstrum_[bin] = cepstrum_[kFftSize - bin];

    smoothLogMagnitude(cepstrum_);
    bandEnvelope(cepstrum_, outBandsDb);
}

void SpectralTarget::smoothLogMagnitude(float* logMag) {
#if SPECTRAL_TARGET_USE_CMSIS
    arm_rfft_fast_f32(&fft_, logMag, fftOutput_, 0);

    const int half = kFftSize / 2;
    for (int bin = lifterCutoff_ + 1; bin < half; ++bin) {
        fftOutput_[2 * bin] = 0.0f;
        fftOutput_[2 * bin + 1] = 0.0f;
    }
    if (lifterCutoff_ < half)
        fftOutput_[1] = 0.0f;

    arm_rfft_fast_f32(&fft_, fftOutput_, logMag, 1);
#else
    memcpy(fftInput_, logMag, sizeof(fftInput_));
    memset(fftOutput_, 0, sizeof(fftOutput_));
    complexFft(fftInput_, fftOutput_, false);

    const int keep = std::min(lifterCutoff_, kFftSize / 2 - 1);
    for (int bin = keep + 1; bin < kFftSize - keep; ++bin) {
        fftInput_[bin] = 0.0f;
        fftOutput_[bin] = 0.0f;
    }

    complexFft(fftInput_, fftOutput_, true);
    memcpy(logMag, fftInput_, sizeof(fftInput_));
#endif
}

#if !SPECTRAL_TARGET_USE_CMSIS
void SpectralTarget::complexFft(float* real, float* imag, bool inverse) {
    int j = 0;
    for (int i = 1; i < kFftSize; ++i) {
        int bit = kFftSize >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            std::swap(real[i], real[j]);
            std::swap(imag[i], imag[j]);
        }
    }

    const float sign = inverse ? 1.0f : -1.0f;
    for (int len = 2; len <= kFftSize; len <<= 1) {
        const float angle = sign * 6.2831853071795864769f / (float)len;
        const float wlenReal = cosf(angle);
        const float wlenImag = sinf(angle);

        for (int i = 0; i < kFftSize; i += len) {
            float wReal = 1.0f;
            float wImag = 0.0f;
            const int halfLen = len >> 1;
            for (int k = 0; k < halfLen; ++k) {
                const int even = i + k;
                const int odd = even + halfLen;
                const float oddReal = real[odd] * wReal - imag[odd] * wImag;
                const float oddImag = real[odd] * wImag + imag[odd] * wReal;
                const float evenReal = real[even];
                const float evenImag = imag[even];

                real[even] = evenReal + oddReal;
                imag[even] = evenImag + oddImag;
                real[odd] = evenReal - oddReal;
                imag[odd] = evenImag - oddImag;

                const float nextReal = wReal * wlenReal - wImag * wlenImag;
                wImag = wReal * wlenImag + wImag * wlenReal;
                wReal = nextReal;
            }
        }
    }

    if (inverse) {
        const float scale = 1.0f / (float)kFftSize;
        for (int i = 0; i < kFftSize; ++i) {
            real[i] *= scale;
            imag[i] *= scale;
        }
    }
}
#endif

void SpectralTarget::bandEnvelope(const float* smoothedLogMag, float* outBandsDb) const {
    for (int band = 0; band < kNumBands; ++band) {
        const float low = bandEdges_[band];
        const float high = bandEdges_[band + 1];
        float sum = 0.0f;
        int count = 0;
        int firstBin = std::max(1, (int)ceilf(low * (float)kFftSize / sampleRate_));
        int lastBin = std::min(kFftSize / 2, (int)floorf(high * (float)kFftSize / sampleRate_));
        if (lastBin < firstBin)
            lastBin = firstBin;
        for (int bin = firstBin; bin <= lastBin; ++bin) {
            sum += smoothedLogMag[bin];
            ++count;
        }
        outBandsDb[band] = 20.0f * (count > 0 ? sum / (float)count : smoothedLogMag[firstBin]);
    }
}

void SpectralTarget::captureTarget(const float* bandsDb) {
    const int count = std::min(targetCaptureCount_, 9999);
    const int newCount = count + 1;

    for (int i = 0; i < kNumBands; ++i) {
        const float x = bandsDb[i];
        if (count == 0) {
            targetDb_[i] = x;
            targetM2Db_[i] = 0.0f;
            targetMinDb_[i] = x;
            targetMaxDb_[i] = x;
        } else {
            const float delta = x - targetDb_[i];
            targetDb_[i] += delta / (float)newCount;
            const float delta2 = x - targetDb_[i];
            targetM2Db_[i] += delta * delta2;
            targetMinDb_[i] = std::min(targetMinDb_[i], x);
            targetMaxDb_[i] = std::max(targetMaxDb_[i], x);
        }
    }

    targetCaptureCount_ = newCount;
    targetReady_ = targetCaptureCount_ > 0;
    matchReady_ = false;
}

void SpectralTarget::updateMatching(const float* liveDb) {
    if (!targetReady_)
        return;

    float differences[kNumBands];
    float sorted[kNumBands];
    for (int i = 0; i < kNumBands; ++i) {
        differences[i] = targetDb_[i] - liveDb[i];
        sorted[i] = differences[i];
    }
    const float median = median16(sorted);

    for (int i = 0; i < kNumBands; ++i) {
        const float normalisedLive = liveDb[i] + median;
        const float tolerance = targetToleranceDb(i);
        const float lower = targetDb_[i] - tolerance;
        const float upper = targetDb_[i] + tolerance;
        float correction = 0.0f;
        if (normalisedLive < lower)
            correction = lower - normalisedLive;
        else if (normalisedLive > upper)
            correction = upper - normalisedLive;

        correction = clamp(correction, -kMaxGainDb, kMaxGainDb);
        if (!matchReady_)
            desiredGainDb_[i] = correction;
        else
            desiredGainDb_[i] += learningRate_ * (correction - desiredGainDb_[i]);
        appliedGainDb_[i] += 0.05f * (desiredGainDb_[i] - appliedGainDb_[i]);
    }
    matchReady_ = true;

    updateBiquadCoefficients();
}

void SpectralTarget::updateBiquadCoefficients() {
    makeLowShelf(sampleRate_, bandCenters_[0], 0.707f, appliedGainDb_[0], biquadCoeffs_);
    for (int band = 1; band < kNumBands - 1; ++band) {
        float bandwidth = std::max(1.0f, bandEdges_[band + 1] - bandEdges_[band]);
        float q = clamp(bandCenters_[band] / bandwidth, 0.35f, 4.0f);
        makePeaking(sampleRate_, bandCenters_[band], q, appliedGainDb_[band], biquadCoeffs_ + band * 5);
    }
    makeHighShelf(sampleRate_, bandCenters_[kNumBands - 1], 0.707f, appliedGainDb_[kNumBands - 1], biquadCoeffs_ + (kNumBands - 1) * 5);
    biquad_.numStages = kNumBands;
    biquad_.pCoeffs = biquadCoeffs_;
    biquad_.pState = biquadState_;
    biquadR_.numStages = kNumBands;
    biquadR_.pCoeffs = biquadCoeffs_;
    biquadR_.pState = biquadStateR_;
}

void SpectralTarget::updateAnalysisCoefficients() {
    for (int band = 0; band < kNumBands; ++band) {
        const float bandwidth = std::max(1.0f, bandEdges_[band + 1] - bandEdges_[band]);
        const float q = clamp(bandCenters_[band] / bandwidth, 0.35f, 4.0f);
        makeBandpass(sampleRate_, bandCenters_[band], q, analysisCoeffs_ + band * 5);
    }
}

void SpectralTarget::resetFilterState() {
    memset(biquadState_, 0, sizeof(biquadState_));
    memset(biquadStateR_, 0, sizeof(biquadStateR_));
}

void SpectralTarget::resetAnalysisState() {
    memset(analysisEnergy_, 0, sizeof(analysisEnergy_));
    memset(analysisState_, 0, sizeof(analysisState_));
    analysisSamples_ = 0;
}

float SpectralTarget::targetToleranceDb(int band) const {
    if (band < 0 || band >= kNumBands)
        return 1.5f;

    if (targetCaptureCount_ < 2)
        return 1.5f;

    const float variance = std::max(0.0f, targetM2Db_[band] / (float)(targetCaptureCount_ - 1));
    return clamp(1.5f * sqrtf(variance), 1.5f, 12.0f);
}

float SpectralTarget::clamp(float x, float lo, float hi) {
    return std::min(hi, std::max(lo, x));
}

float SpectralTarget::dbToLinear(float db) {
    return powf(10.0f, db / 20.0f);
}

float SpectralTarget::median16(float* values) {
    std::sort(values, values + kNumBands);
    return 0.5f * (values[7] + values[8]);
}

void SpectralTarget::makePeaking(float sampleRate, float frequency, float q, float gainDb, float* coeffs) {
    const float a = dbToLinear(gainDb);
    const float omega = 6.2831853071795864769f * clamp(frequency, 1.0f, sampleRate * 0.49f) / sampleRate;
    const float sn = sinf(omega);
    const float cs = cosf(omega);
    const float alpha = sn / (2.0f * std::max(0.05f, q));

    float b0 = 1.0f + alpha * a;
    float b1 = -2.0f * cs;
    float b2 = 1.0f - alpha * a;
    float a0 = 1.0f + alpha / a;
    float a1 = -2.0f * cs;
    float a2 = 1.0f - alpha / a;

    coeffs[0] = b0 / a0;
    coeffs[1] = b1 / a0;
    coeffs[2] = b2 / a0;
    coeffs[3] = -a1 / a0;
    coeffs[4] = -a2 / a0;
}

void SpectralTarget::makeLowShelf(float sampleRate, float frequency, float slope, float gainDb, float* coeffs) {
    const float a = sqrtf(dbToLinear(gainDb));
    const float omega = 6.2831853071795864769f * clamp(frequency, 1.0f, sampleRate * 0.49f) / sampleRate;
    const float sn = sinf(omega);
    const float cs = cosf(omega);
    const float alpha = sn / 2.0f * sqrtf((a + 1.0f / a) * (1.0f / std::max(0.05f, slope) - 1.0f) + 2.0f);
    const float beta = 2.0f * sqrtf(a) * alpha;

    float b0 = a * ((a + 1.0f) - (a - 1.0f) * cs + beta);
    float b1 = 2.0f * a * ((a - 1.0f) - (a + 1.0f) * cs);
    float b2 = a * ((a + 1.0f) - (a - 1.0f) * cs - beta);
    float a0 = (a + 1.0f) + (a - 1.0f) * cs + beta;
    float a1 = -2.0f * ((a - 1.0f) + (a + 1.0f) * cs);
    float a2 = (a + 1.0f) + (a - 1.0f) * cs - beta;

    coeffs[0] = b0 / a0;
    coeffs[1] = b1 / a0;
    coeffs[2] = b2 / a0;
    coeffs[3] = -a1 / a0;
    coeffs[4] = -a2 / a0;
}

void SpectralTarget::makeHighShelf(float sampleRate, float frequency, float slope, float gainDb, float* coeffs) {
    const float a = sqrtf(dbToLinear(gainDb));
    const float omega = 6.2831853071795864769f * clamp(frequency, 1.0f, sampleRate * 0.49f) / sampleRate;
    const float sn = sinf(omega);
    const float cs = cosf(omega);
    const float alpha = sn / 2.0f * sqrtf((a + 1.0f / a) * (1.0f / std::max(0.05f, slope) - 1.0f) + 2.0f);
    const float beta = 2.0f * sqrtf(a) * alpha;

    float b0 = a * ((a + 1.0f) + (a - 1.0f) * cs + beta);
    float b1 = -2.0f * a * ((a - 1.0f) + (a + 1.0f) * cs);
    float b2 = a * ((a + 1.0f) + (a - 1.0f) * cs - beta);
    float a0 = (a + 1.0f) - (a - 1.0f) * cs + beta;
    float a1 = 2.0f * ((a - 1.0f) - (a + 1.0f) * cs);
    float a2 = (a + 1.0f) - (a - 1.0f) * cs - beta;

    coeffs[0] = b0 / a0;
    coeffs[1] = b1 / a0;
    coeffs[2] = b2 / a0;
    coeffs[3] = -a1 / a0;
    coeffs[4] = -a2 / a0;
}

void SpectralTarget::makeBandpass(float sampleRate, float frequency, float q, float* coeffs) {
    const float omega = 6.2831853071795864769f * clamp(frequency, 1.0f, sampleRate * 0.49f) / sampleRate;
    const float sn = sinf(omega);
    const float cs = cosf(omega);
    const float alpha = sn / (2.0f * std::max(0.05f, q));

    const float b0 = alpha;
    const float b1 = 0.0f;
    const float b2 = -alpha;
    const float a0 = 1.0f + alpha;
    const float a1 = -2.0f * cs;
    const float a2 = 1.0f - alpha;

    coeffs[0] = b0 / a0;
    coeffs[1] = b1 / a0;
    coeffs[2] = b2 / a0;
    coeffs[3] = -a1 / a0;
    coeffs[4] = -a2 / a0;
}
