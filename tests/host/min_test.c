/* SPDX-License-Identifier: GPL-3.0-only */
/* The instrument on the host: the pure parts (the cycles, chord names and spelling, a key's triad,
 * figures, voice leading) and the render (2 against 3 exact, the keys take the chord and give it back,
 * the additive process, the form moves, every part heard alone and silent at 0, the same start each
 * time, the overload guard, ten minutes of the busiest piece without a NaN or a clip). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "min.h"

static int fails;
#define CHECK(c, ...)                                                                                              \
    do {                                                                                                           \
        if (!(c)) {                                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                            \
            printf(__VA_ARGS__);                                                                                   \
            printf("\n");                                                                                          \
            fails++;                                                                                               \
        }                                                                                                          \
    } while (0)

static uint32_t midi_count;
#define LOGN 8000
static uint32_t lg[LOGN][2], lgn, logging;      /* note-ons while logging: the take's tick, status and note */
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
    midi_count++;
    if (logging && (st & 0xF0u) == 0x90u && d2 && lgn < LOGN) {
        lg[lgn][0] = en_song_tick;
        lg[lgn][1] = st << 8 | d1;
        lgn++;
    }
}

static int32_t blk[2 * 256];

/* a piece from power-on, playing; its levels changed by the caller after, if it likes */
static void start(int piece)
{
    int16_t par[P_NPARAMS];
    int p;
    en_init();
    en_piece(piece, par);
    for (p = 0; p < P_NPARAMS; p++)
        en_set(p, par[p]);
    en_regrow();
    en_play(1);
    en_render(blk, 0, 2048);
}

typedef struct {
    double peak, ss;
    uint32_t frames, nans;
    uint64_t sum;
} meas_t;

static void run(double secs, meas_t *m)
{
    uint32_t n = (uint32_t)(secs * 44100.0 / 256.0), b, k;
    for (b = 0; b < n; b++) {
        en_render(blk, 256, 2048);
        for (k = 0; k < 512; k++) {
            double x = (double)blk[k] / 8388608.0;
            if (x != x)
                m->nans++;
            if (fabs(x) > m->peak)
                m->peak = fabs(x);
            m->ss += x * x;
            m->sum = m->sum * 31u + (uint32_t)blk[k];
        }
        m->frames += 256;
    }
}

static double rms_db(const meas_t *m) { return 10.0 * log10(m->ss / (2.0 * m->frames) + 1e-20); }

