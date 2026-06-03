# Spectral Target for disting NT

Spectral Target is a stereo tonal-balance matching effect for the Expert Sleepers disting NT.

It captures a reference tonal range, compares live input to that learned range with median-normalized band errors, and nudges a 16-band hybrid EQ bank only where the live signal falls outside the learned envelope. The audio path is zero-latency and uses a cascaded CMSIS-DSP DF2T biquad bank when built for hardware.

## Controls

- **Mode**: Bypass, Capture, Match, Watch.
- **Learn**: convergence speed for the matching optimizer; defaults to 0/off.
- **Smooth**: analysis smoothing/stability; higher values react more slowly.
- **Amount**: dry/wet amount of the correction filter; defaults to 100%.
- **Input L/R and Output L/R modes**: disting NT stereo routing.

### Physical UI

- **Pot L**: Learn.
- **Pot C**: Smooth.
- **Pot R**: Amount.
- **Pot L/C/R press**: reset Learn/Smooth/Amount to their defaults.
- **Encoder L**: change Mode.
- **Encoder R**: fine-trim Amount in 5% steps.
- **Encoder L press**: advance to the next Mode.
- **Encoder R press**: toggle between Watch and Match.

## Workflow

1. Set **Mode** to **Capture** and play the reference material until the target range is representative. Re-entering Capture clears the previous target and starts a new capture.
2. Switch to **Match** to apply the learned tonal-balance correction. The Match screen shows the current outside-range error and the correction being applied per band.
3. Switch to **Watch** when you only want metering. Watch shows how far the live input sits outside the learned range without changing the audio.

Captured targets are saved with disting NT presets, including the learned mean, range, and variance for all 16 bands. On preset load, the filter starts flat and rebuilds its correction from the restored target as new audio is analysed.

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

Run the qualitative audio-analysis pass:

```sh
make analysis
```

This renders a captured reference, a deliberately tilted live input, and the matched output under `build/analysis/`. It also writes `spectral_target_match_bands.csv` and `spectral_target_summary.csv` so you can inspect how much the Match path reduced the median-normalized band error after settling.

Build for nt_emu with the fallback shim:

```sh
make emu
```

Build the hardware plugin:

```sh
make hardware
```

Build artifacts are written to `plugins/`:

- `plugins/spectral_target.o` for the hardware plugin.
- `plugins/spectral_target.dylib` or `plugins/spectral_target.so` for `nt_emu`, depending on the host OS.

The default hardware build uses the built-in fallback FFT/biquad shim so the plugin does not depend on CMSIS-DSP symbols being exported by the disting NT firmware. If you are bundling/linking CMSIS-DSP yourself, opt in explicitly:

```sh
make hardware USE_CMSIS=1 CMSIS_DSP_DIR=/path/to/CMSIS-DSP CMSIS_CORE_DIR=/path/to/CMSIS-Core CMSIS_DSP_LIB=/path/to/libarm_cortexM7lfsp_math.a
```

`DISTINGNT_API_DIR` defaults to the `distingNT_API` git submodule and can be overridden if needed.
