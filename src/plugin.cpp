#include "spectral_target.hpp"

#include <stddef.h>
#include <math.h>
#include <distingnt/api.h>
#include <distingnt/serialisation.h>
#include <new>

static const char* kModeStrings[] = { "Bypass", "Capture", "Match", "Watch", NULL };

enum {
    kParamInputL,
    kParamInputR,
    kParamOutputL,
    kParamOutputLMode,
    kParamOutputR,
    kParamOutputRMode,
    kParamMode,
    kParamLearningRate,
    kParamSmoothing,
    kParamAmount,
    kNumParams,
};

static const _NT_parameter parameters[] = {
    NT_PARAMETER_AUDIO_INPUT("Input L", 1, 1)
    NT_PARAMETER_AUDIO_INPUT("Input R", 1, 2)
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE("Output L", 1, 13)
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE("Output R", 1, 14)
    { .name = "Mode", .min = 0, .max = 3, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = kModeStrings },
    { .name = "Learn", .min = 0, .max = 100, .def = 0, .unit = kNT_unitPercent, .scaling = 0, .enumStrings = NULL },
    { .name = "Smooth", .min = 0, .max = 100, .def = 85, .unit = kNT_unitPercent, .scaling = 0, .enumStrings = NULL },
    { .name = "Amount", .min = 0, .max = 100, .def = 100, .unit = kNT_unitPercent, .scaling = 0, .enumStrings = NULL },
};

static const uint8_t pageAnalysis[] = { kParamMode, kParamLearningRate, kParamSmoothing, kParamAmount };
static const uint8_t pageRouting[] = { kParamInputL, kParamInputR, kParamOutputL, kParamOutputLMode, kParamOutputR, kParamOutputRMode };

static const _NT_parameterPage parameterPages[] = {
    { .name = "Analysis", .numParams = ARRAY_SIZE(pageAnalysis), .group = 0, .unused = { 0, 0 }, .params = pageAnalysis },
    { .name = "Routing", .numParams = ARRAY_SIZE(pageRouting), .group = 0, .unused = { 0, 0 }, .params = pageRouting },
};

struct _spectralTargetAlgorithm : public _NT_algorithm {
    _spectralTargetAlgorithm() : dsp(NULL) {}
    SpectralTarget* dsp;
    _NT_parameterPages pages;
};

static void applyAllParameters(_spectralTargetAlgorithm* alg) {
    if (!alg || !alg->dsp || !alg->v)
        return;
    alg->dsp->setMode((SpectralTarget::Mode)alg->v[kParamMode]);
    alg->dsp->setLearningRate(alg->v[kParamLearningRate] * 0.01f);
    alg->dsp->setAnalysisSmoothing(alg->v[kParamSmoothing] * 0.01f);
    alg->dsp->setAmount(alg->v[kParamAmount] * 0.01f);
}

static void calculateRequirements(_NT_algorithmRequirements& req, const int32_t*) {
    req.numParameters = kNumParams;
    req.sram = sizeof(_spectralTargetAlgorithm);
    req.dram = sizeof(SpectralTarget);
    req.dtc = 0;
    req.itc = 0;
}

static _NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs,
                                const _NT_algorithmRequirements&,
                                const int32_t*) {
    _spectralTargetAlgorithm* alg = new (ptrs.sram) _spectralTargetAlgorithm();
    alg->dsp = new (ptrs.dram) SpectralTarget();
    alg->dsp->prepare((float)NT_globals.sampleRate);

    alg->parameters = parameters;
    alg->pages = { .numPages = ARRAY_SIZE(parameterPages), .pages = parameterPages };
    alg->parameterPages = &alg->pages;
    return alg;
}

static void parameterChanged(_NT_algorithm* self, int p) {
    _spectralTargetAlgorithm* alg = (_spectralTargetAlgorithm*)self;
    if (!alg || !alg->dsp)
        return;

    switch (p) {
    case kParamMode:
        alg->dsp->setMode((SpectralTarget::Mode)alg->v[kParamMode]);
        break;
    case kParamLearningRate:
        alg->dsp->setLearningRate(alg->v[kParamLearningRate] * 0.01f);
        break;
    case kParamSmoothing:
        alg->dsp->setAnalysisSmoothing(alg->v[kParamSmoothing] * 0.01f);
        break;
    case kParamAmount:
        alg->dsp->setAmount(alg->v[kParamAmount] * 0.01f);
        break;
    default:
        break;
    }
}