static void test_pure(void)
{
    uint16_t m;
    uint8_t b;
    char nm[16];
    int8_t s[EN_FIG_MAX];
    int n, i;
    /* the cycles: i VI iv V, relative to the root */
    n = en_cycle_get(0, 0, 0, &m, &b);
    CHECK(n == 4 && m == 0x089 && b == 0, "i: %d %03x %d", n, m, b);
    en_cycle_get(0, 0, 1, &m, &b);
    CHECK(m == 0x109 && b == 8, "VI: %03x %d", m, b);
    en_cycle_get(0, 0, 2, &m, &b);
    CHECK(m == 0x121 && b == 5, "iv: %03x %d", m, b);
    en_cycle_get(0, 0, 3, &m, &b);
    CHECK(m == 0x884 && b == 7, "V: %03x %d", m, b);
    en_cycle_get(5, 0, 1, &m, &b);               /* i over its seventh in the bass */
    CHECK(m == 0x489 && b == 10, "i/7: %03x %d", m, b);
    CHECK(en_cycle_minor(0, 0) && !en_cycle_minor(8, 0), "minor flags");
    CHECK(!strcmp(en_cycle_name(EN_CYC_MINE), "Mine") && !strcmp(en_cycle_name(EN_CYC_ROLLED), "Rolled"), "names");
    CHECK(en_cycle_get(16, 0, 0, &m, &b) == 1 && m == 0x489, "Still: one chord, i7 (%03x)", m);
    /* names: the bass as the root when a quality fits it, else the commonest quality */
    en_spell(0, 0);
    CHECK(!strcmp(en_chord_name(0x091, 0, nm), "C"), "C: %s", nm);
    CHECK(!strcmp(en_chord_name(0x089, 0, nm), "Cm"), "Cm: %s", nm);
    CHECK(!strcmp(en_chord_name(0x291, 7, nm), "Am7/G"), "Am7/G: %s", nm);
    CHECK(!strcmp(en_chord_name(0x291, 0, nm), "C6"), "C6: %s", nm);
    CHECK(!strcmp(en_chord_name(0x221, 9, nm), "F/A"), "F/A: %s", nm);
    /* spelling: F minor's VI is Db; A major's iii is C#m; D minor's leading tone is C# */
    en_spell(5, 1);
    CHECK(!strcmp(en_chord_name(0x122, 1, nm), "Db"), "Db in F minor: %s", nm);
    en_spell(9, 0);
    CHECK(!strcmp(en_chord_name(0x112, 1, nm), "C#m"), "C#m in A major: %s", nm);
    en_spell(2, 1);
    CHECK(!strcmp(en_note_name(1), "C#"), "C# in D minor: %s", en_note_name(1));
    en_spell(7, 0);
    CHECK(!strcmp(en_note_name(3), "Eb"), "Eb (borrowed) in G major: %s", en_note_name(3));
    en_spell(0, 0);
    /* a key alone: the triad of the cycle's key on it */
    CHECK(en_triad(2, 9, 1) == 0x224, "D in A minor: D minor %03x", en_triad(2, 9, 1));
    CHECK(en_triad(4, 9, 1) == 0x910, "E in A minor: E major %03x", en_triad(4, 9, 1));
    CHECK(en_triad(11, 0, 0) == 0x824, "B in C major: B dim %03x", en_triad(11, 0, 0));
    CHECK(en_triad(7, 0, 0) == 0x884, "G in C major: G major %03x", en_triad(7, 0, 0));
    /* figures */
    n = en_figure(2, 0, s);
    CHECK(n == 6 && s[0] == 0 && s[3] == 3 && s[5] == 1, "Up-down");
    for (i = 0; i < EN_NFIG; i++)
        CHECK(en_figure(i, 0, s) >= 2 && en_figure(i, 0, s) <= EN_FIG_MAX, "figure %d's length", i);
    /* voice leading: C major to F major, each voice the least way, all F major's */
    {
        int from[4] = {55, 60, 64, 67}, pcs[3] = {5, 9, 0}, to[4], moved = 0, v;
        en_voice_lead(from, pcs, 3, to, 52, 74);
        for (v = 0; v < 4; v++) {
            moved += abs(to[v] - from[v]);
            CHECK(to[v] % 12 == 5 || to[v] % 12 == 9 || to[v] % 12 == 0, "voice %d on %d", v, to[v]);
        }
        CHECK(moved <= 6, "C to F moved %d semitones", moved);
    }
}

/* Morning: the upper in threes against the lower in twos, both from the start (Build 20, two parts) */
static void test_two_against_three(void)
{
    meas_t m = {0};
    uint32_t b0, u0, l0, beats;
    start(0);
    run(2.0, &m);
    b0 = en_beats;
    u0 = en_notes[PT_UPPER];
    l0 = en_notes[PT_LOWER];
    run(30.0, &m);
    beats = en_beats - b0;
    CHECK(en_part_on[PT_UPPER] && en_part_on[PT_LOWER], "both hands play");
    CHECK(abs((int)(en_notes[PT_UPPER] - u0) - 3 * (int)beats) <= 3, "upper: %u notes in %u beats",
          en_notes[PT_UPPER] - u0, beats);
    CHECK(abs((int)(en_notes[PT_LOWER] - l0) - 2 * (int)beats) <= 2, "lower: %u notes in %u beats",
          en_notes[PT_LOWER] - l0, beats);
    CHECK(beats >= 48 && beats <= 52, "100 BPM: %u beats in 30 s", beats);
    CHECK(m.nans == 0 && m.peak < 0.95, "Morning: %u NaN, peak %.3f", m.nans, m.peak);
    printf("  Morning 32 s: peak %.3f, rms %.1f dBFS, %u + %u notes\n", m.peak, rms_db(&m), en_notes[0], en_notes[1]);
}

