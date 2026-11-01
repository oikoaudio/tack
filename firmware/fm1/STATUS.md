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
| | Each of these stops first if another program holds the FM-1's MIDI port, and names it. `FM1_FORCE=1` goes ahead anyway |
| `fm1 convert OLD.tack NEW.tack` | one-off: a backup from before the list of locks, for this build. Delete `tools/ktl_convert.py` when done |

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

## The site

`/tack/` introduces both forms, `/tack/plugin/` is the plugins, and `/tack/fm1/` is this firmware, with `install/`,
`backup/` and `firmware/` under it and the manual at `/manuals/tack-fm1/`. They are on local branch `feat/tack` of
oikoaudio's site, in worktree `~/ws/dev/web/web-oikoaudio-tack`, uncommitted. A push to that repository's `main` deploys, so keep this off `main` until
the list below is done. `tools/make_web.py VERSION WEB_DIR` writes the installer and backup pages there from a release
package. It uses fm1-base's page logic, leaves out m-bootloader, and needs no Emscripten. `site/install_app.js` warns a
unit running V16 before anything is written. `firmware/fm1/.gitignore` ignores `web/`, so own files go in `site/`.

Headless Chromium against a fake FM-1 in the page, no MIDI port opened: the page loads the package, asks for a backup,
warns on V16, cancels without an update request, and names V15 and V16 and refuses each with a bit flipped in flash.bin or
the loader. Compiled and simulator-tested only: none of it has run against a unit.

Going back to stock has not been run on a unit. fm1-base's `checkUfw` refused every real M-VAVE package as damaged, as
parts 50, 52, 161 and 251 use another checksum and its tests only use fm1-base packages, so `make_web.py` checks stock on
flash.bin and the loader only. Move that into `web/fm1pkg.js` on the fork. `tests/ota_test.c` staged V15 and V16 through
`chip/ota.c` and refused both with a damaged boot area, so the step before M-VAVE's writer takes over is tested off the unit.
Tack's saves leave the stock radio data at 0xE7000 to 0xEA000 alone. V16's image runs to 0xA9000 over nine pattern slots,
which read as empty after that because the store checks CRCs.

V15 and V16's boot areas differ in 12,655 of 16,384 bytes. M-VAVE's download is now V16. V15 is on the Internet Archive,
SHA-256 `db1642b2...7edb8a`, as in `fm1_rescue.sh`. Tack replaces the whole app, so stock's V16 features do not carry
over. What matters is whether Tack boots on V16's bootloader, which has not been tried.

Before publishing:

1. Go from Tack to V15 and back on the unit, with the local installer, then take `STOCK_NOTE` out of `make_web.py`.
2. Decide how to meet a V16 unit: test it, or keep the warning.
3. Make the Tack repository public, or publish its source for that version: the GPL needs it, and the page links it.
4. Fill the kit bank from the browser. Today it takes a terminal and this repository, and no kits ship. Link to kits, don't
   host them: none of the three kit archives here has a licence file.
5. The hour's soak, and a release build from the commit that gets tagged.
6. Fill in the manual again after the panel changes below.

## Features

The panel:

| Control | Action |
| --- | --- |
| White keys | the 16 steps of the page, on release |
| A step held, K1 to K4 | lock those values on the step, eight per step, on every page but the bars view. EDIT clears them |
| SEL held, a white key | that step becomes the lane's last |
| Black keys, first seven | select a voice and play it, with REC running recording it; with SEQ held, select it silently; with ARP held, mute it. Lit: the voice. Mid: steps in this pattern. Glow: an empty lane. Dark: muted. Each flashes as its voice fires, the lit one to dark |
| NOTE mode | the bars view's grid, top bar and selected step, with each step showing its note by name and height, three octaves to a box. All 27 keys play the voice from F3 to G5, and OCT moves them up to 3 octaves. REC on writes what they play: stopped, step by step from the selected step; running, live. REC off only plays. SEL and SELECT select a step, ALGORITHM nudges it. K1 its note, K2 its velocity, K3 how hard the keys write, K4 on or off. The keys light the step's note. SEQ held with a black key picks the voice |
| MIDI channel 1 | notes 60 to 66 play the voices. Notes 0 to 15 and Program Change 0 to 15 pick a pattern, at the end of the bar when running |
| Recurrence, running | a step that rests on this pass is grey on the screen and its key is at the lowest light. In the one voice view a tall mark shows the slot this pass is on |
| LFO WAVE RND with RETRIG on a voice | that voice's hit takes a new value, there before the note starts and held until the next hit. FM-1 only: the LXR and desktop Tack keep the value until the LFO's own cycle wraps |
| A step held | a popup until it is let go. ALGORITHM: nudge, on a ruler. PRESET: chance. SELECT: the recurrence cycle, 1 (every pass) to 8, and the first eight black keys pick the passes it plays on |
| ARP, third screen | VEL shows the bars view's bars, empty steps too. K1 picks one of 12 rhythm shapes, K2 and K3 set the centre and amount, K4 moves it later. It writes velocities into every step of the selected lane, so the steps hold them and nothing about the shape is saved: these are a tool's dials, reset at power-on. A hand edit afterwards is an ordinary edit, until the next turn writes over it. K1 at OFF puts back the velocities from before the first shape, once, on the last lane and pattern shaped. Shapes come from aww's tables, written out by `tools/gen_shapes.py`. Bembe is from the Groove MIDI Dataset, CC BY 4.0 |
| ARP | tap: the selected lane's bars, with three screens of dials in turn, and the white keys stay steps. LANE, how it plays: K1 length, up to 64 and no step changes, K2 direction, K3 speed, K4 the whole lane's nudge. Forward, reverse, ping-pong or random: a direction changes the order within a pass, not where a pass starts or ends. Ping-pong turns at each pass's end, so each end plays twice. Random plays every step once a pass in a new order. Speed is 1X, 1/2 or 1/4: each step holds for 1, 2 or 4 steps' time, its own hit and nudges staying as long as they are. A pattern's length is its longest lane in steps at 1X, whatever the speed: a slow lane runs on across the repeats of its pattern, and a pattern change or song row cuts it off. Direction and speed are saved per lane and pattern, and a pattern from an older build plays forward at 1X |
| ARP, second screen | EUCL: K1 to K3 are Euclidean length, hits and rotation, and replace the lane's steps. K4 puts back the steps and length from before the first turn, once, on the last lane and pattern |
| SEQ held, ARP | SONG page, up to 63 rows of one pattern each. White keys fill the rows. K1 row, K2 pattern or end, K3 stop or loop at the end, K4 on or off. COPY tapped while a chain plays writes the chain over the rows from this one and moves past it, keeping the song's end and any rows after it. Picking one pattern turns the song off and keeps its rows |
| OCT- and OCT+ | bar 1 to 4 |
| PLAY, REC | start and stop, live recording |
| Screen | the 7 row grid on the main page. EDIT, ENV, LFO, FX, GLO and SONG have the screen below the header, as the keys show the voice's steps |
| K1 to K4 | what the open page shows: pitch, filter, decay and volume of the voice on the main page |
| GLO, OCT- held | USB AUDIO: STEREO, the master, or 10 CH: 1-2 the master, 3-8 D1 D2 D3 SN CY HH, 9-10 the FX bus, dry and wet, of the voices sent to it. Each voice as it leaves the voice, so a voice sent to the FX is dry on its channel. 44.1 kHz only. Changing it needs the sequencer stopped and re-enumerates, so USB-MIDI goes for a moment. It is never saved, so every power-on is stereo, and with no host after 8 s it goes back to stereo. K2 is RATE: every voice's sample rate, before the FX bus, so delay and distortion stay clean, 127 for no change. A kit saves it, and it is the one sound parameter the plugin has that no voice page does. K3 is BITS, 16 to 2: the final mix's depth on a continuous dial, after the FX and before MASTER, saved with the pattern. It reaches the mix on channels 1-2 and not the voices' own channels. OCT+ held in 10 CH: DROPS, the lost packets, a click each, and the rest of what USB audio has been doing |
| SELECT, ALGORITHM, PRESET | tempo and shuffle, on a screen of their own for a moment; kit |
| SEQ | tap: the last detail view, one voice's steps or NOTE, from the overview or any page; tapped in a detail, the other one. NOTE carries on over the sound pages, so the keys play the voice while the dials tune it, and on the SONG, FX and GLO pages the keys are that page's own. HOME ends NOTE, so its black keys always pick a voice, and SEQ goes back to NOTE if that was the detail left. A key held through HOME still sends its note-off. Held with a white key: pattern 1 to 16. Held with several, a chain that loops until one pattern is picked. SAVE leaves a chain out |
| Page buttons | EDIT ENV LFO FX GLO ARP: opens the page. Pressed again, goes to its next screen if it has one, as EDIT does, and otherwise does nothing. HOME, SEQ or another page's button leaves |
| EDIT, ENV, LFO | the Syntakt layout: a top bar and all three rows in four equal columns, with smooth widgets and one picture across the parameters that shape it: the envelope, the filter's shape, the oscillator's wave, the LFO. LEVEL is the fourth dial of AMP EG, and REPEAT the fourth of ENV's last row. The exact value shows in the top bar while a dial turns, and a held step's recurrence there too. Velocity and LFO targets skip velocity's own settings |
| One voice's view (SEQ), the bars view | CHN or SONG by the voice's name in the top bar, here and in NOTE, while a chain or the song picks the pattern. One voice's bar at a larger size, in the white keys' order. Each step shows velocity as height, nudge on a ruler under it, its number of locks as L2, recurrence as boxes, and chance and flam as text. A key pressed, a tap of SEL or a turn of SELECT selects a step, shown by an outline and S3 in the top bar, the same step NOTE has selected. ALGORITHM nudges it, so tempo and shuffle are on the other pages. K1 to K4 are that step's velocity, nudge, flam and chance, with or without the key held, so they never lock here and the view does not tune the sound. HOME tunes and locks. A held step's ALGORITHM, PRESET and SELECT popups work here too |
| OCT+, OCT- on the main page | the bar, among those the lanes reach |
| SEL held, OCT+ or OCT- | a bar more or less on the selected lane, 16 steps at a time up to 64, on the main page and in NOTE. Steps past the end stay, and come back when the lane grows |
| OCT+, OCT- on a sound page | held: the upper or lower row takes the screen and the dials, and the middle one comes back when let go. Elsewhere OCT is the bar, or in NOTE mode the keys' range |
| FX | the sends, lit by the black keys, then FX type, mix and two settings of that type. OCT- held reaches its others |
| GLO | settings: tempo, shuffle, external MIDI clock, roll rate, and the `CPU`, `PEAK` and `XRUN` readings |
| Black key 8 | ROLL while held. The top bar says ROLL |
| Black key 11 | COPY held with a white key: this pattern onto that one. The top bar says COPY TO |
| Black keys 9 and 10 | nothing |
| SEL and EDIT held a second | clear the lane |
| SAVE | every changed pattern, tempo and the startup kit, refused while running because a flash write stops the sound |
| HOME | the overview, every voice, from anywhere. Each step's height is its velocity, and the first of each four is lighter. Under the kit and voice names: the transport, REC, the selected lane's length and direction, its speed if it is not 1X, and a mark per bar when there is more than one. Beside the envelope: the 16 patterns in the white keys' order, the one playing lit, those with steps grey, the one queued outlined, then what comes next: LOOP, NEXT, or the song's or chain's row of how many. Under that SHUF, and FX lit if the voice is sent there. HOME is lit, as SEQ is in a detail view. GLO held: zero `PEAK` and `XRUN`. SEL and HOME held a second: the kit as it is in the bank, dropping every edit |
| GLO held, PLAY | load test: every voice every 35 blocks, about 51 ms, on the loaded kit. PLAY alone ends it |

