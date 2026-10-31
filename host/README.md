# Host build of the sound engine

Builds the LXR sound engine (`Core/Src/Soundengine`) for a desktop computer, so it can be run, tested and listened
to without the hardware. Tested with gcc 15 on Linux x86-64.

```sh
cmake -S host -B build/host -G Ninja
cmake --build build/host
build/host/lxr_render kit.wav          # every voice of the init kit in turn
build/host/lxr_render kick.wav 0       # one voice: 0-2 drums, 3 snare, 4 cymbal, 5 closed hat, 6 open hat
build/host/lxr_render -r 48000 kit48.wav
```

`lxr_render` loads the factory init kit, triggers voices at velocity 127 and renders through the full mixer and FX
bus (`mixer_calcNextSampleBlock`). It writes a stereo WAV with both DAC pairs summed: at `REAL_FS` (44140 Hz) by
default, or through the resampler at the rate given with `-r`.

Needs libsamplerate. An installed copy (with its CMake package) is used if present; otherwise CMake fetches 0.2.2.

## Sample rate

The engine always runs at `REAL_FS`. Its filter cutoffs are fractions of the sample rate, envelopes advance by fixed
amounts per sample, and delay and reverb lengths are fixed sample counts, so running it at another rate would change
the sound. `lxr_host_process()` resamples the output to the host rate instead (libsamplerate, `SRC_SINC_MEDIUM_QUALITY`).

- Note ons take effect at the next engine block, so their timing is quantised to 64 samples at 44140 Hz (1.45 ms).
  With the resampler, a single hit comes out about 2.9 ms after the note-on at 48 kHz.
- The noise oscillators draw from the RNG every sample, whether or not a voice is playing, so the noise in a hit
  depends on when it was triggered. Renders of snare, cymbal and hi-hats are not sample-identical across rates or
  trigger times, even though they sound the same. On the device the RNG is the STM32's hardware generator, so its
  noise never repeats either: compare those voices with the hardware by envelope and spectrum, not sample by sample.

Add `-DLXR_HOST_SANITIZE=ON` to build with AddressSanitizer and UBSan.

## Layout

- `shim/` stands in for the STM32 HAL: C versions of the ARM SIMD intrinsics, `__fp16` as `_Float16`, a xorshift
  RNG, and dummy handles and GPIO ports. The GPIO input registers read 0, so the mixer sees no jacks plugged in and
  uses the routing from the kit unchanged.
- `src/stubs.c`: no-op sequencer, menu, UART and USB functions the engine links against.
- `src/lxr_host.c`: engine setup (the parts of `soundengine_init()` that don't touch the codec or sequencer), kit
  loading, block rendering.
- `tools/`: Python scripts for inspecting renders, e.g.
  `uv run --no-project --with numpy --with matplotlib python host/tools/overview.py out.png kick=kick.wav`.

## Firmware issues found while porting

- `MidiParser.c`: loading a kit wrote one byte far past `midiParser_originalCcValues` (`PAR_BANK_CHANGE` is sent as
  CC 0, so `paramNr` underflowed to 65535). Fixed.
- `parameterArray[i].ptr` is NULL for "no modulation target", and the modulation code dereferences it. On the STM32
  that reads flash at address 0; on a PC it segfaults. `lxr_host_init()` points those entries at a dummy instead of
  changing the firmware.
- `Oscillator.c`: the interpolating wavetable oscillators read `table[offset][itg + 1]` with no wrap, so at the end
  of each cycle they read index 1024 of a `[11][1024]` table: sample 0 of the next band-limited table, or past the
  end of the array for the last one. `sine_table` has a guard sample and isn't affected. Not fixed yet, to keep the
  output identical to the hardware.
- `calcNoise()` reads the ARM overflow flag through `__get_APSR()`, which the shim can't provide, but it is only
  called from commented-out code. The live noise path, `calcNoiseBlock()`, detects phase wrap portably.