static void routeDryStereo(const float* inputL,
                           const float* inputR,
                           float* outputL,
                           float* outputR,
                           int numFrames,
                           bool replaceOutputL,
                           bool replaceOutputR) {
    for (int i = 0; i < numFrames; ++i) {
        outputL[i] = replaceOutputL ? inputL[i] : (outputL[i] + inputL[i]);
        outputR[i] = replaceOutputR ? inputR[i] : (outputR[i] + inputR[i]);
    }
}

static void step(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
    _spectralTargetAlgorithm* alg = (_spectralTargetAlgorithm*)self;
    if (!alg || !alg->dsp)
        return;

    if (numFramesBy4 <= 0)
        return;

    applyAllParameters(alg);

    const int numFrames = numFramesBy4 * 4;
    const int inputLBus = alg->v[kParamInputL] - 1;
    const int inputRBus = alg->v[kParamInputR] - 1;
    const int outputLBus = alg->v[kParamOutputL] - 1;
    const int outputRBus = alg->v[kParamOutputR] - 1;
    if (inputLBus < 0 || inputRBus < 0 || outputLBus < 0 || outputRBus < 0)
        return;

    const float* inputL = busFrames + inputLBus * numFrames;
    const float* inputR = busFrames + inputRBus * numFrames;
    float* outputL = busFrames + outputLBus * numFrames;
    float* outputR = busFrames + outputRBus * numFrames;
    const bool replaceL = alg->v[kParamOutputLMode];
    const bool replaceR = alg->v[kParamOutputRMode];

    if (alg->v[kParamMode] == SpectralTarget::kBypass) {
        routeDryStereo(inputL, inputR, outputL, outputR, numFrames, replaceL, replaceR);
        return;
    }

    alg->dsp->processBlockStereo(inputL, inputR, outputL, outputR, numFrames, replaceL, replaceR);
}

static int clampInt(int value, int lo, int hi) {
    return value < lo ? lo : (value > hi ? hi : value);
}

static int percentFromPot(float value) {
    return clampInt((int)(value * 100.0f + 0.5f), 0, 100);
}

static void setParameterFromUi(_NT_algorithm* self, int parameter, int value) {
    const int32_t algorithmIndex = NT_algorithmIndex(self);
    if (algorithmIndex < 0)
        return;
    NT_setParameterFromUi((uint32_t)algorithmIndex, (uint32_t)parameter + NT_parameterOffset(), (int16_t)value);
}

static int nextMode(int mode, int delta) {
    int next = mode + delta;
    while (next < SpectralTarget::kBypass)
        next += 4;
    while (next > SpectralTarget::kWatch)
        next -= 4;
    return next;
}

static uint32_t hasCustomUi(_NT_algorithm*) {
    return kNT_potL | kNT_potC | kNT_potR
         | kNT_potButtonL | kNT_potButtonC | kNT_potButtonR
         | kNT_encoderL | kNT_encoderR
         | kNT_encoderButtonL | kNT_encoderButtonR;
}

static void customUi(_NT_algorithm* self, const _NT_uiData& data) {
    _spectralTargetAlgorithm* alg = (_spectralTargetAlgorithm*)self;
    if (!alg || !alg->dsp || !alg->v)
        return;

    if (data.controls & kNT_potL)
        setParameterFromUi(self, kParamLearningRate, percentFromPot(data.pots[0]));
    if (data.controls & kNT_potC)
        setParameterFromUi(self, kParamSmoothing, percentFromPot(data.pots[1]));
    if (data.controls & kNT_potR)
        setParameterFromUi(self, kParamAmount, percentFromPot(data.pots[2]));

    const uint16_t pressed = data.controls & ~data.lastButtons;
    if (pressed & kNT_potButtonL)
        setParameterFromUi(self, kParamLearningRate, 0);
    if (pressed & kNT_potButtonC)
        setParameterFromUi(self, kParamSmoothing, 85);
    if (pressed & kNT_potButtonR)
        setParameterFromUi(self, kParamAmount, 100);

    if (data.encoders[0] != 0)
        setParameterFromUi(self, kParamMode, nextMode(alg->v[kParamMode], data.encoders[0]));
    if (data.encoders[1] != 0)
        setParameterFromUi(self, kParamAmount, clampInt(alg->v[kParamAmount] + data.encoders[1] * 5, 0, 100));

    if (pressed & kNT_encoderButtonL)
        setParameterFromUi(self, kParamMode, nextMode(alg->v[kParamMode], 1));
    if (pressed & kNT_encoderButtonR)
        setParameterFromUi(self, kParamMode, alg->v[kParamMode] == SpectralTarget::kWatch ? SpectralTarget::kMatch : SpectralTarget::kWatch);
}

