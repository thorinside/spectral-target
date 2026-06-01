# Spectral Target for disting NT

Spectral Target is a mono tonal-balance matching effect for the Expert Sleepers disting NT.

It captures a reference spectral envelope, compares live input to that target with median-normalized band errors, and nudges a 16-band hybrid EQ bank toward the target shape. The audio path is zero-latency and uses a cascaded CMSIS-DSP DF2T biquad bank when built for hardware.

## Controls

- **Mode**: Bypass, Capture, Match.
- **Learn**: convergence speed for the matching optimizer.
- **Detail**: cepstral lifter cutoff; higher values retain more envelope detail.
- **Amount**: dry/wet amount of the correction filter.
- **Input/Output/Output mode**: disting NT routing.

## Build

Host regression tests use the built-in fallback DSP shim:

```sh
make test
```

Build for nt_emu with the fallback shim:

```sh
make emu
```

Build the hardware plugin with CMSIS-DSP available on the include path:

```sh
make hardware CMSIS_DSP_DIR=/path/to/CMSIS-DSP CMSIS_CORE_DIR=/path/to/CMSIS-Core CMSIS_DSP_LIB=/path/to/libarm_cortexM7lfsp_math.a
```

`DISTINGNT_API_DIR` and `CMSIS_DSP_DIR` default to local checkouts under `/Users/nealsanche/nosuch` and can be overridden. Provide `CMSIS_DSP_LIB` to resolve the CMSIS FFT/biquad objects into the relocatable plugin.
