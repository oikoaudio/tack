# Tack on the M-VAVE FM-1: status

Branch `experiment/fm1-tack`. Each result below says how far it got:

- **Compiled:** the JieLi toolchain built it and fm1-base's image checks passed.
- **Simulator-tested:** it ran on this computer, in fm1-base's host simulator or a test program.
- **Hardware-tested:** installing, booting, sound, kit upload and kit switching, on one FM-1 on 2026-11-01.

## Recommendation: continue

The real LXR engine and sequencer run on an FM-1. The image uses 40% of the app slot, 36% of RAM and 56% of the buffer pool.
The load test on the init kit reads 42% CPU, 42% peak and 0 xruns, on one core at the boot loader's clock. CPU load was
the question that could stop the project, and that reading leaves more than half the budget free. The next work is the
panel, kit loading, a long soak run and a listening test of the open audio differences.

## Architecture

```
firmware/fm1/app.c              the panel and screen, in fm1-base's unity build
firmware/fm1/src/tack_engine  event queue, engine and sequencer calls; no hardware
firmware/fm1/src/seq_platform   the sequencer's clock, and stand-ins for its LEDs, menu, SD and trigger jacks
firmware/fm1/shim/              stand-ins for STM32 headers, libm and FatFS types
host/src/lxr_core.c             engine control, shared with desktop Tack
Core/                           the LXR engine and sequencer
third_party/fm1-base            the platform (submodule), with felucca-bsp inside it
```

The audio interrupt is the only writer of engine and sequencer state. The main loop posts events to a ring, and the
render drains it before each 64-frame block. Each 256-frame audio callback runs four of these steps: drain events, advance
the clock, `seq_tick()`, render. The LXR calls `seq_tick()` once per block in its audio interrupt, so the order matches.

### Pinned revisions

| Component | Revision | Licence |
| --- | --- | --- |
| fm1-base | `5649be98b517df053ccc5f48981b2838c8a4f9fa` | MPL-2.0 |
| felucca-bsp | `cd76d43ab4b94d6da9a8f1f24f3f4446135196b7` | MPL-2.0 |
| JieLi toolchain | `jieli-linux-toolchains-20260730.1`, clang 4.0.1 | JieLi's |
| AC79 SDK, three files in the package | `d179b4484759423312073f5fbb232501aa491047` | Apache-2.0 |

fm1-base is the platform, because it already has audio, MIDI, the panel, storage, the update path and the simulator.
felucca-bsp comes with it. Felucca itself (`406b328`, GPL-3.0-only) was a reference for panel conventions only, and no
code was copied from it.

`NOTICE.md` restricts the names Erica Synths, LXR and LXR-02 and requires modified firmware to say so on its start-up
screen. The splash reads TACK, DRUM SYNTH, and the docs use the names only to state where the code comes from.

The firmware is GPL-3.0-only. MPL-2.0 files can be part of it, and it links no JieLi radio library. A package must ship
`LICENSES/Apache-2.0.txt` for the three SDK files.

JieLi's SDK repository no longer exists at the URL in `get_sdk.sh`. The same revision is at the mirror
`amitv87/fw-AC79_AIoT_SDK`, and the script's SHA-256 checks passed for all five pinned files.

## Using a connected FM-1

`firmware/fm1/tools/fm1` runs the device tools from any directory. Its first run makes `firmware/fm1/.venv` from
`tools/requirements.txt`. Close anything holding the FM-1's MIDI port first.

| Command | What it does |
| --- | --- |
| `fm1 info` | shows what is on the unit |
| `fm1 install` | installs `build/tack.fwsc` |
| `fm1 log` | reads the crash log |
| `fm1 kits DIR` | replaces the kit bank with every `.SND` under `DIR`, sequencer stopped |
| `fm1 backup NAME` | writes patterns and settings to `NAME.tack`, and the kit bank to `NAME.kits/` |
| `fm1 restore NAME` | puts both back. The FM-1 asks first: SEL yes, HOME no |

`tests/verify.sh` checks the desktop renders against `tests/desktop_renders.sha256`, then builds the firmware and simulator.

## Build and simulator

