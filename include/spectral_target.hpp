#pragma once

#include <stdint.h>

#if !defined(SPECTRAL_TARGET_FORCE_FALLBACK)
#if defined(__has_include)
#if __has_include(<arm_math.h>)
#define SPECTRAL_TARGET_USE_CMSIS 1
#endif
#endif
#endif

#if !defined(SPECTRAL_TARGET_USE_CMSIS)
#define SPECTRAL_TARGET_USE_CMSIS 0
#endif

#if SPECTRAL_TARGET_USE_CMSIS
#include <arm_math.h>
#else
struct arm_rfft_fast_instance_f32 {
    uint16_t fftLenRFFT;
};

struct arm_biquad_cascade_df2T_instance_f32 {
    uint8_t numStages;
    float* pState;
    float* pCoeffs;
};

int arm_rfft_fast_init_f32(arm_rfft_fast_instance_f32* s, uint16_t fftLen);
void arm_rfft_fast_f32(arm_rfft_fast_instance_f32* s, float* p, float* pOut, uint8_t ifftFlag);
void arm_biquad_cascade_df2T_init_f32(arm_biquad_cascade_df2T_instance_f32* s,
                                      uint8_t numStages,
                                      float* pCoeffs,
                                      float* pState);
void arm_biquad_cascade_df2T_f32(const arm_biquad_cascade_df2T_instance_f32* s,
                                 const float* pSrc,
                                 float* pDst,
                                 uint32_t blockSize);
#endif

class SpectralTarget {
public:
    enum Mode {
        kBypass = 0,
        kCapture = 1,
        kMatch = 2,
        kWatch = 3,
    };

    static const int kFftSize = 1024;
    static const int kNumBins = kFftSize / 2 + 1;
    static const int kNumBands = 16;
    static const float kMaxGainDb;

    SpectralTarget();

    void prepare(float sampleRate);
    void reset();

    void setMode(Mode mode);
    void setLearningRate(float value01);
    void setLifterCutoff(int coefficientsToKeep);
    void setAmount(float value01);

    void processBlock(const float* input, float* output, int numFrames, bool replaceOutput);
    void processBlockStereo(const float* inputL,
                            const float* inputR,
                            float* outputL,
                            float* outputR,
                            int numFrames,
                            bool replaceOutputL,
                            bool replaceOutputR);

    bool targetReady() const { return targetReady_; }
    bool watchReady() const { return watchReady_; }
    Mode mode() const { return mode_; }
    float bandGainDb(int band) const;
    float liveBandDb(int band) const;
    float targetBandDb(int band) const;
    float excessBandDb(int band) const;
    float averageExcessDb() const;

    // Small deterministic hooks used by the host-side regression tests.
    void setTargetBandsForTest(const float* db);
    void updateMatchingForTest(const float* liveDb);
    void analyseFrameForTest(const float* frame, float* outBandsDb);

private:
    void pushAnalysisSample(float sample);
    void runAnalysisIfDue();
    bool currentFrame(float* frame) const;
    void analyseFrame(const float* frame, float* outBandsDb);
    void analyseFrameFast(const float* frame, float* outBandsDb);
    void smoothLogMagnitude(float* logMag);
#if !SPECTRAL_TARGET_USE_CMSIS
    void complexFft(float* real, float* imag, bool inverse);
#endif
    void bandEnvelope(const float* smoothedLogMag, float* outBandsDb) const;
    void captureTarget(const float* bandsDb);
    void updateMatching(const float* liveDb);
    void updateBiquadCoefficients();
    void updateAnalysisCoefficients();
    void resetFilterState();
    void resetAnalysisState();

    static float clamp(float x, float lo, float hi);
    static float dbToLinear(float db);
    static float median16(float* values);
    static void makePeaking(float sampleRate, float frequency, float q, float gainDb, float* coeffs);
    static void makeLowShelf(float sampleRate, float frequency, float slope, float gainDb, float* coeffs);
    static void makeHighShelf(float sampleRate, float frequency, float slope, float gainDb, float* coeffs);
    static void makeBandpass(float sampleRate, float frequency, float q, float* coeffs);

    Mode mode_;
    float sampleRate_;
    int analysisHop_;
    int captureHop_;
    int samplesUntilAnalysis_;
    int lifterCutoff_;
    float learningRate_;
    float amount_;

    float ring_[kFftSize];
    int ringWrite_;
    int ringFill_;

    float window_[kFftSize];
    float fftInput_[kFftSize];
    float fftOutput_[kFftSize];
    float cepstrum_[kFftSize];
    float bandsDb_[kNumBands];
    float analysisEnergy_[kNumBands];
    float watchDb_[kNumBands];
    float targetDb_[kNumBands];
    float desiredGainDb_[kNumBands];
    float appliedGainDb_[kNumBands];
    float bandEdges_[kNumBands + 1];
    float bandCenters_[kNumBands];
    int targetCaptureCount_;
    bool targetReady_;
    bool watchReady_;

    arm_rfft_fast_instance_f32 fft_;
    arm_biquad_cascade_df2T_instance_f32 biquad_;
    arm_biquad_cascade_df2T_instance_f32 biquadR_;
    float biquadCoeffs_[kNumBands * 5];
    float biquadState_[kNumBands * 2];
    float biquadStateR_[kNumBands * 2];
    float analysisCoeffs_[kNumBands * 5];
    float analysisState_[kNumBands * 2];
    int analysisSamples_;
};