/* the keys: a chord held is the chord at once; let go, it holds to the cycle's next chord, then the
 * cycle comes back */
static void test_keys(void)
{
    meas_t m = {0};
    int i;
    start(0);
    run(1.0, &m);
    CHECK(!en_yours, "the cycle's chord");
    en_hold(0x091, 0, 3);                        /* C major */
    run(0.1, &m);
    CHECK(en_yours && en_chord_mask == 0x091 && en_chord_bass == 0, "held: %03x over %d", en_chord_mask, en_chord_bass);
    en_hold(0x224, 2, 1);                        /* one key, D: A minor's iv, D minor */
    run(0.1, &m);
    CHECK(en_chord_mask == 0x224 && en_chord_bass == 2, "one key: %03x over %d", en_chord_mask, en_chord_bass);
    en_hold(0, 0, 0);
    run(0.05, &m);
    CHECK(en_yours, "let go: it holds to the chord's end");
    for (i = 0; i < 40 && en_yours; i++)         /* a bar at 100 BPM: 2.4 s at most */
        run(0.1, &m);
    CHECK(!en_yours && i <= 26, "the cycle again after %.1f s", (double)i * 0.1);
}

/* the additive process: Process at 100, the figures grow from 2 notes to the whole and back */
static void test_process(void)
{
    meas_t m = {0};
    int seen[EN_FIG_MAX + 1] = {0}, i, grew = 0, shrank = 0, last = -1;
    start(6);                                    /* Counting: Process 100 */
    for (i = 0; i < 1200; i++) {
        int l = en_fig_len[PT_UPPER];
        run(0.05, &m);
        if (l <= EN_FIG_MAX)
            seen[l] = 1;
        if (last >= 0 && l > last)
            grew = 1;
        if (last >= 0 && l < last)
            shrank = 1;
        last = l;
    }
    CHECK(seen[2] && seen[en_fig_full[PT_UPPER]] && grew && shrank, "the process: 2 %d, full %d %d, grew %d, shrank %d",
          seen[2], en_fig_full[PT_UPPER], seen[en_fig_full[PT_UPPER]], grew, shrank);
}

/* the same piece starts the same way each time; the form moves on (sections) over minutes */
static void test_repeatable(void)
{
    meas_t a = {0}, b = {0};
    start(3);
    run(20.0, &a);
    start(3);
    run(20.0, &b);
    CHECK(a.sum == b.sum, "Machine twice: different samples");
    start(1);
    run(300.0, &a);
    CHECK(en_section >= 3, "Glacier: %u sections in 5 min", en_section);
}

/* a piece from power-on with one part alone (the others at 0, Build by hand) */
static void start_solo(int piece, int part)
{
    static const uint8_t LEVEL[EN_NPART] = {P_UP_LEVEL, P_LO_LEVEL, P_BS_LEVEL, P_LD_LEVEL, P_CH_LEVEL};
    int16_t par[P_NPARAMS];
    int p;
    en_init();
    en_piece(piece, par);
    for (p = 0; p < EN_NPART; p++)
        if (p != part)
            par[LEVEL[p]] = 0;
    par[P_BUILD] = 0;
    for (p = 0; p < P_NPARAMS; p++)
        en_set(p, par[p]);
    en_regrow();
    en_play(1);
    en_render(blk, 0, 2048);
}