static void setupUi(_NT_algorithm* self, _NT_float3& pots) {
    if (!self || !self->v)
        return;
    pots[0] = clampInt(self->v[kParamLearningRate], 0, 100) * 0.01f;
    pots[1] = clampInt(self->v[kParamSmoothing], 0, 100) * 0.01f;
    pots[2] = clampInt(self->v[kParamAmount], 0, 100) * 0.01f;
}

static int textAscent(_NT_textSize size) {
    switch (size) {
    case kNT_textTiny:
        return 5;
    case kNT_textLarge:
        return 21;
    case kNT_textNormal:
    default:
        return 8;
    }
}

static void safeDrawText(int x, int baselineY, const char* text, int colour, _NT_textAlignment align, _NT_textSize size) {
    // Text y is a baseline, so keep the glyphs below the top edge. Also stay
    // one pixel inside the display bounds; some drawing paths are not clipped.
    NT_drawText(clampInt(x, 0, 254), clampInt(baselineY, textAscent(size), 62), text, clampInt(colour, 0, 15), align, size);
}

static void safeDrawShapeI(_NT_shape shape, int x0, int y0, int x1, int y1, int colour) {
    // Stay one pixel inside the display bounds and skip degenerate rectangles.
    // The firmware drawing code should not be asked to clip or normalise for us.
    x0 = clampInt(x0, 0, 254);
    x1 = clampInt(x1, 0, 254);
    y0 = clampInt(y0, 0, 62);
    y1 = clampInt(y1, 0, 62);

    if (x1 < x0) {
        const int t = x0;
        x0 = x1;
        x1 = t;
    }
    if (y1 < y0) {
        const int t = y0;
        y0 = y1;
        y1 = t;
    }

    if (shape == kNT_rectangle && (x1 <= x0 || y1 <= y0))
        return;

    NT_drawShapeI(shape, x0, y0, x1, y1, clampInt(colour, 0, 15));
}

static void drawWatch(SpectralTarget* dsp) {
    safeDrawText(8, 15, "Spectral Target: Watch", 15, kNT_textLeft, kNT_textNormal);

    if (!dsp->targetReady()) {
        safeDrawText(8, 28, "capture target first", 12, kNT_textLeft, kNT_textNormal);
        return;
    }

    if (!dsp->watchReady()) {
        safeDrawText(8, 28, "listening...", 12, kNT_textLeft, kNT_textNormal);
        return;
    }

    safeDrawText(8, 20, "outside target range", 10, kNT_textLeft, kNT_textNormal);
    safeDrawText(166, 20, "avg", 10, kNT_textLeft, kNT_textNormal);
    char value[kNT_parameterStringSize];
    NT_floatToString(value, dsp->averageExcessDb(), 1);
    safeDrawText(196, 20, value, 12, kNT_textLeft, kNT_textNormal);
    safeDrawText(230, 20, "dB", 10, kNT_textLeft, kNT_textNormal);

    const int graphLeft = 8;
    const int graphTop = 28;
    const int graphBottom = 60;
    const int graphMid = 45;
    const int barPitch = 15;
    const int barWidth = 10;
    safeDrawShapeI(kNT_line, graphLeft, graphMid, 248, graphMid, 6);

    for (int band = 0; band < SpectralTarget::kNumBands; ++band) {
        const float outside = dsp->excessBandDb(band);
        if (outside == 0.0f)
            continue;

        const int height = clampInt((int)(fabsf(outside) * 2.0f + 0.5f), 1, graphMid - graphTop);
        const int x0 = graphLeft + band * barPitch;
        const int colour = fabsf(outside) > 6.0f ? 15 : (fabsf(outside) > 3.0f ? 12 : 8);
        if (outside > 0.0f)
            safeDrawShapeI(kNT_rectangle, x0, graphMid - height, x0 + barWidth, graphMid - 1, colour);
        else
            safeDrawShapeI(kNT_rectangle, x0, graphMid + 1, x0 + barWidth, clampInt(graphMid + height, graphTop, graphBottom), colour);
    }
}

