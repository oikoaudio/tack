# Tack

Tack is a drum synth for desktop computers, built as CLAP, VST® 3 and Audio Unit plugins by oikoaudio. It runs the
sound engine of the LXR-02 drum machine by Sonic Potions and Erica Synths. Tack is a modified version of that
firmware's sound engine. The engine code in `Core/Src/Soundengine` is the firmware's, changed only by a few bug fixes,
and Tack adds the plugins, their editor and the code that runs the engine on a computer.

Tack is not affiliated with or endorsed by Erica Synths or Sonic Potions.

## Plugins

Tack comes as `Tack.clap`, `Tack.vst3` and, on macOS, `Tack.component`, the Audio Unit. Each holds the
same five plugins.

| Plugin | Plays |
|---|---|
| Tack | The whole kit, which is three drums, snare, cymbal, closed and open hi-hat, and the FX bus. It has seven stereo outputs, Main and one each for the three drums, snare, cymbal and hi-hat. |
| Tack Drum | One drum voice |
| Tack Snare | The snare |
| Tack Cymbal | The cymbal |
| Tack Hi-hat | The closed and open hi-hat |

In Tack, notes on MIDI channel 1 from C3 up trigger the voices, one note each. Channels 2 to 8 each play one voice
chromatically. The single-voice plugins play chromatically on any channel, or at the voice's tuned pitch on every note
when their Chromatic switch is off.

Every sound parameter on the device's front panel is a plugin parameter, 227 in Tack, and the host can automate and
modulate them. Each plugin has its own editor. The editor does not copy the hardware's front panel or its look.

## Kits

Tack loads kit files in the device's `.SND` format, the files the LXR-02 writes to its SD card. Put folders of kits
in the kit folder, `~/.local/share/Tack/Kits/` on Linux (or `$XDG_DATA_HOME/Tack/Kits/` if that is set) and
`Documents/Tack/Kits` on Windows and macOS. Each folder in it is a pack.
Tack lists the kits in its own kit browser, and the CLAP also lists them in the host's preset browser. The single-voice plugins offer the
matching voices of every kit, so Tack Snare lists each kit's snare.

Saving a kit writes a `.SND` file to the `User` pack in the format the device writes, and the device can load it. On
the device's SD card a kit file needs a slot number in its name, such as `05-NAME.SND`. The single-voice plugins save
a kit with only their voice in it. Tack's browser marks such files with the kind of voice, "Clap (Snare)", and
picking one replaces only that voice of the open kit. A drum goes onto the drum tab that is open.

No kits come with Tack. The factory kits and third-party kits belong to their authors.

## Status

Tack works on Linux x86-64 and on an Apple silicon Mac (M1). The CLAP and the VST3 are tested in Bitwig 6.1.6 on
both, and the Audio Unit in Logic Pro. It also builds for Windows. The Windows build renders the same as Linux under
Wine, but nobody has played it in a DAW yet. The VST3 passes Steinberg's validator and Tack's own VST3 tests on Linux
and Windows, and CI checks the Audio Unit with `auval`.
There are no prebuilt binaries, so build Tack from source.