/* every part of every piece heard alone (the others at 0), and none heard at 0 */
static void test_parts(void)
{
    static const uint8_t LEVEL[EN_NPART] = {P_UP_LEVEL, P_LO_LEVEL, P_BS_LEVEL, P_LD_LEVEL, P_CH_LEVEL};
    int piece, p, q;
    for (piece = 0; piece < EN_NPIECES; piece++) {
        int16_t par[P_NPARAMS];
        en_piece(piece, par);
        for (p = 0; p < EN_NPART; p++) {
            meas_t m = {0};
            if (!par[LEVEL[p]])
                continue;
            start_solo(piece, p);
            run(24.0, &m);
            CHECK(rms_db(&m) > -50.0 && en_notes[p] > 0, "%s's %s alone: %.1f dBFS, %u notes", en_piece_name(piece),
                  EN_PART_NAME[p], rms_db(&m), en_notes[p]);
            for (q = 0; q < EN_NPART; q++)
                CHECK(q == p || en_notes[q] == 0, "%s: %s played at 0", en_piece_name(piece), EN_PART_NAME[q]);
        }
    }
}

/* Mine: a learned cycle, relative to the root, becomes the cycle */
static void test_mine(void)
{
    static const uint16_t MASK[3] = {0x091, 0x221, 0x884};   /* C F G (relative: I IV V) */
    static const uint8_t BASS[3] = {0, 5, 7};
    meas_t m = {0};
    start(0);                                    /* A minor's piece: Mine moves to A */
    en_mine(3, MASK, BASS);
    en_set(P_CYCLE, EN_NCYCLES);
    run(0.5, &m);
    CHECK(en_cyc_n == 3, "Mine: %d chords", en_cyc_n);
    CHECK(en_cyc_mask[0] == 0x212 && en_cyc_bass[0] == 9, "Mine's first, A major: %03x %d", en_cyc_mask[0], en_cyc_bass[0]);
    en_mine(0, MASK, BASS);                      /* none: the first cycle stands in */
    run(0.1, &m);
    CHECK(en_cyc_n == 4, "no Mine: the library's first, %d", en_cyc_n);
}

/* the guard: a block over 90% brings it on; 3 s under 65% take it off */
static void test_guard(void)
{
    int i;
    meas_t m = {0};
    start(3);
    en_load(95, 256);
    CHECK(en_guarding, "the guard on");
    run(2.0, &m);
    for (i = 0; i < 200; i++)
        en_load(40, 256);
    CHECK(en_guarding, "the guard waits 3 s");
    for (i = 0; i < 400; i++)
        en_load(40, 256);
    CHECK(!en_guarding, "the guard off");
    CHECK(m.nans == 0, "guarded: no NaN");
}

/* ten minutes of each busy piece: no NaN, no clip, a sound */
static void test_long(void)
{
    static const int P[5] = {3, 4, 1, 9, 14};    /* Machine, Skyline, Glacier, Breath, Gears */
    int i;
    for (i = 0; i < 5; i++) {
        meas_t m = {0};
        start(P[i]);
        run(600.0, &m);
        CHECK(m.nans == 0 && m.peak < 0.99 && rms_db(&m) > -40.0, "%s 10 min: %u NaN, peak %.3f, rms %.1f",
              en_piece_name(P[i]), m.nans, m.peak, rms_db(&m));
        printf("  %s 10 min: peak %.3f, rms %.1f dBFS, %u sections, voices now %u\n", en_piece_name(P[i]), m.peak,
               rms_db(&m), en_section, en_voices);
    }
}

/* rolled ideas: cycles from the tonic, 2..6 chords of 3 or 4 tones, none twice running, never home at
 * the end; figures 3..12 steps (the seed's last digit), within the seven lowest tones, never one note */
