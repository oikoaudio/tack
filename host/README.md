# Host build of the sound engine

Builds the LXR sound engine (`Core/Src/Soundengine`) for a desktop computer, so it can be run, tested and listened
to without the hardware. Tested with gcc 15 on Linux x86-64.

```sh
cmake -S host -B build/host -G Ninja
cmake --build build/host
build/host/lxr_render kit.wav          # every voice of the init kit in turn
build/host/lxr_render kick.wav 0       # one voice: 0-2 drums, 3 snare, 4 cymbal, 5 closed hat, 6 open hat
build/host/lxr_render -r 48000 kit48.wav
build/host/lxr_render -k 00-LAMBO.SND lambo.wav   # a kit file from the device's SD card
```

`lxr_render` loads the factory init kit, triggers voices at velocity 127 and renders through the full mixer and FX
bus (`mixer_calcNextSampleBlock`). It writes a stereo WAV with both DAC pairs summed: at `REAL_FS` (44140 Hz) by
default, or through the resampler at the rate given with `-r`.

Needs libsamplerate. An installed copy (with its CMake package) is used if present; otherwise CMake fetches 0.2.2.

## Sample rate

The engine always runs at `REAL_FS`. Its filter cutoffs are fractions of the sample rate, envelopes advance by fixed
amounts per sample, and delay and reverb lengths are fixed sample counts, so running it at another rate would change
the sound. `lxr_host_process()` resamples the output to the host rate instead (libsamplerate, `SRC_SINC_MEDIUM_QUALITY`).

- Events (notes and parameter changes) are queued with their exact output time (`lxr_host_queueNoteOn`,
  `lxr_host_queueParam`) and come out a constant `lxr_host_latency()` later: 4 engine blocks, 5.8 ms, at 32 kHz and
  above, a little more at lower rates where the resampler reads further ahead. The engine computes envelopes, pitch
  sweeps and LFOs once per 64-sample block, as on the device, so an event lands on the block nearest its time:
  within ±0.73 ms of the constant latency. `build/host/timing_test [rate]` checks this.
- The noise oscillators draw from the RNG every sample, whether or not a voice is playing, so the noise in a hit
  depends on when it was triggered. Renders of snare, cymbal and hi-hats are not sample-identical across rates or
  trigger times, even though they sound the same. On the device the RNG is the STM32's hardware generator, so its
  noise never repeats either: compare those voices with the hardware by envelope and spectrum, not sample by sample.

Add `-DLXR_HOST_SANITIZE=ON` to build with AddressSanitizer and UBSan.

## Tack (CLAP plugin)

`build/host/Tack.clap` is a drum synth plugin built on the engine. It starts with the init kit and has no GUI
(hosts show their generic parameter editor). Notes from C3 trigger the voices chromatically, each at its default
pitch:

| Note | Voice |
|---|---|
| 60 (C3) | drum 1 |
| 61 (C#3) | drum 2 |
| 62 (D3) | drum 3 |
| 63 (D#3) | snare |
| 64 (E3) | cymbal |
| 65 (F3) | closed hi-hat |
| 66 (F#3) | open hi-hat |

C3 is MIDI note 60, as in Bitwig, Ableton and Cubase (REAPER calls it C4 by default).

- **Parameters:** all 227 sound parameters the front panel offers, per voice plus the FX bus, named as on the
  device's display with the abbreviations spelled out ("Drum 1 Filter Resonance"). `src/lxr_params.c` builds the
  list from the front panel's menu tables (`menuPages`, `valueNames`, `parameter_dtypes`, `modTargets`), so names,
  ranges and value texts follow the firmware; `build/host/list_params` prints it. Parameter ids are the firmware's
  parameter numbers, so they stay stable. Changes go through the same queue as notes, so automation lines up with
  them to within the same ±0.73 ms. The four FX
  parameters mean something different per FX type and are named with all their meanings ("FX Color/Decay/Range").
  LFO destinations cover every modulation target; the front panel's "destination voice" only narrowed its list and
  is left out. Reverb is compiled out of the firmware (`USE_REVERB 0`) and isn't offered.
- **Kits:** Tack offers kit files from the device's SD card (`.SND`) as presets, through CLAP's preset discovery,
  so hosts list them in their preset browser. It looks in `~/.local/share/Tack/Kits/` (or
  `$XDG_DATA_HOME/Tack/Kits/`); each subfolder is a sound pack, e.g. a symlink to a backup of a project folder. Kits
  named "Initkit" are skipped: the device fills empty kit slots with them, and the built-in "Init Kit" preset is the
  same sound. No kits are bundled with Tack; the factory and third-party kits belong to their authors. Linux only
  for now.
- **State:** the kit itself, in the firmware's representation.
- **Latency:** reported to the host, which compensates on playback.
- **Outputs:** nine stereo outputs: Main, Out 2, Drum 1, Drum 2, Drum 3, Snare, Cymbal, Hi-hat and FX. With the
  "Separate Outputs" parameter off (the default) only Main and Out 2 sound: the device's two output pairs, chosen by
  each voice's Audio Out parameter. With it on, each voice plays on its own output, panned, and the FX bus works as a
  send/return: a voice routed to the FX bus keeps its dry share (1 - dry/wet) on its own output and FX carries the
  effect's share. Every effect blends dry + (wet - dry) * dry/wet, so all outputs together still sum to the device's
  mix (bit-identical for the init kit, within a few LSB with the delay). A kit at 100% wet has no dry share, so all
  its FX-routed voices are only on FX. The switch is saved with the project, not with kits, and isn't automatable.
- **Instances:** the engine keeps its state in globals, so each instance runs its own copy of it. Tack loads a
  private copy of its own binary per instance (`clap/engine.c`, from an in-memory file) and drives that copy's
  engine through a function table, `tack_engine()`, the only symbol it exports besides `clap_entry`. Everything
  else has hidden visibility, so each copy's code uses its own globals. Instances can process in parallel.
  `build/host/multi_test Tack.clap kit.snd...` renders a different kit in each of several instances, interleaved
  and on one thread each, and checks every result against the kit rendered alone. Linux only for now (`memfd`).

To install it into `~/.clap`: `cmake --build build/host --target install_tack`. It copies the plugin in one step
rather than linking to the build output, so a host watching the folder never loads a half-written file mid-build.

`build/host/clap_render Tack.clap out.wav [rate] [-s] [-p kit.snd] [note | id=value | id=value@seconds ...]` is a
minimal CLAP host for testing without a DAW: it loads a kit through the preset-load extension, plays notes, sets
parameters through flush or as timed events, and with `-s` runs the state through save, a new instance and load
before playing. `clap-validator list presets build/host/Tack.clap` shows what the preset browser will see.

`clap-validator validate build/host/Tack.clap`: 38 passed, 0 failed, 6 skipped. The skipped tests are for
extensions Tack doesn't have (64-bit audio, configurable ports).

Not done yet: a GUI, macOS and Windows, per-voice MIDI channels, pitch from the note number.

## Layout

- `shim/` stands in for the STM32 HAL: C versions of the ARM SIMD intrinsics, `__fp16` as `_Float16`, a xorshift
  RNG, and dummy handles and GPIO ports. The GPIO input registers read 0, so the mixer sees no jacks plugged in and
  uses the routing from the kit unchanged.
- `src/stubs.c`: no-op sequencer, menu, UART and USB functions the engine links against.
- `src/lxr_host.c`: engine setup (the parts of `soundengine_init()` that don't touch the codec or sequencer), kit
  loading and parameter changes as the front panel sends them, block rendering, the resampler and event queue.
- `src/lxr_params.c`: the parameter list for plugins, from the menu tables. CMake copies `valueNames` and
  `parameter_dtypes` out of `menu.c` into `build/host/generated/menu_tables.inc`, since the rest of `menu.c` is LCD
  code that doesn't build on a desktop; the build stops if it can't find them.
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
  end of the array for the last one. `sine_table` has a guard sample and isn't affected. Fixed by wrapping to sample 0
  of the same table; the init kit renders bit-identically.
- `Delay.c`: switching the delay type from normal to ping-pong while the delay runs kept pointers that are only
  valid for the normal layout, and ping-pong indexes `FX_BUFFER` with `half + pointer`: up to index 49150 of a
  32768-entry buffer, which on the device sits in DTCM next to other data. Fixed with `Delay_setType()`, which masks
  the pointers for the new layout.
- `Distortion.c`: `diode()` read `table_1n914[257]`, one past the end, at full-scale input. The value is multiplied by
  0, so it only mattered if those bytes held a NaN or infinity. Fixed by interpolating from the last pair instead.
- Left as they are (harmless with GCC on x86 and ARM, but undefined in C): misaligned 32-bit loads in the delay's
  mono output path (`Delay.c:178`, `:264`) and a left shift of a negative number in `transientGenerator.c:60`.
- The kit loader also sets the velocity and LFO modulation targets (`preset_sendDrumsetParametersImmediate`); the
  first host harness skipped that. The init kit has none, so renders didn't change.
- Not a bug, but worth knowing when measuring: the first hit after start-up starts later than all others (41 samples
  into its block instead of 4 for the kick).
- `calcNoise()` reads the ARM overflow flag through `__get_APSR()`, which the shim can't provide, but it is only
  called from commented-out code. The live noise path, `calcNoiseBlock()`, detects phase wrap portably.
