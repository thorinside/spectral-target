#include "spectral_target.hpp"

#include <stddef.h>
#include <distingnt/api.h>
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
    { .name = "Learn", .min = 0, .max = 100, .def = 40, .unit = kNT_unitPercent, .scaling = 0, .enumStrings = NULL },
    { .name = "Detail", .min = 2, .max = 96, .def = 32, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
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
    alg->dsp->setLifterCutoff(alg->v[kParamSmoothing]);
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
        alg->dsp->setLifterCutoff(alg->v[kParamSmoothing]);
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

static void drawWatch(SpectralTarget* dsp) {
    NT_drawText(8, 8, "Spectral Target: Watch", 15, kNT_textLeft, kNT_textNormal);

    if (!dsp->targetReady()) {
        NT_drawText(8, 28, "capture target first", 12, kNT_textLeft, kNT_textNormal);
        return;
    }

    if (!dsp->watchReady()) {
        NT_drawText(8, 28, "listening...", 12, kNT_textLeft, kNT_textNormal);
        return;
    }

    NT_drawText(8, 20, "excess over target", 10, kNT_textLeft, kNT_textNormal);
    NT_drawText(166, 20, "avg", 10, kNT_textLeft, kNT_textNormal);
    char value[kNT_parameterStringSize];
    NT_floatToString(value, dsp->averageExcessDb(), 1);
    NT_drawText(196, 20, value, 12, kNT_textLeft, kNT_textNormal);
    NT_drawText(230, 20, "dB", 10, kNT_textLeft, kNT_textNormal);

    const int graphLeft = 8;
    const int graphTop = 28;
    const int graphBottom = 61;
    const int barPitch = 15;
    const int barWidth = 10;
    NT_drawShapeI(kNT_line, graphLeft, graphBottom, 248, graphBottom, 6);

    for (int band = 0; band < SpectralTarget::kNumBands; ++band) {
        const float excess = dsp->excessBandDb(band);
        if (excess <= 0.0f)
            continue;

        const int height = clampInt((int)(excess * 2.0f + 0.5f), 1, graphBottom - graphTop);
        const int x0 = graphLeft + band * barPitch;
        const int y0 = graphBottom - height;
        const int colour = excess > 6.0f ? 15 : (excess > 3.0f ? 12 : 8);
        NT_drawShapeI(kNT_rectangle, x0, y0, x0 + barWidth, graphBottom - 1, colour);
    }
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

    const char* modeString = (mode >= SpectralTarget::kBypass && mode <= SpectralTarget::kWatch) ? kModeStrings[mode] : "?";
    NT_drawText(8, 12, "Spectral Target", 15, kNT_textLeft, kNT_textNormal);
    NT_drawText(8, 26, modeString, 12, kNT_textLeft, kNT_textNormal);
    NT_drawText(8, 40, alg->dsp->targetReady() ? "target ready" : "capture target", 10, kNT_textLeft, kNT_textNormal);
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
    .hasCustomUi = NULL,
    .customUi = NULL,
    .setupUi = NULL,
    .serialise = NULL,
    .deserialise = NULL,
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