static void test_rolled(void)
{
    int seed, k, bad = 0;
    for (seed = 1; seed < 400; seed++) {
        uint16_t m[EN_CYC_MAX];
        uint8_t b[EN_CYC_MAX];
        int minor = seed & 1, n = en_cycle_roll((uint32_t)seed, minor, m, b), i, c;
        if (n < 2 || n > 6 || m[0] != (minor ? 0x089 : 0x091) || b[0] != 0 || m[n - 1] == m[0])
            bad++;
        for (k = 0; k < n; k++) {
            for (i = 0, c = 0; i < 12; i++)
                c += (int)(m[k] >> i & 1u);
            if (c < 3 || c > 4 || (k && m[k] == m[k - 1]))
                bad++;
        }
    }
    CHECK(!bad, "rolled cycles: %d faults", bad);
    for (seed = 0; seed < 300; seed++) {
        int8_t s[EN_FIG_MAX];
        int n = en_figure(EN_FIG_ROLLED, seed, s), same = 1, i;
        if (n != 3 + seed % 10)
            bad++;
        for (i = 0; i < n; i++) {
            if (s[i] < 0 || s[i] > 6)
                bad++;
            if (s[i] != s[0])
                same = 0;
        }
        bad += same;
    }
    CHECK(!bad, "rolled figures: %d faults", bad);
}

/* EASY keys: the white keys the key's chords from the leftmost (the tonic) up, the black ones the white
 * key's left of them with their sevenths */
static void test_pads(void)
{
    int b;
    CHECK(en_pad(0, 0, 0, &b) == 0x091 && b == 0, "F3 in C major: C");
    CHECK(en_pad(7, 0, 0, &b) == 0x884 && b == 7, "C4, the fifth white key: G");
    CHECK(en_pad(1, 0, 0, &b) == 0x891 && b == 0, "F#3: C with its seventh, CM7");
    CHECK(en_pad(12, 0, 0, &b) == 0x091 && b == 0, "F4: the tonic again");
    CHECK(en_pad(4, 9, 1, &b) == 0x091 && b == 0, "A3 in A minor: III, C");
    CHECK(en_pad(7, 9, 1, &b) == 0x910 && b == 4, "C4 in A minor: V, E major");
    CHECK(en_pad(8, 9, 1, &b) == 0x914 && b == 4, "C#4 in A minor: V with its seventh, E7 (%03x)", en_pad(8, 9, 1, &b));
}

/* a piece from power-on, Build by hand (every part in) */
static void start_full(int piece)
{
    int16_t par[P_NPARAMS];
    int p;
    en_init();
    en_piece(piece, par);
    par[P_BUILD] = 0;
    for (p = 0; p < P_NPARAMS; p++)
        en_set(p, par[p]);
    en_regrow();
    en_play(1);
    en_render(blk, 0, 2048);
}

/* SONGS: a take with a player's moves at odd moments, played back: the same notes on the same ticks */
static void test_song(void)
{
    static uint32_t take[LOGN][2];
    uint32_t tn = 0, pn = 0, i, same = 1, end;
    meas_t m = {0};
    en_song_t *sg;
    start(1);                                    /* Glacier, live a while first */
    run(3.0, &m);
    lgn = 0;
    logging = 1;
    en_song_cmd(EN_SONG_REC, 1);
    run(2.0, &m);
    en_hold(0x091, 0, 3);                        /* a chord held */
    run(1.37, &m);
    en_set(P_UP_LEVEL, 40);                      /* a knob turned (one command: the last value) */
    en_set(P_UP_LEVEL, 45);
    run(0.53, &m);
    en_hold(0, 0, 0);
    en_part(PT_LEAD, 0);
    en_verb(PT_UPPER, V_GROW);
    run(3.1, &m);
    en_next();
    en_set(P_SEED_UP, 777);                      /* an idea */
    en_set(P_UP_FIGURE, EN_FIG_ROLLED);
    run(6.0, &m);
    en_song_cmd(EN_SONG_STOP, 0);
    run(0.01, &m);
    logging = 0;
    sg = en_song();
    end = sg->end;
    CHECK(en_song_state == SONG_IDLE && sg->n >= 7 && end > 0, "the take: %u commands, %u ticks", sg->n, end);
    for (i = 0; i < lgn; i++)
        if (lg[i][0] < end) {
            take[tn][0] = lg[i][0];
            take[tn][1] = lg[i][1];
            tn++;
        }
    lgn = 0;
    logging = 1;
    en_song_cmd(EN_SONG_PLAY, 0);
    for (i = 0; i < 600 && (i < 2 || en_song_state == SONG_PLAYING); i++)
        run(0.1, &m);
    logging = 0;
    for (i = 0; i < lgn; i++)
        if (lg[i][0] < end) {
            if (pn < tn && (lg[i][0] != take[pn][0] || lg[i][1] != take[pn][1]))
                same = 0;
            pn++;
        }
    CHECK(same && pn == tn && tn > 50, "played back: %u of %u notes the same (%s)", pn, tn, same ? "same" : "different");
    CHECK(!en_playing && en_song_state == SONG_IDLE, "the take's end stops it");
}