```sh
git submodule update --init --recursive
sh third_party/fm1-base/tools/get_toolchain.sh
SDK_URL=https://github.com/amitv87/fw-AC79_AIoT_SDK sh third_party/fm1-base/bsp/tools/get_sdk.sh
export JIELI_TOOLCHAIN=~/.jieli/toolchain AC79_SDK=~/.jieli/ac79-sdk

# from the repository root
python3 third_party/fm1-base/tools/build.py firmware/fm1                # firmware/fm1/build/tack.fwsc
CC=clang python3 third_party/fm1-base/tools/build.py firmware/fm1 --host

firmware/fm1/build/host/tack_host firmware/fm1/tests/sequencer.txt OUTDIR
firmware/fm1/build/host/tack_host firmware/fm1/tests/keys.txt OUTDIR
firmware/fm1/tests/compare_seq.sh OUTDIR
firmware/fm1/tests/compare_audio.sh OUTDIR kit.snd ...
firmware/fm1/tests/compare_kits.sh OUTDIR KITDIR                        # after compare_audio.sh
```

The browser emulator is not built. `--web` needs Emscripten, and installing it here was refused by the session's
permission rules. The package and the host simulator are built.

## Features

The panel:

| Control | Action |
| --- | --- |
| White keys | STEP mode: the 16 steps of the page, on release. PLAY mode: the first seven play the voices |
| A step held, K1 to K4 | lock those values on the step, two per step. EDIT clears them |
| SEL held, a white key | that step becomes the lane's last |
| Black keys, first seven | select a voice; with ARP held, mute it |
| SEQ | switch between STEP and PLAY mode |
| OCT- and OCT+ | bar 1 to 4 |
| PLAY, REC | start and stop, live recording |
| K1 to K4 | what the open page shows: pitch, filter, decay and volume of the voice on the main page |
| SELECT, ALGORITHM, PRESET | tempo, shuffle, kit |
| SEQ | tap: STEP or PLAY mode. Held with a white key: pattern 1 to 16 |
| EDIT, ENV, LFO | the voice's sound pages, 256 parameter slots in all. Each press opens the next page, and past the last returns to the main page |
| FX | FX type, mix and two settings of that type, then a second page of its others. Black keys send voices to the FX |
| GLO | settings: tempo, shuffle, external MIDI clock, roll rate, and the `CPU`, `PEAK` and `XRUN` readings |
| Black keys 8 to 10 | ROLL while held, the Euclidean page, COPY held with a white key: this pattern onto that one |
| Black key 11 | STEP page. A step held: nudge, flam, chance, velocity. None held: K1 nudges the lane |
| SEL and EDIT held a second | clear the lane |
| SAVE | every changed pattern, tempo and the startup kit, refused while running because a flash write stops the sound |
| HOME | the main page. GLO held: zero `PEAK` and `XRUN` |
| GLO held, PLAY | load test: every voice every 35 blocks, about 51 ms, on the loaded kit. PLAY alone ends it |

Compiled and simulator-tested:

- All seven voices, parameters and the kit format, by the audio comparison. The FX types ran in the CPU estimate and in
  factory kits that use them, without a comparison of each type on its own.
- Keys and USB MIDI note-on through the app, with Tack's channel map.
- The sequencer in the trace harness: 7 tracks, 64 steps, per-track length, shuffle, shifts, flams, rolls, probability,
  two automation slots, live recording, pattern changes and songs.
- The panel in `tests/sequencer.txt`: steps, voices, mutes, bars, tempo, shuffle, patterns, transport, and saving one
  pattern and the kit, then loading them at boot. The recorded audio has onsets 498.8 to 500.9 ms apart at 120 BPM.

Compiled, not tested on their own:

- Hat choke. It is the LXR's own `HiHat_trigger` code, and no scenario checks it.
- TRS MIDI, which comes from the platform.
- External clock through the app. The traces call the sequencer's clock functions directly, so `tack_midi_realtime`
  has not run. The panel test only switches the setting on.
- MIDI out from the sequencer, and recording from the keys through the event queue.

Simulator-tested in this round, not yet on a device:

- The screen shows all seven lanes at once, with their lengths, playheads, mutes and lock marks.
- Lane length per voice, for polyrhythms.
- Parameter locks through the sequencer's two automation slots per step. `tests/lock_test.sh` runs them through the real
  engine: a lock changes its own voice's filter on its step, holds until the lane's next step, and gives back the kit's
  value, including a value edited after the lock was made. With locks addressed by parameter number, not CC number, the
  test fails, so it catches that mistake. A lock in place when the sequencer stops stays in the engine until the lane's
  next step, as on the LXR.

Compiled and simulator-tested, not yet on a device: the FX page, the sound pages, the settings page, patterns on SEQ, and
the xrun tag. `lock_test.sh` drives each through the real engine: a sent voice gets a delay tail and an unsent one none, mix
0 removes the tail, each FX setting changes the output, a lock reaches a parameter above 127, and all 256 page slots step,
clamp and render at both ends.