Compiled and simulator-tested:

- All seven voices, parameters and the kit format, by the audio comparison. The FX types ran in the CPU estimate and in
  factory kits that use them, without a comparison of each type on its own.
- Keys and USB MIDI note-on through the app, with Tack's channel map.
- The sequencer in the trace harness: 7 tracks, 64 steps, per-track length, shuffle, shifts, flams, rolls, probability,
  two automation slots, live recording, pattern changes and songs.
- A lane's direction and speed in `tests/lanes.txt`: each direction's order, hits a pass, half and quarter speed, a nudge and a recurrence across a reversed pass's edge, a slow lane running on across repeats while a pattern change does not wait for it, copying, saving through a reboot, live recording, and writing a chain into the song. `tests/lock_test.c` loads a pattern in the older format.
- Chains and the song in `tests/song.txt`: building and looping a chain, one pattern ending it, filling, cutting and
  growing the rows, playing to the end and stopping, starting again from the top, saving the song through a reboot, and
  a backup and restore that bring it back. A copy of the unit's own older backup restored here with its kit, pattern and
  tempo, and an empty song.
- NOTE mode and a step's note in `tests/note.txt`: playing without writing, step entry, OCT, the knobs, live recording,
  NOTE's K2, and saving through a reboot. The rendered audio measures an octave at 2.05 and a fifth at 1.52 on a
  drum with a pitch envelope.