/* the models: PHASE drifts ahead, PATTERNS move on, BELLS answer every note of the line */
static void test_models(void)
{
    meas_t m = {0};
    int i, moved = 0;
    float maxphi = 0.0f;
    start_full(8);                               /* Drift */
    for (i = 0; i < 300; i++) {
        run(0.2, &m);
        if (en_phase > maxphi)
            maxphi = en_phase;
    }
    CHECK(maxphi >= 1.0f, "PHASE: the lower %.2f steps ahead at most in a minute", (double)maxphi);
    start_full(12);                              /* Bazaar */
    for (i = 0; i < 600 && !moved; i++) {
        run(0.2, &m);
        moved = en_pat_at[PT_UPPER] > 0 || en_pat_at[PT_LEAD] > 0;
    }
    CHECK(moved, "PATTERNS: the players move on");
    start_full(16);                              /* Vigil */
    run(60.0, &m);
    CHECK(en_notes[PT_UPPER] == en_notes[PT_LEAD] && en_notes[PT_LEAD] > 5, "BELLS: %u bells for %u notes",
          en_notes[PT_UPPER], en_notes[PT_LEAD]);
    CHECK(m.nans == 0 && m.peak < 0.99, "the models: %u NaN, peak %.3f", m.nans, m.peak);
}

/* processes by hand, at the start of a pass; parts by hand, at the next chord */
static void test_hands(void)
{
    meas_t m = {0};
    int i, saw2 = 0;
    start(0);                                    /* Morning */
    run(2.0, &m);
    en_verb(PT_UPPER, V_GROW);
    for (i = 0; i < 100 && !saw2; i++) {
        run(0.1, &m);
        saw2 = en_fig_len[PT_UPPER] == 2 && en_fig_user[PT_UPPER];
    }
    CHECK(saw2, "GROW: the upper from two notes, by hand");
    en_verb(PT_UPPER, V_STILL);
    run(3.0, &m);
    CHECK(!en_fig_user[PT_UPPER] && en_fig_len[PT_UPPER] == en_fig_full[PT_UPPER], "STILL: the figure whole again");
    en_part(PT_UPPER, 0);
    run(0.02, &m);
    CHECK(en_part_on[PT_UPPER], "a part out by hand: not before the chord ends");
    for (i = 0; i < 30 && en_part_on[PT_UPPER]; i++)
        run(0.1, &m);
    CHECK(!en_part_on[PT_UPPER], "out at the next chord");
    en_part(PT_UPPER, 1);
    for (i = 0; i < 30 && !en_part_on[PT_UPPER]; i++)
        run(0.1, &m);
    CHECK(en_part_on[PT_UPPER], "in again");
    start_full(8);                               /* Drift: SHIFT puts the lower a step ahead */
    run(1.0, &m);
    {
        float before = en_phase;
        en_verb(PT_LOWER, V_SHIFT);
        run(3.0, &m);
        CHECK(en_phase >= (float)(int)before + 0.99f, "SHIFT: the phase %.2f -> %.2f (the next whole step)", (double)before,
              (double)en_phase);
    }
}

int main(void)
{
    test_pure();
    test_two_against_three();
    test_keys();
    test_process();
    test_repeatable();
    test_parts();
    test_mine();
    test_guard();
    test_rolled();
    test_pads();
    test_song();
    test_models();
    test_hands();
    test_long();
    printf(fails ? "min: %d FAILED\n" : "min: ok\n", fails);
    return fails != 0;
}
