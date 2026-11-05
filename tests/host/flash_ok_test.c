/* SPDX-License-Identifier: GPL-3.0-only */
/* firmware/hal/fm1_flash_ok.h: the flash parts the storage may use, nothing else */
#include <stdio.h>
#include "fm1_flash_ok.h"

static int fails;

static void check(const char *what, unsigned id, int want)
{
    int got = FL_JEDEC_OK(id) ? 1 : 0;
    printf("  %-20s %06X %s\n", what, id, got == want ? "ok" : "FAIL");
    if (got != want)
        fails++;
}

int main(void)
{
    check("Puya P25Q80", 0x856014u, 1);
    check("UC25HQ80", 0xB36014u, 1);
    check("no part answered", 0u, 0);
    check("all ones", 0xFFFFFFu, 0);
    check("Winbond 4 MiB", 0xEF4016u, 0);
    check("near-miss Puya", 0x856015u, 0);
    check("near-miss UC25", 0xB36015u, 0);
    return fails != 0;
}
