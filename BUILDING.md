# Building MIN-1

The build makes three files in `build/`:

| File | What |
| --- | --- |
| `min.bin` | the firmware app |
| `loader/ota.bin` | the update loader |
| `min.fwsc` | the installable package (app + loader) |

## Prerequisites (macOS)

- Python 3 with Pillow: `pip3 install Pillow`
- Docker (Docker Desktop, or Colima: `brew install colima docker`, then `colima start` after each
  reboot). The JieLi toolchain is Linux x86-64 only; the build runs it in one long-lived `linux/amd64`
  `debian:bookworm-slim` container (Rosetta, or QEMU without it: a build takes about 9 s either way).
  Keep the source tree in a folder Docker can share, e.g. under `/Users`.
- The JieLi Linux toolchain (clang 4.0.1 for pi32v2, from JieLi's package server):

  ```
  tools/get_toolchain.sh            # installs to ~/.jieli/toolchain
  ```

- The JieLi AC79 SDK (Apache-2.0). The package uses three of its files
  (`cpu/wl82/tools/uboot.boot`, `cfg_tool.bin`, `cfg/eq_cfg_hw.bin`); they are not part of this tree.

  ```
  git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
      https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
  ```

- Node.js (optional, for the web tests).

On Linux x86-64 the toolchain runs natively and Docker is not needed.

## Build

```
./build.sh
```

`JIELI_TOOLCHAIN` and `AC79_SDK` override the default locations
(`~/.jieli/toolchain`, `~/fw-AC79_AIoT_SDK`).

`./build.sh --release 0.1` makes a release build; the package is `build/min-0.1.fwsc`, identity `FM-1_5000100`.

On macOS with podman instead of Docker, put a `docker` script that runs `exec podman "$@"` first
on your PATH. `EN_JOBS` (default 4) limits parallel compiles: a podman machine drops
connections when many containers start at once.

Build option: `EN_CDC=1` adds Felucca's USB serial function (off by default: one plain
MIDI interface).

The build also generates `build/gen/` (the font). It fails if a soft-double routine is linked
(a `double` crept in) and checks the image, RAM and pool sizes.

## Tests

```
tests/run_tests.sh
```

Runs, on the build machine: the maths library against libm; the instrument (`tests/host/min_test.c`:
the cycles, chord names and spelling, a key's triad, voice leading, 2 against 3 exact, the keys taking
the chord and giving it back, the additive process, every part alone, the same start each time, the
overload guard, ten minutes of the busiest pieces); flash storage; Felucca's update-path tests against
`build/min.fwsc`; the whole app in the simulator (`tests/scenarios/*.min`), with its screenshots and audio
in `build/scenarios/`; and each piece's load on the FM-1 (`tests/host/min_render.c`).

`host/build_host.sh` builds the simulator alone; `build/host/min_host SCRIPT OUTDIR` runs one
script (the command list is at the top of `host/min_host.c`).

## Install

From the command line (needs `pip3 install mido python-rtmidi`):

```
python3 tools/fm1_install.py build/min.fwsc
python3 tools/fm1_install.py --info          # identity of the connected FM-1
```

Installing firmware is at your own risk. Hold OCT− and OCT+ for 5 seconds for Felucca's update
mode. If the FM-1 no longer starts but reaches the chip's update mode (4C4A:8057 on USB),
`tools/fm1_rescue.sh` puts stock firmware back from a Mac; otherwise recovery needs
[FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter).

## The load on the FM-1

`tests/host/min_render.c` renders each piece and counts the instructions it runs a frame in a scalar
build (`-fno-vectorize -fno-slp-vectorize`, on an Apple-silicon Mac through `proc_pid_rusage`). X0X's
manual gives the FM-1's load for its PERF TEST's three patterns (43%, 83%, 115% of one core); its
simulator counts 1464, 2708 and 3888 instructions a frame for them, so one instruction a frame is about
0.030% of the core. AMB-1's pieces come out at 36-44% on this scale (a 256-frame block at worst 42-54%),
MIN-1's at 34-57% (worst blocks 43-79%). On the device the Setup page shows the real load; past 90% the
overload guard lightens the voices (G next to CPU).

## The demo video

```
web/demo/build.sh && node web/demo/record.mjs      # build/demo/min1-demo.mp4
```

The emulator's page with a storyboard (`web/demo/storyboard.js`: what is pressed when, the captions) runs a
frame at a time in headless Chromium (Playwright's headless shell, or `$CHROME`), so every frame of picture
has exactly its 1/30 s of sound, straight from the FM-1's code at 44.1 kHz; ffmpeg makes the MP4 and brings
the sound to -16 LUFS. `--fast` gives instead, in seconds, the sound, a log of what plays and stills
(`--every S`, `--at S,S`) in `build/demo/check/`.