Backup on the unit, build 4231e82: `fm1 backup` ran, and all 86 kits read back byte for byte equal to the uploaded files.
The `.tack` held no patterns or settings, as nothing had been saved and the flush left the settings out. About one kit
read in 3,000 went unanswered, which stopped a run: 4 of about 12,000 requests, all answered on the second try. The
device drops a SysEx that starts before it releases the last one, so the tool now repeats a request and the reply names
its part. The build after 4231e82 saves the settings in the flush and has not been installed.

Not tested on anything: `fm1 restore`, and a backup holding patterns. fm1-base's own test covers the backup code, and the
simulator cannot run it for Tack.

Known omissions:

- 16 patterns, not 64. The full bank needs 233,536 B and the pool has about 150 KB left. Each pattern is 3,649 B.
- Song playback works, but the panel has no song page.
- The STEP page edits nudge, flam, probability and velocity. It has no note, which the sequencer stores per step.
- A lane nudge adds to each step's nudge and stops at the limits of 7/8 of a step, so a large turn back can lose a hit's
  own offset.
- The Euclidean page stops at length 16, as the LXR's menu does. From length 18 with one hit the generator recurses
  without end, which would crash the audio interrupt.
- A Euclidean turn replaces the lane's steps and length, with no undo. Its locks stay on their steps.
- No kits in the firmware. `README.md` says none come with Tack, because they belong to their authors, and `NOTICE.md`
  does not cover them. `tools/fm1_kits.py` sends your own files to 128 slots in flash. Loading a kit runs in the audio
  interrupt, as the engine has one writer. It costs about 57,000 x86 instructions, roughly 23% of a block when warm.
- The delay's time is scaled by its range twice (`calcDelayTime`), so at the init kit's range of 27 the longest delay is
  40 ms, which is a comb or flanger. Range is on the second FX page, and the FX pages show the delay in milliseconds, matching
  rendered echoes at 299, 383 and 531 ms. Delay at maximum time and maximum range runs past the buffer and goes silent,
  so the page stops time at 126.
- The sound pages cover the LXR's voice parameters with sound-design use. Edits apply until a kit loads, and SAVE keeps them as
  the boot kit. Nothing writes an edited kit back to the bank.
- A pattern remembers its bank kit, which loads through the LXR's own hook before the pattern's first step. It remembers
  the slot, not edits made to the kit, and a pattern with no kit leaves the playing kit and its edits alone.
- Reverb. The LXR firmware compiles it out (`USE_REVERB 0`), and desktop Tack does the same. Shelved.
- No Bluetooth, no second core, no velocity from the keys.

The brief's first stage asked for one voice. The mixer computes all voices in one function and the whole engine compiled
as easily as one voice, so the build went straight to the complete kit.

## Memory

Compiled, from the build's own report:

| Region | Used | Available | Share |
| --- | --- | --- | --- |
| Flash image | 260,672 B | 581,564 B | 45% |
| RAM, `.data` and `.bss` | 36,720 B | 98,304 B | 37% |
| Pool | 196,884 B | 344,064 B | 57% |

The largest items are the screen canvas (69,120 B), the FX buffer (65,536 B), 16 patterns (58,384 B), the wavetables
(67,584 B plus the 8,194 B sine) and the transients (26,460 B). All engine code runs from flash through the XIP cache.

The static stack depth of the render path is about 1.3 KB against a 7.9 KB stack. That analysis follows direct calls only.

## CPU

Hardware-tested on the init kit: the load test (HOME) reads `CPU 42`, `PEAK 42`, `XRUN 0`. All engine code runs from flash,
on one core. The reading is whole percents, and the platform smooths it over about 16 audio blocks, so a spike shorter
than about 90 ms would not show in `PEAK`. A late block would show in `XRUN`.

Not yet measured on hardware: other kits, FX, or the load test with the sequencer running. `plat_cpu_pct` in the simulator
times this computer, so the `PEAK` it shows there means nothing.

The estimate below predicted this. 103,287 instructions per block reading 42% is about 2.1 cycles per x86 instruction.
Scaled by that one reading, delay on the init kit would be about 48% and BADON about 54%. Those two are still estimates.

The estimate counts user-space x86-64 instructions per engine block, with the FM-1's arithmetic and vectorisation off. The
budget is 522,449 cycles per block at 360 MHz.

