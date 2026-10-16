/* SPDX-License-Identifier: GPL-3.0-only */
/* MIN-1's demo video (web/demo/record.mjs): the browser build (web/emu/min_web.c) as it is, with a few
 * readings more, so the storyboard can press keys on the chord and the log can say what plays. */
#include "../emu/min_web.c"

#define DEMO_EXPORT __attribute__((used, visibility("default")))
DEMO_EXPORT float demo_chord_t(void) { return en_chord_t; }
DEMO_EXPORT uint32_t demo_beats(void) { return en_beats; }
DEMO_EXPORT uint32_t demo_playing(void) { return en_playing; }
DEMO_EXPORT uint32_t demo_section(void) { return en_section; }
DEMO_EXPORT uint32_t demo_piece(void) { return proj.piece; }
DEMO_EXPORT uint32_t demo_view(void) { return ui.view; }
DEMO_EXPORT uint32_t demo_cycle(void) { return (uint32_t)proj.par[P_CYCLE]; }
DEMO_EXPORT uint32_t demo_cyc_pos(void) { return en_cyc_pos; }
DEMO_EXPORT uint32_t demo_yours(void) { return en_yours; }
DEMO_EXPORT uint32_t demo_voices(void) { return en_voices; }
DEMO_EXPORT uint32_t demo_parts(void)          /* a bit a part playing: upper, lower, bass, lead, chords */
{
    uint32_t m = 0;
    int p;
    for (p = 0; p < EN_NPART; p++)
        m |= (uint32_t)(en_part_on[p] != 0) << p;
    return m;
}
DEMO_EXPORT uint32_t demo_notes(int p) { return p >= 0 && p < EN_NPART ? en_notes[p] : 0; }
DEMO_EXPORT const char *demo_chord(void)       /* the chord sounding, by name */
{
    static char t[16];
    en_chord_name(en_chord_mask, en_chord_bass, t);
    return t;
}
DEMO_EXPORT const char *demo_msg(void) { return plat_ms() < ui.msg_until ? ui.msg[0] : ""; }
DEMO_EXPORT const char *demo_msg2(void) { return plat_ms() < ui.msg_until ? ui.msg[1] : ""; }

/* the cycle an EDIT on the Form page would roll if it came at ms (ui.c's idea_rnd from idea_r), by name in
 * the key playing: for choosing when the storyboard presses it */
DEMO_EXPORT uint32_t demo_idea_r(void) { return ui.idea_r; }
DEMO_EXPORT const char *demo_roll(uint32_t ms, uint32_t idea_r)
{
    static char out[96];
    uint32_t x = idea_r ^ (ms * 2654435761u);
    int seed, n, k, root = key_root_now();
    char *o = out;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    seed = (int)((x ? x : 1u) % 16000u) * 2 + key_minor();
    n = en_cycle_get(EN_CYC_ROLLED, seed, -1, 0, 0);
    for (k = 0; k < n; k++) {
        uint16_t m;
        uint8_t b;
        char t[16];
        en_cycle_get(EN_CYC_ROLLED, seed, k, &m, &b);
        en_chord_name(rot12(m, root), (b + root) % 12, t);
        o = puts_(o, k ? " " : "");
        o = puts_(o, t);
    }
    return out;
}
