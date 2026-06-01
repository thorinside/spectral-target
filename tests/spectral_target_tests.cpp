#include "spectral_target.hpp"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void test_median_normalization_removes_loudness() {
    SpectralTarget target;
    target.prepare(48000.0f);
    target.setLearningRate(1.0f);

    float targetDb[SpectralTarget::kNumBands];
    float liveDb[SpectralTarget::kNumBands];
    for (int i = 0; i < SpectralTarget::kNumBands; ++i) {
        targetDb[i] = 0.0f;
        liveDb[i] = -6.0f;
    }

    target.setTargetBandsForTest(targetDb);
    target.updateMatchingForTest(liveDb);

    for (int i = 0; i < SpectralTarget::kNumBands; ++i)
        assert(fabsf(target.bandGainDb(i)) < 1.0e-5f);
}

static void test_matching_clamps_and_keeps_shape() {
    SpectralTarget target;
    target.prepare(48000.0f);
    target.setLearningRate(1.0f);

    float targetDb[SpectralTarget::kNumBands];
    float liveDb[SpectralTarget::kNumBands];
    for (int i = 0; i < SpectralTarget::kNumBands; ++i) {
        targetDb[i] = 0.0f;
        liveDb[i] = 0.0f;
    }
    targetDb[5] = 96.0f;

    target.setTargetBandsForTest(targetDb);
    for (int i = 0; i < 16; ++i)
        target.updateMatchingForTest(liveDb);

    assert(target.bandGainDb(5) > 0.0f);
    assert(target.bandGainDb(5) <= SpectralTarget::kMaxGainDb + 1.0e-5f);
    assert(fabsf(target.bandGainDb(0)) <= SpectralTarget::kMaxGainDb + 1.0e-5f);
}

static void test_amount_zero_is_dry_even_when_matching() {
    SpectralTarget target;
    target.prepare(48000.0f);
    target.setLearningRate(1.0f);

    float targetDb[SpectralTarget::kNumBands];
    float liveDb[SpectralTarget::kNumBands];
    for (int i = 0; i < SpectralTarget::kNumBands; ++i) {
        targetDb[i] = (i == 7) ? 24.0f : 0.0f;
        liveDb[i] = 0.0f;
    }
    target.setTargetBandsForTest(targetDb);
    target.updateMatchingForTest(liveDb);
    target.setMode(SpectralTarget::kMatch);
    target.setAmount(0.0f);

    float input[64];
    float output[64];
    for (int i = 0; i < 64; ++i) {
        input[i] = sinf(0.01f * (float)i);
        output[i] = -100.0f;
    }

    target.processBlock(input, output, 64, true);
    for (int i = 0; i < 64; ++i)
        assert(fabsf(output[i] - input[i]) < 1.0e-6f);
}

static void test_cepstral_analysis_is_finite() {
    SpectralTarget target;
    target.prepare(48000.0f);
    target.setLifterCutoff(24);

    float frame[SpectralTarget::kFftSize];
    float bands[SpectralTarget::kNumBands];
    for (int i = 0; i < SpectralTarget::kFftSize; ++i)
        frame[i] = 0.5f * sinf(2.0f * 3.14159265358979323846f * 440.0f * (float)i / 48000.0f);

    target.analyseFrameForTest(frame, bands);
    for (int i = 0; i < SpectralTarget::kNumBands; ++i)
        assert(isfinite(bands[i]));
}

int main() {
    test_median_normalization_removes_loudness();
    test_matching_clamps_and_keeps_shape();
    test_amount_zero_is_dry_even_when_matching();
    test_cepstral_analysis_is_finite();
    puts("spectral_target_tests passed");
    return 0;
}