static void drawMatch(SpectralTarget* dsp) {
    safeDrawText(8, 15, "Spectral Target: Match", 15, kNT_textLeft, kNT_textNormal);

    if (!dsp->targetReady()) {
        safeDrawText(8, 28, "capture target first", 12, kNT_textLeft, kNT_textNormal);
        return;
    }

    safeDrawText(8, 20, "live + correction", 10, kNT_textLeft, kNT_textNormal);

    const int graphLeft = 8;
    const int graphTop = 28;
    const int graphBottom = 60;
    const int graphMid = 45;
    const int graphRight = 246;
    const int graphHalfHeight = graphMid - graphTop;
    const int barPitch = 15;
    const int barWidth = 10;
    const float dbToPixels = (float)graphHalfHeight / SpectralTarget::kMaxGainDb;

    // Keep this deliberately conservative: no large filled background rectangle.
    // Some firmware drawing paths appear not to clip robustly on every primitive.
    safeDrawShapeI(kNT_box, graphLeft, graphTop, graphRight, graphBottom, 5);
    safeDrawShapeI(kNT_line, graphLeft, graphMid, graphRight, graphMid, 7);

    for (int band = 0; band < SpectralTarget::kNumBands; ++band) {
        const int x0 = graphLeft + band * barPitch;
        const int xCenter = x0 + barWidth / 2;

        // Dark vertical marker: learned tonal range width for this band. The
        // full graph height represents +/- kMaxGainDb around the band centre.
        const int tolerancePixels = clampInt((int)(dsp->targetToleranceBandDb(band) * dbToPixels + 0.5f), 1, graphHalfHeight);
        safeDrawShapeI(kNT_line, xCenter, graphMid - tolerancePixels, xCenter, graphMid + tolerancePixels, 3);
        safeDrawShapeI(kNT_line, xCenter - 1, graphMid - tolerancePixels, xCenter - 1, graphMid + tolerancePixels, 2);

        const float outside = dsp->excessBandDb(band);
        if (outside != 0.0f) {
            const int liveHeight = clampInt((int)(fabsf(outside) * dbToPixels + 0.5f), 1, graphHalfHeight);
            const int liveColour = fabsf(outside) > 6.0f ? 10 : (fabsf(outside) > 3.0f ? 8 : 6);
            if (outside > 0.0f)
                safeDrawShapeI(kNT_rectangle, x0, graphMid - liveHeight, x0 + barWidth, graphMid - 1, liveColour);
            else
                safeDrawShapeI(kNT_rectangle, x0, graphMid + 1, x0 + barWidth, clampInt(graphMid + liveHeight, graphTop, graphBottom), liveColour);
        }

        const float correction = dsp->correctionBandDb(band);
        if (correction == 0.0f)
            continue;

        const int height = clampInt((int)(fabsf(correction) * dbToPixels + 0.5f), 1, graphHalfHeight);
        const int colour = fabsf(correction) > 6.0f ? 15 : (fabsf(correction) > 3.0f ? 13 : 11);
        if (correction > 0.0f)
            safeDrawShapeI(kNT_rectangle, x0 + 3, graphMid - height, x0 + barWidth - 3, graphMid - 1, colour);
        else
            safeDrawShapeI(kNT_rectangle, x0 + 3, graphMid + 1, x0 + barWidth - 3, clampInt(graphMid + height, graphTop, graphBottom), colour);
    }
}

static void serialiseFloatArray(_NT_jsonStream& stream, const char* name, SpectralTarget* dsp, float (SpectralTarget::*getter)(int) const) {
    stream.addMemberName(name);
    stream.openArray();
    for (int band = 0; band < SpectralTarget::kNumBands; ++band)
        stream.addNumber((dsp->*getter)(band));
    stream.closeArray();
}

static void serialise(_NT_algorithm* self, _NT_jsonStream& stream) {
    _spectralTargetAlgorithm* alg = (_spectralTargetAlgorithm*)self;
    if (!alg || !alg->dsp)
        return;

    stream.addMemberName("spectral_target_version");
    stream.addNumber(1);

    stream.addMemberName("mode");
    stream.addNumber((int)alg->dsp->mode());

    stream.addMemberName("target_count");
    stream.addNumber(alg->dsp->targetCaptureCount());

    serialiseFloatArray(stream, "target_mean", alg->dsp, &SpectralTarget::targetBandDb);
    serialiseFloatArray(stream, "target_m2", alg->dsp, &SpectralTarget::targetM2BandDb);
    serialiseFloatArray(stream, "target_min", alg->dsp, &SpectralTarget::targetLowBandDb);
    serialiseFloatArray(stream, "target_max", alg->dsp, &SpectralTarget::targetHighBandDb);
}

static bool parseFloatArray(_NT_jsonParse& parse, float* values) {
    int count = 0;
    if (!parse.numberOfArrayElements(count) || count != SpectralTarget::kNumBands)
        return false;

    for (int i = 0; i < SpectralTarget::kNumBands; ++i) {
        if (!parse.number(values[i]))
            return false;
    }
    return true;
}