- The panel in `tests/sequencer.txt`: steps, voices, mutes, bars, tempo, shuffle, patterns, transport, and saving one
  pattern and the kit, then loading them at boot. The recorded audio has onsets 498.8 to 500.9 ms apart at 120 BPM.

Compiled, not tested on their own:

- Hat choke. It is the LXR's own `HiHat_trigger` code, and no scenario checks it.
- TRS MIDI, which comes from the platform.
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

Backup on the unit: at 4231e82 all 86 kits read back byte for byte equal to the uploaded files. About one kit read in
3,000 went unanswered, 4 of about 12,000 requests, and each was answered on the second try. The device drops a SysEx that
starts before it releases the last one, so the tool repeats a request and the reply names its part. At 6a16e28 the user's
full sequence worked: save a pattern and kit, back up, change the pattern, restore, power cycle.

Locks: a step holds 8 and a pattern 640, in a sorted list per pattern. The sequencer's two slots in a step stay in RAM, unused,
which costs about 30 KB of the pool, so the one change to `sequencer.c` is a hook. The store holds the compact form: 1,863 B
plus 3 B a lock, against 3,650 B before. A pattern is refused whole, and loads empty, unless its magic, length, ranges and
list order are right. Refused patterns stay in the store until overwritten, and the screen counts them at boot. There is no
conversion in the firmware: `tools/ktl_convert.py` is a one-off for `.tack` version 1, and `APP_BACKUP_VERSION` is now 2.

One parameter update costs 108 to 257 x86 instructions, median 118, for every destination up to 250 (`midiParser_ccHandler`
stores a value, and the render does the rest). A lock is two updates when its step fires, one putting the last value back.
Eight locks on all seven voices together is 112 updates, about 16,000 instructions or 6% of a block by the 2.1 cycles per
instruction above. The load test now does exactly this at each hit, so its `XRUN` on a unit is the real answer. It has not
been run on a unit.

Recurrence: a step plays on chosen slots of a cycle of 2 to 8 passes. The length and mask sit in the sequencer's unused first
lock slot, so they copy and clear with the step, and each takes 3 B of the 640 in the image. Each lane counts its passes of
each pattern, from the start of play.

The distortion's resting level comes off its output on the FM-1 (`fxBus_distortionNoRest`, FM-1 build only). The ECC83 curve reads -0.11 at no input, and with the mix and volume up the main output sat at -1120 with nothing playing. It is a constant worked out from the settings and no filter, so no phase moves, and the asymmetry of a sound as it plays stays. `tests/fxdc.txt` checks TUBE and FOLD at rest. EDIT goes between its two screens and no longer on to the main page.

