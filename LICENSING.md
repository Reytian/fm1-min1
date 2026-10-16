# MIN-1 licensing

MIN-1 is free software under the GNU General Public License, version 3 only
(`GPL-3.0-only`, full text in `LICENSE`). It is built on Felucca's platform layer (through X0X, FoMni and
AMB-1) and keeps Felucca's licence. If you distribute MIN-1, or firmware derived from it, you must give
your recipients its complete corresponding source under the same licence.

## Where the code comes from

| What | Origin | Licence |
| --- | --- | --- |
| Platform: `firmware/hal/`, `firmware/loader/`, `firmware/src/{libc,lcd,gfx,usb,storage,ota,midi_uart}.c`, `tools/` (build, package, install, rescue), `web/fm1*.js` | [Felucca](https://github.com/hugelton/Felucca), Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments, as changed for [X0X](https://github.com/charlesvestal/fm1-x0x), FoMni and [AMB-1](https://github.com/charlesvestal/fm1-amb) (Charles Vestal), then MIN-1 | GPL-3.0-only |
| `firmware/src/app/{panel,plat_fm1,main_fm1,project}.c`, `firmware/src/app/{app,plat}.h`, `host/min_host.c`, `web/emu/`, the drawing helpers and knob strip in `firmware/src/app/ui.c` | AMB-1 at d4a211c (adapted from X0X, itself from Felucca's `panel.c`, `main.c`, `audio.c`), changed for MIN-1 | GPL-3.0-only |
| `firmware/src/dsp/fastmath.h` | X0X, by way of AMB-1 | GPL-3.0-only |
| Dattorro's plate reverb and the voice leading in `firmware/src/dsp/min.c` | AMB-1 (the plate from [FoMni](https://github.com/charlesvestal/fm1-fomni)) | GPL-3.0-only |
| The overload guard in `firmware/src/dsp/min.c` | after X0X's (`firmware/src/app/engine.c`) | GPL-3.0-only |
| The idea of a browser emulator for FM-1 firmware (`web/emu/`) | [Groove OS](https://www.groove-os.com/) by Peter Gombos, whose site runs its firmware in the browser; no code from it | credit only |
| Everything else in `firmware/src/{app,dsp}`, `tests/`, `docs/`, `web/demo/` | MIN-1, Copyright (C) 2026 Reytian | GPL-3.0-only |

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Barlow Semi Condensed (The Barlow Project Authors), the UI face | SIL OFL 1.1 | `assets/fonts/BarlowSemiCondensed-*.ttf`, `assets/fonts/Barlow-OFL.txt` |
| Terminus (Dimitar Toshkov Zhekov), an alternative font set | SIL OFL 1.1 | `assets/fonts/ter-u*.bdf`, `assets/fonts/Terminus-LICENSE.txt` |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` are read from your SDK checkout at build time and placed in the package; no SDK files are in this tree | Apache-2.0 | <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |

## Music

MIN-1 is inspired by AMB-1 and by the music of Philip Glass (*Glassworks* above all). The chord cycles
are plain progressions of the common practice (i–VI–iv–V and the like) and the models follow techniques,
not pieces: no melody, figure or progression is taken from any work.

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments.
"M-VAVE" and "FM-1" are trademarks of their respective owners. MIN-1 is independent firmware; it is
not affiliated with, endorsed by or supported by any of them, nor by Charles Vestal or Philip Glass.

## Radio

MIN-1 never enables the Bluetooth / Wi-Fi radio of the hardware.
