#include "spectral_target.hpp"

#include <distingnt/api.h>
#include <new>

static const char* kModeStrings[] = { "Bypass", "Capture", "Match", NULL };

enum {
    kParamInput,
    kParamOutput,
    kParamOutputMode,
    kParamMode,
    kParamLearningRate,
    kParamSmoothing,
    kParamAmount,
    kNumParams,
};

static const _NT_parameter parameters[] = {
    NT_PARAMETER_AUDIO_INPUT("Input", 1, 1)
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE("Output", 1, 13)
    { .name = "Mode", .min = 0, .max = 2, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = kModeStrings },
    { .name = "Learn", .min = 0, .max = 100, .def = 40, .unit = kNT_unitPercent, .scaling = 0, .enumStrings = NULL },
    { .name = "Detail", .min = 2, .max = 96, .def = 32, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    { .name = "Amount", .min = 0, .max = 100, .def = 100, .unit = kNT_unitPercent, .scaling = 0, .enumStrings = NULL },
};

static const uint8_t pageAnalysis[] = { kParamMode, kParamLearningRate, kParamSmoothing, kParamAmount };
static const uint8_t pageRouting[] = { kParamInput, kParamOutput, kParamOutputMode };

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

static void step(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
    _spectralTargetAlgorithm* alg = (_spectralTargetAlgorithm*)self;
    if (!alg || !alg->dsp)
        return;

    if (numFramesBy4 <= 0)
        return;

    applyAllParameters(alg);

    const int numFrames = numFramesBy4 * 4;
    const int inputBus = alg->v[kParamInput] - 1;
    const int outputBus = alg->v[kParamOutput] - 1;
    if (inputBus < 0 || outputBus < 0)
        return;

    const float* input = busFrames + inputBus * numFrames;
    float* output = busFrames + outputBus * numFrames;
    const bool replace = alg->v[kParamOutputMode];
    alg->dsp->processBlock(input, output, numFrames, replace);
}

static bool draw(_NT_algorithm* self) {
    _spectralTargetAlgorithm* alg = (_spectralTargetAlgorithm*)self;
    if (!alg || !alg->dsp)
        return false;

    const char* mode = kModeStrings[alg->dsp->mode()];
    NT_drawText(8, 12, "Spectral Target", 15, kNT_textLeft, kNT_textNormal);
    NT_drawText(8, 26, mode, 12, kNT_textLeft, kNT_textNormal);
    NT_drawText(8, 40, alg->dsp->targetReady() ? "target ready" : "capture target", 10, kNT_textLeft, kNT_textNormal);
    return false;
}

static const _NT_factory factory = {
    .guid = NT_MULTICHAR('N', 's', 'S', 't'),
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