Panel words: pan reads L63, C, R63 on a dial that fills from 12 o'clock. 63 and 64 both read C, as the middle is 63.5 and the default kit sits at 63. Kits still store 0-127. PITCH, FINE and the second oscillators' frequencies read as signed offsets, as on the LXR's menu: `tools/gen_pages.c` asks the desktop's value text what reads as 0, 60 for tuning and 63 for FINE, and dials and bars fill from there. `tools/gen_pages.c` names the LXR's REC and PWM waves SQR and PULSE. The second oscillator's labels follow MODE: FM AMT, FM FREQ, FM WAVE, or MIX, FREQ 2, WAVE 2, as AMOUNT crossfades the two in MIX. GLO is bright for its page alone, and external clock shows as EXT by the tempo. STEP or NOTE is in bold on the main page and in the top bar of the bars view and NOTE. On D1 D2 D3 and SN the middle row of EDIT's first screen starts with PITCH, the main page's, and its lower row is FINE and the MOD EG's amount, decay and slope, which are ENV's. On CY and the hats that row is FINE, WAVE, PULSE and PITCH. OUTPUT's K4 on every voice is FX SEND, the FX page's send, which takes no lock.

External clock's steps come from a flywheel in `tack_engine.c`, simulator-tested and not yet on the unit. After 12 pulses the sequencer takes its pulses from a clock counted in audio frames, as the internal clock is, and each pulse from outside corrects it by a sixteenth of how far out it was, or a quarter when it is more than half a pulse out. `tests/grid.py` measures it from the audio, at 8 bars of closed hats: the gaps between hits varied by 8.7 ms on steady pulses and 14.5 ms with 3 ms of jitter, and now by 2.9 ms on both. That is the 1.45 ms block at each end of a gap. Before the 12 pulses, including the first hit after a Start with no clock running, steps still follow the pulses, and a pause of 3 pulses' time or more starts the count again. A Stop exactly on a step can sound that step for a moment before the sound stops, as the flywheel has already played it, and the position stays right. The tests cover a tempo jump, a ramp, 60 ms and 400 ms without pulses, Stop and Continue, Start on a running clock, and shuffle. External MIDI clock: followed Bitwig on the unit, in a short check, and run in the simulator, where `tests/clock.txt` sends Start, Clock and Stop through `midi_in`. The panel's setting used to select the LXR's mode 1, its trigger input, so Start ran the sequencer while `seq_sync` ignored every clock. It now selects mode 2. Tack follows MIDI Beat Clock only. `midi_in` drops MIDI Time Code and Song Position Pointer, so Continue carries on from where the sequencer stopped and a DAW that starts mid-bar is not followed to its position. Step 0 falls on the first clock and step 4 on pulse 25. USB and TRS MIDI reach the queue only between renders (`usb_poll` waits outside the render), so Tack reads them once a render, and reading them on each engine block would find nothing new. A step can land up to 5.8 ms late. The tempo comes from the time across the last 96 pulses, 4 beats, which reads 125 for a true 125 where the LXR's 8 pulse average read 127. It moves only by 0.6 BPM or more, so synced LFOs are not recalculated on each pulse. A beat that disagrees by 4% restarts the span, so a jump shows within a beat and a slow ramp can lag by up to 4 beats. For the first 12 pulses after a stopped clock it holds the old tempo, and for the next few beats it can read about 2% out. The first note is on the first clock whether 0 or 12 clocks came before Start. Start and Stop also go back out on USB, as the LXR sends them.

