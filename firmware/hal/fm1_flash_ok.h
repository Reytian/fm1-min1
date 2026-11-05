/* SPDX-License-Identifier: GPL-3.0-only */
/* The flash parts the storage may use. The first FM-1s carry a Puya P25Q80 (JEDEC 85 60 14); units sold since autumn
 * 2026 (mostly AliExpress) a UC25HQ80 (B3 60 14). Both 1 MiB, the same plain SPI NOR commands. Any other answer: RAM
 * only, nothing written, and no update from the running firmware (main_fm1.c). */
#pragma once
#define FL_JEDEC_OK(id) ((id) == 0x856014u || (id) == 0xB36014u)
