# Spectral Target for disting NT

Spectral Target is a stereo tonal-balance matching effect for the Expert Sleepers disting NT.

It captures a reference tonal range, compares live input to that learned range with median-normalized band errors, and nudges a 16-band hybrid EQ bank only where the live signal falls outside the learned envelope. The audio path is zero-latency and uses a cascaded CMSIS-DSP DF2T biquad bank when built for hardware.

## Controls

- **Mode**: Bypass, Capture, Match, Watch.
- **Learn**: convergence speed for the matching optimizer; defaults to 0/off.
- **Smooth**: analysis smoothing/stability; higher values react more slowly.
- **Amount**: dry/wet amount of the correction filter; defaults to 100%.
- **Input L/R and Output L/R modes**: disting NT stereo routing.

## Build

Fetch the actual Expert Sleepers disting NT API headers first:

```sh
git submodule update --init --recursive
# or: make deps
```

Host regression tests use the built-in fallback DSP shim:

```sh
make test
```

Build for nt_emu with the fallback shim:

```sh
make emu
```

Build the hardware plugin:

```sh
make hardware
```

The default hardware build uses the built-in fallback FFT/biquad shim so the plugin does not depend on CMSIS-DSP symbols being exported by the disting NT firmware. If you are bundling/linking CMSIS-DSP yourself, opt in explicitly:

```sh
make hardware USE_CMSIS=1 CMSIS_DSP_DIR=/path/to/CMSIS-DSP CMSIS_CORE_DIR=/path/to/CMSIS-Core CMSIS_DSP_LIB=/path/to/libarm_cortexM7lfsp_math.a
```

`DISTINGNT_API_DIR` defaults to the `distingNT_API` git submodule and can be overridden if needed.