| Case | Instructions per block | At 1 per cycle | At 2 per cycle |
| --- | --- | --- | --- |
| Init kit, no FX | 103,287 | 19.8% | 39.5% |
| Init kit, all voices through delay | 118,388 | 22.7% | 45.3% |
| BADON, no FX | 131,538 | 25.2% | 50.4% |

The load barely changes between idle and busy, because the engine computes every voice all the time. A hit should not
cause a spike. The figures leave out XIP cache misses, the panel, USB, and the difference between x86 and pi32v2 code.
Moving hot code to the 32 KB `.ram_text` region and skipping silent voices are the first steps if the device reads high.

## Platform findings

- The FPU is real and single precision. The compiler emits hardware multiply, divide and `ftoi`, and native half-float
  conversions. Doubles and `sqrtf` become calls. No fixed-point rewrite is needed, and desktop float code does not run
  unchanged either.
- The target has no libm. `shim/math.h` maps the engine onto fm1-base's `fastmath.h`.
- `Oscillator.c` used inline 64-bit multiplies beside 16-bit loads, the pattern `bsp/docs/CPU.md` reports corrupting
  registers on this chip. On the FM-1 `OSC_LERP` stays in 32 bits. The build has no 64-bit multiply now.
- FM phase converts a negative float to unsigned. That is undefined in C: x86-64 wraps and a Cortex-M7 clamps. On the
  FM-1 `OSC_PHASE` and `OSC_PWM` use a signed conversion and wrapping arithmetic, which gives the desktop's result.
- `systick_ticks` counts 0.25 ms, not 1 ms. The adapter derives it from audio frames in integers.

Fixes in the LXR sequencer, which desktop Tack does not compile:

- A stored track length of 0 means 16, but three places divided by it. `seq_trackLength` replaces them.
- Start and the first clock in one block gave an infinite tempo. `sync_calcBpm` now returns early.
- Pattern numbers are bounded by `NUM_PATTERN`.

Two rules for any UI on this sequencer:

- A pattern change restores the outgoing pattern from `patternBackupBuffer`, so an edit not committed there is lost. The
  buffer must also be filled at boot.
- `seq_addPatternToSong` writes at `menu_songMenuPosition`, and playback leaves that value at 255. Set it first.

## Desktop Tack

Nine reference renders are byte-identical before and after these changes, and `smoke_test.sh` passes. Shared changes:
`lxr_core.c` split from `lxr_host.c`, macros in `Oscillator.c` that expand to the original code, and overridable
`REAL_FS` and `NUM_PATTERN`.

## Sample rate

The FM-1 runs at 44,100 Hz and the engine expects 44,140, a difference of 0.09%. The options:

| Option | Result |
| --- | --- |
| Run unchanged at 44,100 | every pitch 1.57 cents flat, measured; rejected |
| Resample 44,140 to 44,100 | extra CPU and latency, and treble loss, for a 0.09% correction; rejected |
| Set `REAL_FS` to 44,100 | chosen: everything derived from `REAL_FS` follows |

One literal stays at 44140, at `Distortion.c:399`, an error of 0.09% in one filter coefficient.

## Audio comparison

Simulator-tested, with 601 sounding voices from 86 distinct factory kits, each voice from the first block. The yardstick
is desktop against desktop with another noise seed. The LXR's noise comes from a hardware RNG and never repeats, so the
yardstick shows how far two renders of one sound can differ.

| Metric, p95 | Yardstick | FM-1 arithmetic, 44,140 | `REAL_FS` 44,100 |
| --- | --- | --- | --- |
| Strongest partial, drums 1 to 3 | 25.89 cents | 0.01 cents | 16.03 cents |
| Decay to -40 dB | 19.94 ms | 0.00 ms | 10.69 ms |
| Worst band, 20 Hz to 16 kHz | 3.35 dB | 0.14 dB | 3.57 dB |
| Band 16 to 20 kHz | 1.05 dB | 0.20 dB | 2.71 dB |
| Transient, first 5 ms | 1.85 dB | 0.00 dB | 0.42 dB |

The FM-1's arithmetic nulls against desktop at a median of -83.7 dB, with a typical largest sample difference of 2.5 of
32,768. Control renders at the unadapted rate read exactly -1.57 cents, which validates the measurement. The partial
metric picks the loudest partial, so on noisy and FM voices it can report large jumps with no change in tuning. Its worst
cases are about 2,000 cents in all but the arithmetic column.

Open differences, which need a listening test:

- The 16 to 20 kHz band is the one metric where the rate change exceeds the yardstick (p99 7.50 dB against 3.41). The
  cause is not established. Aliasing is one candidate.
