# Host build of the sound engine

Builds the LXR sound engine (`Core/Src/Soundengine`) for a desktop computer, so it can be run, tested and listened to without the hardware. Tested with gcc 15 on Linux x86-64. It also builds for Windows and macOS (see [Windows and macOS](#windows-and-macos)).

```sh
cmake -S host -B build/host -G Ninja
cmake --build build/host
build/host/lxr_render kit.wav          # every voice of the init kit in turn
build/host/lxr_render kick.wav 0       # one voice: 0-2 drums, 3 snare, 4 cymbal, 5 closed hat, 6 open hat
build/host/lxr_render -r 48000 kit48.wav
build/host/lxr_render -k 00-LAMBO.SND lambo.wav   # a kit file from the device's SD card
```

`lxr_render` loads the factory init kit, triggers voices at velocity 127 and renders through the full mixer and FX bus (`mixer_calcNextSampleBlock`). It writes a stereo WAV with both DAC pairs summed: at `REAL_FS` (44140 Hz) by default, or through the resampler at the rate given with `-r`.

Needs libsamplerate. An installed copy (with its CMake package) is used if present; otherwise CMake fetches 0.2.2.

## Sample rate

The engine always runs at `REAL_FS`. Its filter cutoffs are fractions of the sample rate, envelopes advance by fixed amounts per sample, and delay and reverb lengths are fixed sample counts, so running it at another rate would change the sound. `lxr_host_process()` resamples the output to the host rate instead (libsamplerate, `SRC_SINC_MEDIUM_QUALITY`). That converter keeps its artefacts about 97 dB down and passes 90% of the band, so at a 44.1 kHz host it rolls off above about 20 kHz. The audible aliasing comes from the engine's own oscillators and distortion, as on the device, and the resampler neither adds to it nor removes it.

Running the engine at the host's rate would need more than `REAL_FS` as a variable. Only 16 places read it. The rest of the engine assumes 44,140 Hz without saying so:

- Envelopes and LFOs step a fixed amount per sample, so their times scale with the rate.
- The transients are sample recordings, `TRANSIENT_SAMPLE_LENGTH` 2205 samples, 50 ms at 44.1 kHz.
- `FX_BUFFER` holds 32,768 samples, so at 96 kHz the longest delay would be about a third of a second, and the reverb's all-pass lengths are sample counts.
- Parameters are applied once per 64-sample block.
- `Distortion.c` has 44140 written into one filter coefficient.

Each would need converting to seconds or hertz and checking against the kits, and at higher rates the oscillators alias differently, which is part of the LXR's sound. The latency would barely change, as it comes from the engine's blocks, and the resampler costs about 1.3% of a core with Stereo outputs. So the engine keeps its rate. The FM-1 build sets `REAL_FS` to 44,100, its codec's rate: see `firmware/fm1/NOTES.md`, Sample rate.

- Events (notes and parameter changes) are queued with their exact output time (`lxr_host_queueNoteOn`, `lxr_host_queueParam`) and come out a constant `lxr_host_latency()` later: 4 engine blocks, 5.8 ms, at 32 kHz and above, a little more at lower rates where the resampler reads further ahead. The engine computes envelopes, pitch sweeps and LFOs once per 64-sample block, as on the device, so an event lands on the block nearest its time: within ±0.73 ms of the constant latency. `build/host/timing_test [rate]` checks this.
- The noise oscillators draw from the RNG every sample, whether or not a voice is playing, so the noise in a hit depends on when it was triggered. Renders of snare, cymbal and hi-hats are not sample-identical across rates or trigger times, even though they sound the same. On the device the RNG is the STM32's hardware generator, so its noise never repeats either: compare those voices with the hardware by envelope and spectrum, not sample by sample.

Add `-DLXR_HOST_SANITIZE=ON` to build with AddressSanitizer and UBSan.

## Tack (CLAP plugin)

`build/host/Tack.clap` is a drum synth plugin built on the engine. It starts with the init kit. The MIDI channel decides how notes play:

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

Transposing works as on the device, where a sequencer step or an incoming note sets the pitch: a voice moves by (note - 60) semitones, both its oscillators together, so FM timbres and the hats' and cymbal's metallic ratios keep their character, and the pitch envelope scales with the note. The snare's noise and the transient samples don't move. An oscillator stops at MIDI note 127 (12.5 kHz), so voices tuned near the top, like the init kit's cymbal and the hats' second mod oscillator, only go down. The noise waveform is sample-and-hold at the oscillator's frequency, so the note sets how bright it is. Each voice is monophonic: a new note retriggers it. Note-offs are ignored, as on the device, since every voice is one-shot.

- **Parameters:** all 227 sound parameters the front panel offers, per voice plus the FX bus, named as on the device's display with the abbreviations spelled out ("Drum 1 Filter Resonance"). `src/lxr_params.c` builds the list from the front panel's menu tables (`menuPages`, `valueNames`, `parameter_dtypes`, `modTargets`), so names, ranges and value texts follow the firmware; `build/host/list_params` prints it. Parameter ids are the firmware's parameter numbers, so they stay stable. Changes go through the same queue as notes, so automation lines up with them to within the same ±0.73 ms. The amounts (every parameter with a knob) can be modulated by the host: its modulators add to the parameter's value without moving it, as CLAP modulation does, and the editor shows the modulation as a thin ring around the knob. The sum is rounded to the parameter's steps and kept in its range, so modulation is as coarse as automation; options (waveforms, destinations, outputs) aren't modulatable. The four FX parameters mean something different per FX type and are named with all their meanings ("FX Color/Decay/Range"). LFO destinations cover every modulation target; the front panel's "destination voice" only narrowed its list and is left out. Its target list has the FX parameters once per voice, so Tack lists each target once, and names targets like the parameters (the front panel calls the FM waveform "Oscillator Waveform" there). The front panel's "Veloc EG" page is the voice's amplitude envelope and has nothing to do with velocity, which has its own parameters, so Tack calls it "Amp EG" ("Drum 1 Amp EG Decay").
- **Kits:** Tack offers kit files from the device's SD card (`.SND`) as presets, through CLAP's preset discovery, so hosts list them in their preset browser. It looks in the kit folder, `~/.local/share/Tack/Kits/` on Linux (or `$XDG_DATA_HOME/Tack/Kits/`) and `Documents/Tack/Kits` on Windows and macOS; each subfolder is a sound pack, e.g. a symlink to a backup of a project folder. Kits named "Initkit" are skipped, also when the device garbled two of its letters: the device fills empty kit slots with them, and the built-in "Init Kit" preset is the same sound. No kits are bundled with Tack; the factory and third-party kits belong to their authors.
- **Choosing kits in the editor:** the same kits are in the editor too. Clicking the kit's name opens a browser: the kit folder's folders on the left (a pack, or a project folder in it), their kits on the right alphabetically. Clicking a kit loads it and leaves the browser open, to try one after another. The arrows next to the name step to the previous or next kit in the same folder. The editor reads the kit folder each time, so new kits show up at once. A `*` after the name means the kit was edited in the editor since it was loaded or saved; loading another kit replaces those edits without asking, as on the device. Tack tells the host which preset it loaded, and the project keeps the kit file's path, so the arrows carry on from there when the project is opened again.
- **Saving kits:** "Random" in the save box picks a name. The editor's "Save" button writes the kit as it is now to the "User" pack, `Kits/User/<name>.SND`, where the preset browser lists it with the others. The file is what the device writes: a name of up to 8 characters from the device's character set, then the kit's parameters. The device finds kits by slot number, so on its SD card a kit needs a file name such as `05-NAME.SND`. Saving under the name of a kit that is already there asks before overwriting it. Hosts may only see a new kit once they rescan their presets.
- **LFO sync:** an LFO's Sync switch makes Freq a fraction of the bar at the host's tempo, in whole BPM. Each way, the other control jumps to the nearest speed. To the host these stay two parameters, Freq and a Clock Sync list, so automation can reach all the fractions.
- **Delay sync:** the delay's mode has two more entries. In them Time picks a fraction of the bar at the host's tempo, though the dial still reads as a number.
- **LFO once:** Retrig's list has each voice twice, the second as once. The LFO then plays start to end when that voice fires, and finishes on the value the target is set to, so a rising wave ducks and recovers. FX Dry/Wet is a target, so an LFO can duck the FX.
- **Random per hit:** a random LFO with Retrig set to a voice takes a new value each time that voice fires, and holds it until the next hit, as on the FM-1. At the lowest Freq it changes only on hits, so a small Amount varies each hit a little.
- **Mutating kits:** the die makes a small random change to the open tab's voice, and Shift-click (two dice) to every voice. Each click goes further. Its arrow retraces those steps exactly, and its number says how far you are from where you began, up to 99; the number is bright while the steps are the die's. Volume, pan, routing, modulation targets, filter type and the FX stay. An edit or another kit starts again from there. A voice plugin's die changes its voice.
- **Reverb:** the LXR's code leaves it switched off, and Tack turns it on, in stereo. It has a Low Cut and a High Cut, adds to the dry sound, and has fixes to the flutter and growl of its tail.
- **Editor:** a tab per voice plus FX. Each tab shows the front panel's sub-pages as boxes of one row of controls, three boxes to a row, titled in the voice's colour. `clap/layout.h` has the layout's numbers, and `tools/layout_test.cpp` checks every tab against its rules. Each control has its label on top: knobs for amounts with their value under them (drag, Shift for fine, wheel steps, double-click resets to the init kit value), switches for options with two values and lists for options with more, with short labels. Tooltips, off until turned on in the logo's menu, show a shortened label or value in full. The FX tab shows the controls of the selected FX type under that type's names. It is Dear ImGui (MIT) drawn into a pugl (ISC) OpenGL view, fetched at pinned versions, embedded in the host's window (X11, Win32 or Cocoa); on X11 the host's timer drives it. Edits go to the audio thread through a queue and reach the host as parameter gestures. `build/host/gui_test Tack.clap [seconds] [kit.snd]` opens it without a DAW (also under Xvfb, with `__GLX_VENDOR_LIBRARY_NAME=mesa` for software OpenGL).
- **About:** the logo's menu in the editor opens a box with the version, where the engine comes from, the copyright and licence notices with the warranty disclaimer (the GPL's "Appropriate Legal Notices"), the libraries Tack is built with and Steinberg's trademark line.
- **Scaling and zoom:** the editor is 912 x 430 units (a voice plugin's 912 x 401). On X11 and Windows the host's scale factor (CLAP's `set_scale`, VST3's content scale) sets how many pixels a unit takes, and the editor reports its size in pixels; a new factor while it shows, as when the window moves to another screen, resizes it. On macOS the host works in points and the editor takes the screen's backing scale, 2 on a Retina screen, and a new one when the window moves to a screen with another (`cmake/pugl-backing-scale.patch`: pugl only noticed on a resize). With no factor from the host the editor stays at 1. The logo's menu has a zoom from 50% to 200% in steps of 25%, on top of that scale, and so has the grip in the bottom right corner: a drag shows the zoom it would take and releasing takes it, without the window changing under the pointer while dragging (Esc cancels). The host can't resize the editor by its frame; the project keeps it, and a state loaded while the editor is open (a VST3 or AU preset is one) leaves the open editor's zoom alone. The editor asks the host for its new size from `on_main_thread`, outside its own event handling, since a host may resize it from inside the request (the Audio Unit wrapper calls `set_size` there); it takes the zoom before asking, and goes back to the old zoom if the host refuses. Its size is always the layout's times the zoom and the scale, so what it reports and what it draws can't drift apart. ImGui lays out in units and draws at the full resolution, and the font is ImGui's vector font (its pixel font's shapes), rasterized at the size it is drawn at, so text stays sharp at every scale and zoom. `gui_test` takes a factor from `$TACK_SCALE`, a change while it runs from `$TACK_RESCALE` (`"1.5@3"`, factor 1.5 after 3 seconds) and a zoom saved in the state from `$TACK_ZOOM` (`"150"`).
- **State:** the kit itself, in the firmware's representation, the outputs switch, the kit's name, its file and whether it was edited.
- **Pulse width:** a deliberate difference from the device: the PWM waveform subtracts two copies of a saw offset by the pulse width, so at width 0 it is silent. Pulse width only matters for PWM, so its minimum is 1 here, and kits that store 0 play with 1.
- **Latency:** reported to the host, which compensates on playback.
- **Outputs:** seven stereo outputs: Main, Drum 1, Drum 2, Drum 3, Snare, Cymbal and Hi-hat. Each voice's Output is Direct or FX: it plays as it is, or goes through the FX bus. The bus works as on the device, a group bus with an insert: the voices routed to it are summed and processed together, and the bus plays on Main. The "Outputs" switch, in the Master box on the Mix & FX page, decides where Direct voices go: with "Stereo" (the default) to Main, so everything is there, with "Individual" to their own outputs, panned, leaving Main with only the voices that go through the FX. Both settings sum to the same mix. The switch is saved with the project, not with kits. It isn't a host parameter, so the host has no automation or undo for it. With "Stereo" only Main is resampled to the host's rate, which halves Tack's CPU use (about 1.3% of one core here, against 2.8% with "Individual"). Switching to "Individual" while playing starts resampling the voices' outputs from that moment, so they can be up to half an engine sample (11 µs) off Main until the project is reopened or the audio engine restarts. The device's other destinations, its second output pair and the mono jacks, become Direct (and the FX bus always returns to Main), a deliberate difference; none of the factory or Hrtl kits use them, and kit files keep what they store.
- **Instances:** the engine keeps its state in globals, so each instance runs its own copy of it. Tack loads a private copy of its own binary per instance (`clap/engine.c`; on Linux from an in-memory file, on Windows and macOS as described below) and drives that copy's engine through a function table, `tack_engine()`, the only symbol it exports besides `clap_entry`. Everything else has hidden visibility, so each copy's code uses its own globals. Instances can process in parallel. `build/host/multi_test Tack.clap kit.snd...` renders a different kit in each of several instances, interleaved and on one thread each, and checks every result against the kit rendered alone.

### Single voices: Tack Drum, Snare, Cymbal and Hi-hat

`Tack.clap` holds four more plugins, one per kind of voice, for playing a single voice on its own track or in a host's drum rack. Each runs a whole engine but plays and shows one voice: Tack Drum is drum 1 (drums 1-3 are the same engine), the others the snare, cymbal and hi-hat.

- **Notes:** on any channel, chromatically: C3 plays the voice at its tuned pitch. The keyboard switch next to the name (the "Chromatic" parameter) turns that off: every note then plays the tuned pitch, as a drum pad would. Tack Hi-hat plays the open hat on channel 2 and the closed hat on every other channel; a closed hit chokes an open one, as on the device. Open and closed differ only in their decay ("Decay Open", "Decay Closed").
- **Presets:** the voices of the kits in the kit folder, in the plugin's own browser (click the name; the arrows step through a folder): every kit's drums 1-3 in Tack Drum ("Lambo Drum 2"), its snare, cymbal or hi-hat in the others, leaving out voices that are just the init kit's. A voice is copied onto the plugin's voice with its modulation: targets and LFO retrigger on its own voice move with it. What depended on the rest of its kit doesn't come along (another voice's LFO on it, its LFO on another voice, an LFO retriggered by another voice), so a few voices sound different on their own. The host's preset browser only lists the init kit for the voice plugins: a kit file that offers several presets is a preset container, and hosts didn't list those (Bitwig 6.1.6 showed no Tack presets at all).
- **Saving:** the editor's "Save" button writes the voice as a kit with only this voice, to the "User" pack like Tack's kits: the voice in its own place (Tack Drum's on drum 1), every other voice the init kit's. The voice browser lists it under its own name ("Clap", not "Clap Drum 1"). Tack's browser lists it with its kind of voice ("Clap (Snare)"), and picking it there replaces only that voice of the open kit, keeping the voice's output (Direct or FX). A drum goes onto the drum tab that is open, or onto drum 1 from any other tab. The kit keeps its name and is marked edited. The host's preset browser and the device load the file as a whole kit, with the init kit's other voices.
- **Parameters:** the voice's own, named without the voice ("Filter Frequency", ids as in Tack). Velocity and LFO destinations offer only the voice's own parameters. No FX bus: the voice plays directly on the plugin's one output.
- **Editor:** the voice's page of the Tack editor, without its output menu; no play button, notes play it.
- **CPU:** the engine computes only the plugin's voice, without the FX bus, and resamples only its two channels: about 0.7% of one core here, against 1.3% for a whole Tack. The noise in snare, cymbal and hi-hat sounds differs sample by sample from the same voice in Tack (other voices no longer draw from the random number generator in between), not in level or colour.
- Velocity and LFO destinations of the other voices are off, since those voices aren't computed.

To install the plugins into the user's plugin folders (`~/.clap` and `~/.vst3` on Linux): `cmake --build build/host --target install_tack`. It copies each one in one step rather than linking to the build output, so a host watching the folder never loads a half-written file mid-build.

`build/host/clap_render Tack.clap out.wav [rate] [-s] [-p kit.snd] [[channel:]note | id=value | id~amount [@seconds] ...]` is a minimal CLAP host for testing without a DAW: it loads a kit through the preset-load extension, plays notes (on channel 1 unless given, e.g. `2:48` for drum 1 an octave down), sets parameters (`=`) or modulates them (`~`) through flush or as timed events, and with `-s` runs the state through save, a new instance and load before playing. `gui_test` takes modulation from `$TACK_MOD` (`"37~-60 8~30"`) to show it in the editor. `clap-validator list presets build/host/Tack.clap` shows what the preset browser will see.

`clap-validator validate build/host/Tack.clap`: 192 tests for the five plugins, 162 passed, 0 failed, 30 skipped. The skipped tests are for extensions Tack doesn't have (64-bit audio, configurable ports).

Not done yet: trying Tack in a DAW on Windows, a styling pass on the editor.

## VST3 and Audio Unit

[clap-wrapper](https://github.com/free-audio/clap-wrapper) (MIT, 0.17.0) builds `build/host/Tack.vst3` on every platform and `Tack.component`, an Audio Unit (AUv2), on macOS. Each is Tack's code with the wrapper's VST3 or AU side linked in, so it is complete on its own and doesn't load `Tack.clap`. The VST3 SDK (MIT since 3.8) and Apple's AudioUnitSDK (Apache 2.0) come with it, fetched at pinned versions. `-DTACK_VST3_AU=OFF` builds only the CLAP.

- **Plugins:** the same five, as five VST3 classes and five Audio Units. The wrapper derives the VST3 class ids from the CLAP ids. An Audio Unit is known by type, subtype and manufacturer code, and hosts save those in projects, so `clap/wrapper_info.cpp` fixes them for good: type `aumu`, manufacturer `Oiko`, subtypes `Tack`, `Tdrm`, `Tsnr`, `Tcym` and `That`.
- **What carries over:** parameters (VST3 parameter ids are the CLAP ids), notes with their MIDI channel, the state, latency, the outputs (Tack's seven stereo buses) and the editor. On Linux the editor needs a host that gives plugins a run loop, as VST3 hosts do.
- **Kits:** VST3 and Audio Units have no equivalent of CLAP's preset discovery, so kits come from the editor's own browser.
- **Instances:** as in the CLAP. On Linux and Windows each instance loads a copy of the `.vst3` binary itself; on macOS each bundle has its own `Contents/Frameworks/TackEngine.dylib`. Each macOS binary names pugl's Objective-C classes after its format (`TackClapPuglWrapperView`, `TackVst3PuglWrapperView`, `TackAuPuglWrapperView`), so a host that loads two formats in one process doesn't get two classes with one name.
- **Files:** `Tack.vst3` is a bundle folder on every platform, with the plugin in `Contents/x86_64-linux/Tack.so`, `Contents/x86_64-win/Tack.vst3` or `Contents/MacOS/Tack`. On Linux it exports only the VST3 entry points and `tack_engine()` (`clap/vst3.map`).
- **Installing:** `install_tack` also copies `Tack.vst3` to `~/.vst3` on Linux, `%LOCALAPPDATA%\Programs\Common\VST3` on Windows and `~/Library/Audio/Plug-Ins/VST3` on macOS, and `Tack.component` to `~/Library/Audio/Plug-Ins/Components`.

`build/host/vst3_test build/host/Tack.vst3` hosts the VST3 the way a DAW does, on the VST3 SDK's hosting classes. It checks that the five plugins are there and play, that MIDI channels reach Tack (channel 2 plays drum 1 chromatically, channel 10 nothing), that three instances playing at once render the same as each alone, and that a saved state brings back the sound. `smoke_test.sh` runs it. Steinberg's validator:

```sh
cmake --build build/host --target vst3_validator
build/host/validator-build/bin/Debug/validator build/host/Tack.vst3
```

It runs 235 tests for the five plugins, and all pass. On macOS, `auval -v aumu Tack Oiko` checks the Audio Unit once `install_tack` has put it in the Components folder. clap-wrapper 0.17.0 prints a line like `AUDIO in: 0, out: 7` to stderr for each instance it creates.

## Windows and macOS

The sound engine renders the same on Windows as on Linux, sample for sample: `clap_render` and `multi_test` give byte-identical files under Wine. Nobody has played Tack in a DAW on Windows yet. On an M1 Mac, the CLAP and the VST3 work in Bitwig 6.1.6 and the Audio Unit in Logic Pro.

Windows, cross-compiled from Linux with mingw-w64:

```sh
cmake -S host -B build/win -G Ninja -DCMAKE_TOOLCHAIN_FILE=$PWD/host/cmake/mingw-w64.cmake
cmake --build build/win
wine build/win/clap_render.exe build/win/Tack.clap out.wav
```

macOS, with the Xcode command line tools, CMake and Ninja:

```sh
cmake -S host -B build/mac -G Ninja
cmake --build build/mac
build/mac/clap_render build/mac/Tack.clap out.wav
```

- **Files:** on Windows, `Tack.clap` is one DLL. It and the VST3 link the C and C++ runtimes statically and need only system DLLs, including the Universal C Runtime that comes with Windows 10 and 11. On macOS it is a bundle with the plugin in `Contents/MacOS` and the engine library in `Contents/Frameworks`. The build signs the bundle ad hoc, which lets it run on the Mac that built it; a release needs a Developer ID signature and notarization.
- **Installing:** `install_tack` copies the plugin to `%LOCALAPPDATA%\Programs\Common\CLAP` on Windows and to `~/Library/Audio/Plug-Ins/CLAP` on macOS.
- **Kit folder:** `Documents/Tack/Kits` on both. Paths are UTF-8 throughout (`src/lxr_files.c`), so folders and kits with names outside the Windows code page work.
- **Instances:** Windows loads each instance's copy of `Tack.clap` from the temp folder and deletes it when the instance closes. The next instance removes copies that a crash left behind. macOS copies the bundle's engine library, `TackEngine.dylib`, into `$TMPDIR` instead of the plugin itself: each copy of the plugin would register the editor's Objective-C classes again, and the runtime keeps one class per name for the whole process. For the same reason the build renames pugl's classes with a prefix for each binary (`TackClap` and so on), so they can't clash with another plugin's pugl or another format of Tack.
- **Editor:** pugl's Win32 and Cocoa backends. The host's event loop delivers the editor's events there, and the editor redraws from a timer of its own, so it doesn't need the host's timer as on X11. `gui_test` opens the editor on all three platforms, also under Wine, where `$TACK_SCALE` shows it scaled. On a Retina screen `gui_test` shows it at the full resolution; nobody has seen it there in a DAW yet.

## Builds, releases and repository settings

`.github/workflows/build.yml` builds Tack for Linux (on Ubuntu 22.04, so it runs with glibc 2.35 or newer), Windows (mingw-w64, tested through Wine) and macOS (one bundle for Apple silicon and Intel, macOS 11 or newer). Each job runs `host/tools/smoke_test.sh` and, on Linux and macOS, clap-validator. The Linux job also runs Steinberg's VST3 validator and the macOS job `auval` on the five Audio Units. Each job keeps the platform's zip as an artifact. It runs for pull requests that are ready for review, for pushes to main and by hand.

`host/tools/smoke_test.sh build/host` runs the same tests locally (`host/tools/smoke_test.sh build/win wine` for the Windows build). `make_kit out.snd NAME [param=value ...]` writes the test kits it needs, since none can be bundled.

A release starts from a tag on main that matches the version in `host/CMakeLists.txt`:

```sh
git tag v0.10.0 && git push origin v0.10.0
```

`.github/workflows/release.yml` checks the tag, builds and tests every platform from it and leaves a draft release with the zips and `SHA256SUMS`. Nothing is published until the draft is, and the draft is not a pre-release, so publishing it makes it the latest release. Locally, `-DFETCHCONTENT_TRY_FIND_PACKAGE_MODE=NEVER -DTACK_PACKAGE_PLATFORM=linux-x86_64` and the `package_tack` target build the same zip: the plugins, the READMEs, the licence and every licence of a library linked into them.

`.github/repository.json` describes the GitHub repository's settings and rulesets, as for plugg: no pushes to main except through reviewed pull requests whose three builds pass, linear history, and release tags only from maintainers. `python3 host/tools/github-settings.py` reports differences and `--apply` makes GitHub match. Rulesets need a public repository, so apply them once it goes public.

## Layout

- `shim/` stands in for the STM32 HAL: C versions of the ARM SIMD intrinsics, `__fp16` as `_Float16`, a xorshift RNG, and dummy handles and GPIO ports. The GPIO input registers read 0, so the mixer sees no jacks plugged in and uses the routing from the kit unchanged.
- `src/stubs.c`: no-op sequencer, menu, UART and USB functions the engine links against.
- `src/lxr_host.c`: engine setup (the parts of `soundengine_init()` that don't touch the codec or sequencer), kit loading and parameter changes as the front panel sends them, block rendering, the resampler and event queue.
- `src/lxr_params.c`: the parameter list for plugins, from the menu tables. CMake copies `valueNames` and `parameter_dtypes` out of `menu.c` into `build/host/generated/menu_tables.inc`, since the rest of `menu.c` is LCD code that doesn't build on a desktop; the build stops if it can't find them.
- `tools/`: Python scripts for inspecting renders, e.g. `uv run --no-project --with numpy --with matplotlib python host/tools/overview.py out.png kick=kick.wav`.

## Firmware issues found while porting

- `MidiParser.c`: loading a kit wrote one byte far past `midiParser_originalCcValues` (`PAR_BANK_CHANGE` is sent as CC 0, so `paramNr` underflowed to 65535). Fixed.
- `parameterArray[i].ptr` is NULL for "no modulation target", and the modulation code dereferences it. On the STM32 that reads flash at address 0; on a PC it segfaults. `lxr_host_init()` points those entries at a dummy instead of changing the firmware.
- `Oscillator.c`: the interpolating wavetable oscillators read `table[offset][itg + 1]` with no wrap, so at the end of each cycle they read index 1024 of a `[11][1024]` table: sample 0 of the next band-limited table, or past the end of the array for the last one. `sine_table` has a guard sample and isn't affected. Fixed by wrapping to sample 0 of the same table; the init kit renders bit-identically.
- `Delay.c`: switching the delay type from normal to ping-pong while the delay runs kept pointers that are only valid for the normal layout, and ping-pong indexes `FX_BUFFER` with `half + pointer`: up to index 49150 of a 32768-entry buffer, which on the device sits in DTCM next to other data. Fixed with `Delay_setType()`, which masks the pointers for the new layout.
- `Distortion.c`: `diode()` read `table_1n914[257]`, one past the end, at full-scale input. The value is multiplied by 0, so it only mattered if those bytes held a NaN or infinity. Fixed by interpolating from the last pair instead.
- Left as they are (harmless with GCC on x86 and ARM, but undefined in C): misaligned 32-bit loads in the delay's mono output path (`Delay.c:178`, `:264`) and a left shift of a negative number in `transientGenerator.c:60`.
- The kit loader also sets the velocity and LFO modulation targets (`preset_sendDrumsetParametersImmediate`); the first host harness skipped that. The init kit has none, so renders didn't change.
- Not a bug, but worth knowing when measuring: the first hit after start-up starts later than all others (41 samples into its block instead of 4 for the kick).
- `calcNoise()` reads the ARM overflow flag through `__get_APSR()`, which the shim can't provide, but it is only called from commented-out code. The live noise path, `calcNoiseBlock()`, detects phase wrap portably.
