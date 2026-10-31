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

`build/host/Tack.clap` is a drum synth plugin built on the engine. It starts with the init kit. The MIDI channel
decides how notes play:

| Channel | Plays |
|---|---|
| 1 | the kit: notes from C3 trigger the voices, each at its tuned pitch (table below) |
| 2 - 8 | one voice each (drum 1, drum 2, drum 3, snare, cymbal, closed hi-hat, open hi-hat), chromatically: C3 plays it at its tuned pitch, other notes transpose it |
| 9 - 16 | nothing |

| Note on channel 1 | Voice |
|---|---|
| 60 (C3) | drum 1 |
| 61 (C#3) | drum 2 |
| 62 (D3) | drum 3 |
| 63 (D#3) | snare |
| 64 (E3) | cymbal |
| 65 (F3) | closed hi-hat |
| 66 (F#3) | open hi-hat |

C3 is MIDI note 60, as in Bitwig, Ableton and Cubase (REAPER calls it C4 by default).

Transposing works as on the device, where a sequencer step or an incoming note sets the pitch: a voice moves by
(note - 60) semitones, both its oscillators together, so FM timbres and the hats' and cymbal's metallic ratios keep
their character, and the pitch envelope scales with the note. The snare's noise and the transient samples don't
move. An oscillator stops at MIDI note 127 (12.5 kHz), so voices tuned near the top, like the init kit's cymbal and
the hats' second mod oscillator, only go down. The noise waveform is sample-and-hold at the oscillator's frequency,
so the note sets how bright it is. Each voice is monophonic:
a new note retriggers it. Note-offs are ignored, as on the device, since every voice is one-shot.

- **Parameters:** all 227 sound parameters the front panel offers, per voice plus the FX bus, named as on the
  device's display with the abbreviations spelled out ("Drum 1 Filter Resonance"). `src/lxr_params.c` builds the
  list from the front panel's menu tables (`menuPages`, `valueNames`, `parameter_dtypes`, `modTargets`), so names,
  ranges and value texts follow the firmware; `build/host/list_params` prints it. Parameter ids are the firmware's
  parameter numbers, so they stay stable. Changes go through the same queue as notes, so automation lines up with
  them to within the same ±0.73 ms. The amounts (every parameter with a knob) can be modulated by the host: its
  modulators add to the parameter's value without moving it, as CLAP modulation does, and the editor shows the
  modulation as a thin ring around the knob. The sum is rounded to the parameter's steps and kept in its range, so
  modulation is as coarse as automation; options (waveforms, destinations, outputs) aren't modulatable. The four FX
  parameters mean something different per FX type and are named with all their meanings ("FX Color/Decay/Range").
  LFO destinations cover every modulation target; the front panel's "destination voice" only narrowed its list and
  is left out. Its target list has the FX parameters once per voice, so Tack lists each target once, and names
  targets like the parameters (the front panel calls the FM waveform "Oscillator Waveform" there). Reverb is compiled out of the firmware (`USE_REVERB 0`) and isn't offered.
- **Kits:** Tack offers kit files from the device's SD card (`.SND`) as presets, through CLAP's preset discovery,
  so hosts list them in their preset browser. It looks in `~/.local/share/Tack/Kits/` (or
  `$XDG_DATA_HOME/Tack/Kits/`); each subfolder is a sound pack, e.g. a symlink to a backup of a project folder. Kits
  named "Initkit" are skipped, also when the device garbled two of its letters: the device fills empty kit slots
  with them, and the built-in "Init Kit" preset is the same sound. No kits are bundled with Tack; the factory and
  third-party kits belong to their authors. Linux only for now.
- **Choosing kits in the editor:** the same kits are in the editor too. Clicking the kit's name opens a browser: the
  kit folder's folders on the left (a pack, or a project folder in it), their kits on the right alphabetically.
  Clicking a kit loads it and leaves the browser open, to try one after another. The arrows next to the name step to
  the previous or next kit in the same folder. The editor reads the kit folder each time, so new kits show up at
  once. A `*` after the name means the kit was edited in the editor since it was loaded or saved; loading another kit
  replaces those edits without asking, as on the device. Tack tells the host which preset it loaded, and the
  project keeps the kit file's path, so the arrows carry on from there when the project is opened again.
- **Saving kits:** the editor's "Save kit" button writes the kit as it is now to the "User" pack,
  `Kits/User/<name>.SND`, where the preset browser lists it with the others. The file is what the device writes:
  a name of up to 8 characters from the device's character set, then the kit's parameters. The device finds kits by
  slot number, so on its SD card a kit needs a file name such as `05-NAME.SND`. Saving under the name of a kit
  that is already there asks before overwriting it. Hosts may only see a new kit once they rescan their presets.
- **Editor:** a tab per voice plus FX. Each tab shows the front panel's sub-pages as boxes of up to 4 x 2 controls,
  three boxes to a row (`clap/gui.cpp`): knobs for amounts (drag, Shift for fine, wheel steps, double-click resets
  to the init kit value) and drop-downs for options, with short labels and the full name as a tooltip. The FX tab
  shows the controls of the selected FX type under that type's names. Trig buttons trigger the selected voice. It
  is Dear ImGui (MIT) drawn into a pugl (ISC) OpenGL view, fetched at pinned versions, embedded in the host's X11
  window and driven by the host's timer; edits go to the audio thread through a queue and reach the host as
  parameter gestures. `build/host/gui_test Tack.clap [seconds] [kit.snd]` opens it without a DAW (also under
  Xvfb, with `__GLX_VENDOR_LIBRARY_NAME=mesa` for software OpenGL).
- **State:** the kit itself, in the firmware's representation, the outputs switch, the kit's name, its file and whether
  it was edited.
- **Pulse width:** a deliberate difference from the device: the PWM waveform subtracts two copies of a saw
  offset by the pulse width, so at width 0 it is silent. Pulse width only matters for PWM, so its minimum is 1 here,
  and kits that store 0 play with 1.
- **Latency:** reported to the host, which compensates on playback.
- **Outputs:** seven stereo outputs: Main, Drum 1, Drum 2, Drum 3, Snare, Cymbal and Hi-hat. Each voice's Output is
  Direct or FX: it plays as it is, or goes through the FX bus. The bus works as on the device, a group bus with an
  insert: the voices routed to it are summed and processed together, and the bus plays on Main. The "Outputs"
  switch, on the Mix & FX page, decides where Direct voices go: with "Stereo" (the default) to Main, so everything
  is there, with "Individual" to their own outputs, panned, leaving Main with only the voices that go through the
  FX. Both settings sum to the same mix. The switch is saved with the project, not with kits, and isn't automatable.
  With "Stereo" only Main is resampled to the host's rate, which halves Tack's CPU use (about 1.3% of one core
  here, against 2.8% with "Individual"). Switching to "Individual" while playing starts resampling the voices'
  outputs from that moment, so they can be up to half an engine sample (11 µs) off Main until the project is
  reopened or the audio engine restarts.
  The device's other destinations, its second output pair and the mono jacks, become Direct (and the FX bus always
  returns to Main), a deliberate difference; none of the factory or Hrtl kits use them, and kit files keep what
  they store.
- **Instances:** the engine keeps its state in globals, so each instance runs its own copy of it. Tack loads a
  private copy of its own binary per instance (`clap/engine.c`, from an in-memory file) and drives that copy's
  engine through a function table, `tack_engine()`, the only symbol it exports besides `clap_entry`. Everything
  else has hidden visibility, so each copy's code uses its own globals. Instances can process in parallel.
  `build/host/multi_test Tack.clap kit.snd...` renders a different kit in each of several instances, interleaved
  and on one thread each, and checks every result against the kit rendered alone. Linux only for now (`memfd`).

### Single voices: Tack Drum, Snare, Cymbal and Hi-hat

`Tack.clap` holds four more plugins, one per kind of voice, for playing a single voice on its own track or in a
host's drum rack. Each runs a whole engine but plays and shows one voice: Tack Drum is drum 1 (drums 1-3 are the
same engine), the others the snare, cymbal and hi-hat.

- **Notes:** on any channel, chromatically: C3 plays the voice at its tuned pitch. The keyboard switch next to the
  name (the "Chromatic" parameter) turns that off: every note then plays the tuned pitch, as a drum pad would.
  Tack Hi-hat plays the open hat on channel 2 and the closed hat on every other channel; a closed hit chokes an
  open one, as on the device. Open and closed differ only in their decay ("Decay Open", "Decay Closed").
- **Presets:** the voices of the kits in the kit folder, in the plugin's own browser (click the name; the arrows
  step through a folder): every kit's drums 1-3 in Tack Drum ("Lambo Drum 2"), its snare, cymbal or hi-hat in the
  others, leaving out voices that are just the init kit's. A voice is copied onto the plugin's voice with its
  modulation: targets and LFO retrigger on its own voice move with it. What depended on the rest of its kit doesn't
  come along (another voice's LFO on it, its LFO on another voice, an LFO retriggered by another voice), so a few
  voices sound different on their own. The host's preset browser only lists the init kit for the voice plugins: a
  kit file that offers several presets is a preset container, and hosts didn't list those (Bitwig 6.1.6 showed no
  Tack presets at all).
