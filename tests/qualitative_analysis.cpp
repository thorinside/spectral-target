#include "spectral_target.hpp"

#include <algorithm>
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>

namespace {

const float kSampleRate = 48000.0f;
const int kBlockFrames = 64;
const int kCaptureFrames = (int)(kSampleRate * 5.0f);
const int kMatchFrames = (int)(kSampleRate * 30.0f);
const int kMeasureFrames = (int)(kSampleRate * 5.0f);
const float kPi = 3.14159265358979323846f;
const float kTwoPi = 2.0f * kPi;

struct BandProfile {
    float db[SpectralTarget::kNumBands];
};

float clampFloat(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

float dbToLinear(float db) {
    return powf(10.0f, db / 20.0f);
}

float median16(float* values) {
    std::sort(values, values + SpectralTarget::kNumBands);
    return 0.5f * (values[7] + values[8]);
}

float bandCentre(int band) {
    const float minFrequency = 31.25f;
    const float maxFrequency = 20000.0f;
    const float logMin = logf(minFrequency);
    const float logMax = logf(maxFrequency);
    const float edge0 = expf(logMin + (logMax - logMin) * (float)band / (float)SpectralTarget::kNumBands);
    const float edge1 = expf(logMin + (logMax - logMin) * (float)(band + 1) / (float)SpectralTarget::kNumBands);
    return sqrtf(edge0 * edge1);
}

float programSample(int frame, bool tiltedLive) {
    float y = 0.0f;
    const float t = (float)frame / kSampleRate;
    for (int band = 0; band < SpectralTarget::kNumBands; ++band) {
        const float centre = bandCentre(band);
        const float phase = 0.173f * (float)(band + 1);
        const float referenceDb = -16.0f - 0.45f * (float)band
                                + 3.0f * sinf(0.58f * (float)band);
        const float tiltDb = tiltedLive ? (-6.0f + 12.0f * (float)band / 15.0f) : 0.0f;
        const float amp = dbToLinear(referenceDb + tiltDb);
        y += amp * sinf(kTwoPi * centre * t + phase);
        y += 0.35f * amp * sinf(kTwoPi * centre * 1.017f * t + phase * 1.7f);
    }

    // Slow broadband level movement keeps the capture/match path closer to
    // program material than a static oscillator bank, while staying deterministic.
    const float envelope = 0.82f + 0.18f * sinf(kTwoPi * 0.37f * t);
    return clampFloat(y * envelope * 0.42f, -0.95f, 0.95f);
}

void renderProgram(std::vector<float>& out, bool tiltedLive) {
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = programSample((int)i, tiltedLive);
}

void analyseProfile(const std::vector<float>& audio, int offset, int frames, BandProfile& profile) {
    SpectralTarget analyser;
    analyser.prepare(kSampleRate);

    for (int band = 0; band < SpectralTarget::kNumBands; ++band)
        profile.db[band] = 0.0f;

    float frame[SpectralTarget::kFftSize];
    float bands[SpectralTarget::kNumBands];
    int count = 0;
    const int end = std::min<int>((int)audio.size(), offset + frames);
    for (int pos = offset; pos + SpectralTarget::kFftSize <= end; pos += SpectralTarget::kFftSize) {
        memcpy(frame, audio.data() + pos, sizeof(frame));
        analyser.analyseFrameForTest(frame, bands);
        for (int band = 0; band < SpectralTarget::kNumBands; ++band)
            profile.db[band] += bands[band];
        ++count;
    }

    assert(count > 0);
    for (int band = 0; band < SpectralTarget::kNumBands; ++band)
        profile.db[band] /= (float)count;
}

float shapeErrorDb(const BandProfile& target, const BandProfile& candidate, float* perBandAbsError) {
    float diff[SpectralTarget::kNumBands];
    float sorted[SpectralTarget::kNumBands];
    for (int band = 0; band < SpectralTarget::kNumBands; ++band) {
        diff[band] = target.db[band] - candidate.db[band];
        sorted[band] = diff[band];
    }

    const float loudnessOffset = median16(sorted);
    float sum = 0.0f;
    for (int band = 0; band < SpectralTarget::kNumBands; ++band) {
        const float error = fabsf(diff[band] - loudnessOffset);
        if (perBandAbsError)
            perBandAbsError[band] = error;
        sum += error;
    }
    return sum / (float)SpectralTarget::kNumBands;
}

float rms(const std::vector<float>& audio, int offset, int frames) {
    const int end = std::min<int>((int)audio.size(), offset + frames);
    double sum = 0.0;
    int count = 0;
    for (int i = offset; i < end; ++i) {
        sum += (double)audio[i] * (double)audio[i];
        ++count;
    }
    return count > 0 ? (float)sqrt(sum / (double)count) : 0.0f;
}

float peakAbs(const std::vector<float>& audio) {
    float peak = 0.0f;
    for (size_t i = 0; i < audio.size(); ++i)
        peak = std::max(peak, fabsf(audio[i]));
    return peak;
}

void runCapture(SpectralTarget& dsp, const std::vector<float>& reference) {
    std::vector<float> out(kBlockFrames);
    dsp.setMode(SpectralTarget::kCapture);
    for (int offset = 0; offset < (int)reference.size(); offset += kBlockFrames) {
        const int n = std::min(kBlockFrames, (int)reference.size() - offset);
        dsp.processBlock(reference.data() + offset, out.data(), n, true);
    }
}

void runMatch(SpectralTarget& dsp, const std::vector<float>& live, std::vector<float>& matched) {
    std::vector<float> out(kBlockFrames);
    dsp.setMode(SpectralTarget::kMatch);
    dsp.setLearningRate(1.0f);
    dsp.setAnalysisSmoothing(0.35f);
    dsp.setAmount(1.0f);

    for (int offset = 0; offset < (int)live.size(); offset += kBlockFrames) {
        const int n = std::min(kBlockFrames, (int)live.size() - offset);
        dsp.processBlock(live.data() + offset, out.data(), n, true);
        memcpy(matched.data() + offset, out.data(), n * sizeof(float));
    }
}

void writeCsv(const char* path,
              const SpectralTarget& dsp,
              const BandProfile& target,
              const BandProfile& live,
              const BandProfile& matched,
              const float* liveError,
              const float* matchedError) {
    FILE* f = fopen(path, "w");
    assert(f);
    fprintf(f, "band,centre_hz,captured_target_db,measured_target_db,live_db,matched_db,live_abs_error_db,matched_abs_error_db,applied_correction_db\n");
    for (int band = 0; band < SpectralTarget::kNumBands; ++band) {
        fprintf(f, "%d,%.3f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",
                band,
                bandCentre(band),
                dsp.targetBandDb(band),
                target.db[band],
                live.db[band],
                matched.db[band],
                liveError[band],
                matchedError[band],
                dsp.correctionBandDb(band));
    }
    fclose(f);
}

void writeSummary(const char* path,
                  float liveError,
                  float matchedError,
                  float liveRms,
                  float matchedRms,
                  float matchedPeak,
                  float lowCorrectionDb,
                  float highCorrectionDb) {
    FILE* f = fopen(path, "w");
    assert(f);
    fprintf(f, "metric,value\n");
    fprintf(f, "live_mean_abs_shape_error_db,%.6f\n", liveError);
    fprintf(f, "matched_mean_abs_shape_error_db,%.6f\n", matchedError);
    fprintf(f, "shape_error_improvement_db,%.6f\n", liveError - matchedError);
    fprintf(f, "shape_error_ratio,%.6f\n", matchedError / std::max(0.001f, liveError));
    fprintf(f, "live_rms,%.6f\n", liveRms);
    fprintf(f, "matched_rms,%.6f\n", matchedRms);
    fprintf(f, "matched_peak,%.6f\n", matchedPeak);
    fprintf(f, "low_band_mean_correction_db,%.6f\n", lowCorrectionDb);
    fprintf(f, "high_band_mean_correction_db,%.6f\n", highCorrectionDb);
    fclose(f);
}

void writeWav16(const char* path, const std::vector<float>& audio) {
    FILE* f = fopen(path, "wb");
    assert(f);

    const uint16_t channels = 1;
    const uint32_t sampleRate = (uint32_t)kSampleRate;
    const uint16_t bitsPerSample = 16;
    const uint16_t blockAlign = channels * bitsPerSample / 8;
    const uint32_t byteRate = sampleRate * blockAlign;
    const uint32_t dataBytes = (uint32_t)(audio.size() * blockAlign);
    const uint32_t riffBytes = 36u + dataBytes;

    fwrite("RIFF", 1, 4, f);
    fwrite(&riffBytes, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    const uint32_t fmtBytes = 16;
    const uint16_t audioFormat = 1;
    fwrite(&fmtBytes, 4, 1, f);
    fwrite(&audioFormat, 2, 1, f);
    fwrite(&channels, 2, 1, f);
    fwrite(&sampleRate, 4, 1, f);
    fwrite(&byteRate, 4, 1, f);
    fwrite(&blockAlign, 2, 1, f);
    fwrite(&bitsPerSample, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&dataBytes, 4, 1, f);

    for (size_t i = 0; i < audio.size(); ++i) {
        const float x = clampFloat(audio[i], -1.0f, 1.0f);
        const int16_t sample = (int16_t)lrintf(x * 32767.0f);
        fwrite(&sample, 2, 1, f);
    }
    fclose(f);
}

const char* joinPath(char* buffer, size_t bufferSize, const char* dir, const char* file) {
    snprintf(buffer, bufferSize, "%s/%s", dir, file);
    return buffer;
}

} // namespace

int main(int argc, char** argv) {
    const char* outDir = argc > 1 ? argv[1] : "build/analysis";

    std::vector<float> reference(kCaptureFrames);
    std::vector<float> live(kMatchFrames);
    std::vector<float> matched(kMatchFrames, 0.0f);
    renderProgram(reference, false);
    renderProgram(live, true);

    SpectralTarget dsp;
    dsp.prepare(kSampleRate);
    runCapture(dsp, reference);
    assert(dsp.targetReady());
    assert(dsp.targetCaptureCount() >= 4);
    runMatch(dsp, live, matched);

    BandProfile targetProfile;
    BandProfile liveProfile;
    BandProfile matchedProfile;
    analyseProfile(reference, std::max(0, (int)reference.size() - kMeasureFrames), kMeasureFrames, targetProfile);
    analyseProfile(live, std::max(0, (int)live.size() - kMeasureFrames), kMeasureFrames, liveProfile);
    analyseProfile(matched, std::max(0, (int)matched.size() - kMeasureFrames), kMeasureFrames, matchedProfile);

    float liveErrorByBand[SpectralTarget::kNumBands];
    float matchedErrorByBand[SpectralTarget::kNumBands];
    const float liveError = shapeErrorDb(targetProfile, liveProfile, liveErrorByBand);
    const float matchedError = shapeErrorDb(targetProfile, matchedProfile, matchedErrorByBand);
    const float liveRms = rms(live, (int)live.size() - kMeasureFrames, kMeasureFrames);
    const float matchedRms = rms(matched, (int)matched.size() - kMeasureFrames, kMeasureFrames);
    const float matchedPeak = peakAbs(matched);
    float lowCorrectionDb = 0.0f;
    float highCorrectionDb = 0.0f;
    for (int band = 0; band < 5; ++band)
        lowCorrectionDb += dsp.correctionBandDb(band);
    for (int band = 11; band < SpectralTarget::kNumBands; ++band)
        highCorrectionDb += dsp.correctionBandDb(band);
    lowCorrectionDb /= 5.0f;
    highCorrectionDb /= 5.0f;

    char path[512];
    writeCsv(joinPath(path, sizeof(path), outDir, "spectral_target_match_bands.csv"),
             dsp,
             targetProfile,
             liveProfile,
             matchedProfile,
             liveErrorByBand,
             matchedErrorByBand);
    writeSummary(joinPath(path, sizeof(path), outDir, "spectral_target_summary.csv"),
                 liveError,
                 matchedError,
                 liveRms,
                 matchedRms,
                 matchedPeak,
                 lowCorrectionDb,
                 highCorrectionDb);
    writeWav16(joinPath(path, sizeof(path), outDir, "reference_capture.wav"), reference);
    writeWav16(joinPath(path, sizeof(path), outDir, "live_tilted_input.wav"), live);
    writeWav16(joinPath(path, sizeof(path), outDir, "matched_output.wav"), matched);

    printf("spectral_target qualitative analysis\n");
    printf("  target captures: %d\n", dsp.targetCaptureCount());
    printf("  live shape error: %.3f dB\n", liveError);
    printf("  matched shape error: %.3f dB\n", matchedError);
    printf("  improvement: %.3f dB\n", liveError - matchedError);
    printf("  matched rms: %.4f peak: %.4f\n", matchedRms, matchedPeak);
    printf("  mean correction low/high: %.3f / %.3f dB\n", lowCorrectionDb, highCorrectionDb);
    printf("  outputs: %s\n", outDir);

    assert(matchedPeak < 1.0f);
    assert(matchedRms > 0.005f);
    assert(matchedError < liveError * 0.85f);
    assert(liveError - matchedError > 2.0f);
    assert(lowCorrectionDb > 0.25f);
    assert(highCorrectionDb < -0.25f);

    return 0;
}