static bool deserialise(_NT_algorithm* self, _NT_jsonParse& parse) {
    _spectralTargetAlgorithm* alg = (_spectralTargetAlgorithm*)self;
    if (!alg || !alg->dsp)
        return false;

    int numMembers = 0;
    if (!parse.numberOfObjectMembers(numMembers))
        return false;

    int savedMode = -1;
    int targetCount = 0;
    bool haveMean = false;
    bool haveM2 = false;
    bool haveMin = false;
    bool haveMax = false;
    float mean[SpectralTarget::kNumBands];
    float m2[SpectralTarget::kNumBands];
    float minDb[SpectralTarget::kNumBands];
    float maxDb[SpectralTarget::kNumBands];
    for (int i = 0; i < SpectralTarget::kNumBands; ++i) {
        mean[i] = 0.0f;
        m2[i] = 0.0f;
        minDb[i] = 0.0f;
        maxDb[i] = 0.0f;
    }

    for (int member = 0; member < numMembers; ++member) {
        if (parse.matchName("spectral_target_version")) {
            int version = 0;
            if (!parse.number(version))
                return false;
        } else if (parse.matchName("mode")) {
            if (!parse.number(savedMode))
                return false;
        } else if (parse.matchName("target_count")) {
            if (!parse.number(targetCount))
                return false;
        } else if (parse.matchName("target_mean")) {
            if (!parseFloatArray(parse, mean))
                return false;
            haveMean = true;
        } else if (parse.matchName("target_m2")) {
            if (!parseFloatArray(parse, m2))
                return false;
            haveM2 = true;
        } else if (parse.matchName("target_min")) {
            if (!parseFloatArray(parse, minDb))
                return false;
            haveMin = true;
        } else if (parse.matchName("target_max")) {
            if (!parseFloatArray(parse, maxDb))
                return false;
            haveMax = true;
        } else {
            if (!parse.skipMember())
                return false;
        }
    }

    if (haveMean)
        alg->dsp->restoreTargetForPreset(targetCount, mean, haveM2 ? m2 : NULL, haveMin ? minDb : NULL, haveMax ? maxDb : NULL);

    if (savedMode >= SpectralTarget::kBypass && savedMode <= SpectralTarget::kWatch)
        alg->dsp->setMode((SpectralTarget::Mode)savedMode);

    return true;
}

static bool draw(_NT_algorithm* self) {
    _spectralTargetAlgorithm* alg = (_spectralTargetAlgorithm*)self;
    if (!alg || !alg->dsp)
        return false;

    const SpectralTarget::Mode mode = alg->dsp->mode();
    if (mode == SpectralTarget::kWatch) {
        drawWatch(alg->dsp);
        return true;
    }
    if (mode == SpectralTarget::kMatch) {
        drawMatch(alg->dsp);
        return true;
    }

    const char* modeString = (mode >= SpectralTarget::kBypass && mode <= SpectralTarget::kWatch) ? kModeStrings[mode] : "?";
    safeDrawText(8, 15, "Spectral Target", 15, kNT_textLeft, kNT_textNormal);
    safeDrawText(8, 28, modeString, 12, kNT_textLeft, kNT_textNormal);
    safeDrawText(8, 42, alg->dsp->targetReady() ? "target ready" : "capture target", 10, kNT_textLeft, kNT_textNormal);
    return true;
}

static const _NT_factory factory = {
    .guid = NT_MULTICHAR('T', 'h', 'S', 't'),
    .name = "Spectral Target",
    .description = "Cepstral tonal balance matcher",
    .numSpecifications = 0,
    .specifications = NULL,
    .calculateStaticRequirements = NULL,
    .initialise = NULL,
    .calculateRequirements = calculateRequirements,
    .construct = construct,
    .parameterChanged = parameterChanged,
    .step = step,
    .draw = draw,
    .midiRealtime = NULL,
    .midiMessage = NULL,
    .tags = kNT_tagEffect | kNT_tagFilterEQ,
    .hasCustomUi = hasCustomUi,
    .customUi = customUi,
    .setupUi = setupUi,
    .serialise = serialise,
    .deserialise = deserialise,
    .midiSysEx = NULL,
    .parameterUiPrefix = NULL,
    .parameterString = NULL,
};

extern "C" uintptr_t pluginEntry(_NT_selector selector, uint32_t data) {
    switch (selector) {
    case kNT_selector_version:
        return kNT_apiVersionCurrent;
    case kNT_selector_numFactories:
        return 1;
    case kNT_selector_factoryInfo:
        return (uintptr_t)((data == 0) ? &factory : NULL);
    }
    return 0;
}