USB audio's 10 channels: run on the unit. Linux took 10 channels, and 6 s of a running pattern captured with `arecord` gave every frame, each voice on its own channel, with `DROPS` at 0. It also streamed into Bitwig through PipeWire and a combined device with no drops, on a normal session rather than a timed run on a heavy kit. One SAVE during that stream was followed by a buzz on USB and not the speaker, with 5 `DROPS`, and PipeWire's capture of the FM-1 stuck in an XRUN loop that a reopened node and a USB reconnect did not clear and restarting Bitwig's audio engine did, as far as the timing shows. A later SAVE brought it back: PipeWire logged `follower delay:7 target:768`, as a flash erase runs with interrupts off and no USB packets go. So a store write now queues silent packets from the flash wait, in RAM (`FL_USB_PUMP`, `fl_pump_poll` in felucca-bsp's `fm1_flash.h`), while 10 CH streams. Compiled: `.ram_text` has no calls and no flash addresses, and the update loader is byte for byte the same. Not yet run on a device. The simulator has no USB, and stereo streaming still has the gap. Bitwig's links to the FM-1 go when the FM-1 re-enumerates, as it does on switching to 10 CH, so turn 10 CH on before starting Bitwig's audio engine. With every voice sent to the FX, 9-10 equal 1-2, as the FX bus carries dry and wet together, so they are the bus and not a wet return. The simulator has no USB side. The design is Acid Moogi's (github.com/TomPtherapy/moogi, GPL-3.0), whose notes record clicks and drops found on a device and what fixed them, with the ring and bands worked out again for Tack's 256 frame render. It lives on local branches `tack-usb10` of fm1-base and of its nested felucca-bsp, so the pins here point at commits that exist on this machine only, until those are pushed to forks. `chip/usb.c` is also built into the update loader: `loader.bin` was built from nothing with and without the change and is byte for byte the same. Every power-on is stereo.

Compiled and simulator-tested, not yet on a device: the three row pages and voice colours, recurrence, the list of locks, chains, the SONG page, pattern select over MIDI, the key lights, the larger pages, and NOTE mode.

Known omissions:

- 16 patterns, not 64. The full bank does not fit the pool, which has about 116 KB left.
- One song. A row plays its pattern once, so a repeat is a second row, and no row can be inserted or deleted.
- A step's note is an offset from the kit's tuning, in semitones from 60, so C4 means the kit's pitch, not concert C4.
- MIDI out is USB only, and notes arriving on USB or the jack go back out on the voice's channel. A DAW track that sends Tack its own output plays voices twice. A sequencer step's note-off comes one pulse after its note-on, about 5 ms at 120 BPM, so recorded steps are short.
- A lane nudge adds to each step's nudge and stops at the limits of 7/8 of a step, so a large turn back can lose a hit's
  own offset.
- The Euclidean dials stop at length 16, as the LXR's menu does. From length 18 with one hit the generator recurses
  without end, which would crash the audio interrupt.
- A Euclidean turn replaces the lane's steps and length, and its locks stay on their steps. K4 on that screen puts them back. The LXR has no undo here either, but an edit there stays unsaved until a pattern change or save, and Tack keeps each edit at once.
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

## Ideas, not started

From the user on 2026-11-01, after the sequencing work:

- An eighth voice: two oscillator FM on the user's wavetables, 1D but stepped through by two parameters as if 2D.
- USB audio: playing a computer through the FM-1 is `usb_audio_out`, off in `fm1.toml`, and cannot run with the 10 channels as they stand.

## Memory

Compiled, from the build's own report:

| Region | Used | Available | Share |
| --- | --- | --- | --- |
| Flash image | 283,520 B | 581,564 B | 46% |
| RAM, `.data` and `.bss` | 37,180 B | 98,304 B | 38% |
| Pool | 254,156 B | 344,064 B | 73% |

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
- fm1-base's host `data` command stores a file 0x4000 below where `plat_data` reads it (`PLAT_DATA_LO` against `DATA_BASE`). `tests/kits.txt` allows for this. Take it out if a newer pin fixes it.
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

- Commit `6a16e28` installed by the user. Saving, `fm1 backup`, `fm1 restore`, the kit reload and a power cycle worked.
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