- 5 of 601 voices null worse than -40 dB, among them kit 81's snare and three voices of NAPALM. A bisect traced NAPALM to
  `double` and literal precision. ACID's snare diverges from any change, and a noise reseed moves it as far. The cause of
  the difference in those kits is unresolved. Higher precision in the affected state would be the fix if it is audible.

## Sequencer comparison

Simulator-tested, by `compare_seq.sh`. It replaces the engine at link level, seeds the RNG and compares three builds:
the original configuration (O), the FM-1's arithmetic and bank at 44,140 (F), and the FM-1's at 44,100 (G).

| Scenario | Events | O and F | O and G events | G timing, ms |
| --- | --- | --- | --- | --- |
| Internal tempo | 43 | identical | identical | 1.25 |
| External clock and transport | 23 | identical | identical | 3.00 |
| Track lengths | 256 | identical | identical | 1.25 |
| Shuffle, mid and full | 64 | identical | identical | 1.25 |
| Shifts | 16 | identical | identical | 2.00 |
| Flams | 30 | identical | identical | 1.25 |
| Rolls | 8 | identical | identical | 1.25 |
| Probability, seeded | 63 | identical | identical | 1.25 |
| Automation | 96 | identical | identical | 1.25 |
| Live recording | 12 | identical | identical | 1.50 |
| Pattern change | 10 | identical | identical | 2.00 |
| Song | 38 | identical | identical | 1.75 |
| Zero-length track | 5 | identical | identical | 0.75 |

F's timing matches O to 0.00 ms in every scenario. One engine block is 1.45 ms. External clock reads higher because the
pulse and the step are each quantised to a block.

The same script runs 17 absolute checks, because two builds can share a bug. They include 125.00 ms per 16th at 120 BPM,
automation arriving before its note on the same tick, exactly 30 notes from the flam pattern, and a song playing 0, 1, 2,
1 and stopping.

The original configuration here is the LXR's sequencer source with these fixes and this adapter. It is not a capture
from an LXR. A trace from a real LXR would be a stronger reference.

## Hardware validation

Each step needs a go-ahead. The test unit ran MONO2K (`FM-1_980`) before this.

Done on 2026-11-01:

- `tools/fm1_install.py` installed commit `4c2bd9d`'s package over MONO2K in about 55 s. The unit passed through
  `Felucca Update` and came back as `TACK FM-1`, identity `FM-1_4700`.
- It stayed up past the 30 s boot guard with no USB reset, and `fm1_remote.py log` reports no crash.
- The user reports that it works on the device.

- It makes sound, and the load test reads `CPU 42`, `PEAK 42`, `XRUN 0` on the init kit (user's reading of the screen).

- Commit `36b4c89` installed by the user. `fm1_kits.py` uploaded 86 kits and the unit answered for each. The kits load.
- The user read `CPU 48`, `PEAK 50`, `XRUN 3` after trying kits, and saw no xruns while switching kits fast. The three are
  unexplained. The next build tags the count with what the panel did last.

Steps 1 and 2 are done, except a check of each control. Step 3 is done for the init kit only.

The way back: M-VAVE's FM1 Workbench (open.m-vave.com/fm1-workbench) offers a bundled V15 restore through "force flash",
which its page warns can leave a unit unable to boot, so it is a recovery step only. V16 boot code is untested with
custom firmware, so do not install V16 first.

1. Have the unmodified V15 `FM-1.fwsc` from M-VAVE to hand before installing. MONO2K's patterns and calibration may not
   survive, so back up what matters first. The update loader writes only the app area, `[0x4000, 0x93000)`, and never
   the flash head. Recovery is the installer again, or `fm1_rescue` through the chip's ROM update mode on Mac or Linux.
2. Install `firmware/fm1/build/tack.fwsc`. Check boot, the screen, keys, encoders, LEDs and audio on the speaker.
3. Run the load test (GLO held, PLAY) on the init kit, BADON, NAPALM and a kit through delay. Read `CPU`, `PEAK` and `XRUN`.
   This is the go or stop reading. My suggested threshold: under 80% peak with no xruns is enough to continue.
4. Run an hour with the sequencer playing, for crashes from the 64-bit multiply fault and for faults on FM values.
5. Record over USB audio and repeat the audio comparison on the device's own output.
6. Check tempo against a reference clock, then external MIDI clock over USB and TRS.
7. Save, power off and on, and confirm the pattern and kit return, and measure the audio gap during a save.
8. Listen to the open differences: kit 81, NAPALM, ACID and the top octave.