Report a problem with the [bug report form](https://github.com/oikoaudio/tack/issues/new?template=bug-report.yml).
Report security problems privately, as [SECURITY.md](SECURITY.md) describes.

## Building and installing

You need CMake 3.21 or newer, Ninja, a C and C++ compiler and the OpenGL and X11 development files. Tack is tested
with gcc 15. CMake fetches the CLAP SDK, clap-wrapper, the VST3 SDK, Apple's AudioUnitSDK, pugl and Dear ImGui at pinned
versions, and libsamplerate if it isn't installed.

```sh
cmake -S host -B build/host -G Ninja
cmake --build build/host
cmake --build build/host --target install_tack
```

The last command copies `Tack.clap` to `~/.clap` and `Tack.vst3` to `~/.vst3`. Add `-DTACK_VST3_AU=OFF` to the
first command to build only the CLAP. [host/README.md](host/README.md#windows-and-macos) has the
commands for Windows and macOS.

[host/README.md](host/README.md) is the technical reference. It covers the MIDI note map, parameters and modulation,
outputs and FX routing, how kits load and save, the command-line tools for testing without a DAW, and the bugs fixed in
the firmware code.

## Repository layout

| Path | Contents |
|---|---|
| `host/` | Tack. The desktop build of the sound engine, the plugins, the editor and test tools. |
| `Core/Src/Soundengine/` | The firmware's sound engine, which Tack compiles. |
| `Core/`, `Drivers/`, `Middlewares/`, `FATFS/`, `USB_DEVICE_MIDI/`, `cmake/`, `CMakeLists.txt` | The original LXR-02 firmware for the STM32F765. |

The firmware still builds for the device, with `cmake --preset release` and `cmake --build --preset release` in the
top folder and the Arm GNU Toolchain on `PATH`. The
[upstream README](https://github.com/erica-synths/lxr-desktop-app) explains the toolchain setup and how to install an
update from the SD card. Firmware built from this repository is a modified version, and its start-up screen says
"Modified V" and the version, as [NOTICE.md](NOTICE.md) requires.

## Changes to the firmware

This fork changes the firmware in six places. The first four are bug fixes in the sound engine, found while running
it on a computer, and Tack and the device firmware both get them. None of them has been reported upstream.

| Commit | File | Change |
|---|---|---|
| 3d578f9 | `Core/Src/Soundengine/MIDI/MidiParser.c` | Loading a kit wrote one byte far past the end of `midiParser_originalCcValues`. The kit loader sends `PAR_BANK_CHANGE` as CC 0, so the parameter number underflowed to 65535. The parser now indexes with the CC number. |
| bccf2ac | `Core/Src/Soundengine/DSPAudio/FX/Delay.c`, `Delay.h`, `MidiParser.c` | Switching the delay from normal to ping-pong while it ran kept pointers that are only valid in normal mode, and the next samples read and wrote up to index 49150 of the 32768-entry delay buffer. `Delay_setType()` now masks the pointers for the new mode. |
| 05ee9e3 | `Core/Src/Soundengine/DSPAudio/Oscillator.c` | The interpolating wavetable oscillators read one sample past the end of each 1024-sample table, from the next table or past the end of the array. They now wrap to the start of the same table. The init kit renders bit-identically. |
| 3f7b14f | `Core/Src/Soundengine/DSPAudio/FX/Distortion.c` | At full-scale input the diode distortion read one entry past the end of `table_1n914`. It now interpolates from the last pair of entries, which gives the same result. |
| 2d0bd12 | `Core/Src/main.c` | The start-up screen's second line reads "Modified V 1.7" instead of "LXR2 Drums V 1.7". The first line still credits Sonic Potions. |
| 2d0bd12 | `Core/Inc/Soundengine/config.h`, `Core/Inc/main.h` | Builds for a computer (`LXR_HOST_BUILD`) leave out the attributes that place code and data in the STM32's fast memories, since macOS rejects their section names. The device build is unchanged, byte for byte. |

Tack also plays a few things differently from the device. Those changes live in its own code under `host/`, not
in the firmware, and [host/README.md](host/README.md) describes them: the pulse width minimum, the init kit's FX
values and the output routing.

## License

Tack is free software under the GNU General Public License version 3 only, in [LICENSE](LICENSE), with the additional
permission and additional terms under section 7 in [NOTICE.md](NOTICE.md). These are the terms Erica Synths published
the LXR-02 firmware under. It comes without any warranty.

The additional terms apply to Tack as well.

- "Erica Synths", "LXR" and "LXR-02" and their logos are trademarks of Erica Synths. The GPL grants no right to use
  them. Tack uses them only to say where its code comes from.
- Modified versions must be marked as modified and must not be presented as official Erica Synths software. Tack is
  a modified version.
- The LXR hardware, including its front panel and appearance, is the property of Erica Synths and is not covered by
  any license in this repository.

The firmware descends from Julian Schmidt's original LXR firmware, copyright 2013 Julian Schmidt, which Sonic Potions
published under its own license, in [LICENSES/SonicPotions-LXR.txt](LICENSES/SonicPotions-LXR.txt). That license
forbids selling the code or using it in a commercial product or activity, requires modified versions to come with
their complete source, and requires its notice, conditions and disclaimer to come with every copy. The LXR-02
firmware's release doesn't say how that license relates to the GPL, so Tack keeps to both.

- Tack is free. It is never sold, bundled with anything paid, or offered behind a paywall.
- Every release comes with its complete source, including the source of the libraries built into the plugin.
- Every release includes the Sonic Potions license, the GPL and NOTICE.md.

If you redistribute Tack or build on it, keep to the same terms.

Third-party code in the repository keeps its own license, listed in [NOTICE.md](NOTICE.md#third-party-components). The
plugin build also fetches the CLAP SDK (MIT), clap-wrapper (MIT), the VST3 SDK (MIT), AudioUnitSDK (Apache 2.0), pugl
(ISC), Dear ImGui (MIT) and libsamplerate (BSD-2-Clause), and each release includes the licenses of those built into
its plugins.

## Credits

Tack's sound is the LXR-02's: the voices, the FX bus and the kit format are the work of Sonic Potions and Erica
Synths. The LXR-02 is Sonic Potions' design, made and sold by Erica Synths, who published its firmware at
[github.com/erica-synths/lxr-desktop-app](https://github.com/erica-synths/lxr-desktop-app), copyright 2012-2026
Erica Synths. This repository is a fork of it.

That firmware grew out of the original LXR by Julian Schmidt of Sonic Potions, published at
[github.com/SonicPotions/LXR](https://github.com/SonicPotions/LXR), and many of its source files still carry his name.
The Sonic Potions firmware also has code from Rudeog, Patrick Dowling and Andrew Shakinovsky, whom its README thanks.

Tack's plugin, editor and host code is by oikoaudio, at
[github.com/oikoaudio/tack](https://github.com/oikoaudio/tack). The VST3 and the Audio Unit are made from the CLAP
with [clap-wrapper](https://github.com/free-audio/clap-wrapper) by the CLAP team.

VST is a registered trademark of Steinberg Media Technologies GmbH.