- **Saving:** the editor's "Save" button writes the voice as a kit with only this voice, to the "User" pack like
  Tack's kits: the voice in its own place (Tack Drum's on drum 1), every other voice the init kit's. The voice
  browser lists it under its own name ("Clap", not "Clap Drum 1"), Tack and the device load it as a kit.
- **Parameters:** the voice's own, named without the voice ("Filter Frequency", ids as in Tack). Velocity and LFO
  destinations offer only the voice's own parameters. No FX bus: the voice plays directly on the plugin's one
  output.
- **Editor:** the voice's page of the Tack editor, without its output menu; no play button, notes play it.
- **CPU:** the engine computes only the plugin's voice, without the FX bus, and resamples only its two channels:
  about 0.7% of one core here, against 1.3% for a whole Tack. The noise in snare, cymbal and hi-hat sounds
  differs sample by sample from the same voice in Tack (other voices no longer draw from the random number
  generator in between), not in level or colour.
- Velocity and LFO destinations of the other voices are off, since those voices aren't computed.

To install it into `~/.clap`: `cmake --build build/host --target install_tack`. It copies the plugin in one step
rather than linking to the build output, so a host watching the folder never loads a half-written file mid-build.

`build/host/clap_render Tack.clap out.wav [rate] [-s] [-p kit.snd] [[channel:]note | id=value | id~amount [@seconds] ...]`
is a minimal CLAP host for testing without a DAW: it loads a kit through the preset-load extension, plays notes (on
channel 1 unless given, e.g. `2:48` for drum 1 an octave down), sets parameters (`=`) or modulates them (`~`)
through flush or as timed events, and with `-s` runs the state through save, a new instance and load before playing.
`gui_test` takes modulation from `$TACK_MOD` (`"37~-60 8~30"`) to show it in the editor. `clap-validator list presets
build/host/Tack.clap` shows what the preset browser will see.

`clap-validator validate build/host/Tack.clap`: 192 tests for the five plugins, 162 passed, 0 failed, 30 skipped. The skipped tests are for
extensions Tack doesn't have (64-bit audio, configurable ports).

Not done yet: macOS and Windows, a styling pass on the editor.

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
