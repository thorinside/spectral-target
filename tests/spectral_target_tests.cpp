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

static void test_matching_uses_target_range_deadband() {
    SpectralTarget target;
    target.prepare(48000.0f);
    target.setLearningRate(1.0f);

    float targetDb[SpectralTarget::kNumBands];
    float liveDb[SpectralTarget::kNumBands];
    for (int i = 0; i < SpectralTarget::kNumBands; ++i) {
        targetDb[i] = 0.0f;
        liveDb[i] = 0.0f;
    }
    targetDb[5] = 1.0f;

    target.setTargetBandsForTest(targetDb);
    target.updateMatchingForTest(liveDb);
    assert(fabsf(target.bandGainDb(5)) < 1.0e-5f);
}

static void test_matching_converges_without_integrator_overshoot() {
    SpectralTarget target;
    target.prepare(48000.0f);
    target.setLearningRate(1.0f);

    float targetDb[SpectralTarget::kNumBands];
    float liveDb[SpectralTarget::kNumBands];
    for (int i = 0; i < SpectralTarget::kNumBands; ++i) {
        targetDb[i] = 0.0f;
        liveDb[i] = 0.0f;
    }
    targetDb[5] = 4.0f;

    target.setTargetBandsForTest(targetDb);
    for (int i = 0; i < 64; ++i)
        target.updateMatchingForTest(liveDb);

    assert(target.bandGainDb(5) > 2.4f);
    assert(target.bandGainDb(5) < 2.6f);
}

static void test_learn_zero_seeds_then_freezes_match_curve() {
    SpectralTarget target;
    target.prepare(48000.0f);
    target.setLearningRate(0.0f);

    float targetDb[SpectralTarget::kNumBands];
    float liveDb[SpectralTarget::kNumBands];
    for (int i = 0; i < SpectralTarget::kNumBands; ++i) {
        targetDb[i] = 0.0f;
        liveDb[i] = 0.0f;
    }
    targetDb[5] = 4.0f;

    target.setTargetBandsForTest(targetDb);
    target.updateMatchingForTest(liveDb);
    const float seededGain = target.bandGainDb(5);
    assert(seededGain > 2.4f);
    assert(seededGain < 2.6f);

    liveDb[5] = 8.0f;
    target.updateMatchingForTest(liveDb);
    assert(target.bandGainDb(5) > 2.4f);
    assert(target.bandGainDb(5) < 2.6f);
}

static void test_bypass_stereo_is_dry() {
    SpectralTarget target;
    target.prepare(48000.0f);
    target.setMode(SpectralTarget::kBypass);

    float inputL[64];
    float inputR[64];
    float outputL[64];
    float outputR[64];
    for (int i = 0; i < 64; ++i) {
        inputL[i] = sinf(0.03f * (float)i);
        inputR[i] = cosf(0.02f * (float)i);
        outputL[i] = -10.0f;
        outputR[i] = 10.0f;
    }

    target.processBlockStereo(inputL, inputR, outputL, outputR, 64, true, true);
    for (int i = 0; i < 64; ++i) {
        assert(fabsf(outputL[i] - inputL[i]) < 1.0e-6f);
        assert(fabsf(outputR[i] - inputR[i]) < 1.0e-6f);
    }
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

static void test_match_mode_filters_audio_when_amount_full() {
    SpectralTarget target;
    target.prepare(48000.0f);
    target.setLearningRate(1.0f);

    float targetDb[SpectralTarget::kNumBands];
    float liveDb[SpectralTarget::kNumBands];
    for (int i = 0; i < SpectralTarget::kNumBands; ++i) {
        targetDb[i] = 0.0f;
        liveDb[i] = 0.0f;
    }
    targetDb[7] = 24.0f;

    target.setTargetBandsForTest(targetDb);
    for (int i = 0; i < 64; ++i)
        target.updateMatchingForTest(liveDb);
    target.setMode(SpectralTarget::kMatch);
    target.setAmount(1.0f);

    float input[128];
    float output[128];
    memset(input, 0, sizeof(input));
    memset(output, 0, sizeof(output));
    input[0] = 1.0f;

    target.processBlock(input, output, 128, true);

    float difference = 0.0f;
    for (int i = 0; i < 128; ++i)
        difference += fabsf(output[i] - input[i]);
    assert(difference > 1.0e-4f);
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
    test_matching_uses_target_range_deadband();
    test_matching_converges_without_integrator_overshoot();
    test_learn_zero_seeds_then_freezes_match_curve();
    test_bypass_stereo_is_dry();
    test_amount_zero_is_dry_even_when_matching();
    test_match_mode_filters_audio_when_amount_full();
    test_cepstral_analysis_is_finite();
    puts("spectral_target_tests passed");
    return 0;
}
