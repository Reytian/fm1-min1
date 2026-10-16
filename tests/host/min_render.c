/* SPDX-License-Identifier: GPL-3.0-only */
/* The pieces on the host, for listening and measuring. Each piece plays from the top for SECONDS into
 * OUTDIR/<n>-<piece>.wav; printed: its peak and loudness, the notes each part played, the most voices
 * at once, the sections, and what the render cost (the engine alone, 256-frame blocks as the device
 * renders them). With -parts, each part of each piece also plays alone (the others at 0) and its
 * loudness is printed: the balance between the instruments.
 *
 *   min_render [-parts] [SECONDS [OUTDIR [PIECE]]]
 *
 * Built by tests/run_tests.sh's way: cc -O2 -ffp-contract=off -DEN_HOST (see host/build_host.sh). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#ifdef __APPLE__
#include <libproc.h>
#include <unistd.h>
#endif
#include "min.h"

/* instructions retired: on an in-order core the cost follows the instruction count, not a Mac's
 * time. Calibrated against X0X's PERF TEST (its manual: 43%, 83% and 115% of one FM-1 core for its
 * three patterns, which run 1464, 2708 and 3888 instructions a frame in a scalar build on an M4):
 * one instruction a frame is ~0.030% of the FM-1's core */
#define DEV_PCT_PER_INSN 0.030
static uint64_t instr_now(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
#endif
    return 0;
}

void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
    (void)st;
    (void)d1;
    (void)d2;
}

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

static void wav_write(const char *path, const int16_t *s, uint32_t frames)
{
    FILE *f = fopen(path, "wb");
    uint32_t bytes = frames * 4, v;
    uint16_t fmt[2] = {1, 2}, al[2] = {4, 16};
    uint32_t r[2] = {44100, 44100 * 4};
    if (!f) {
        fprintf(stderr, "cannot write %s\n", path);
        return;
    }
    fwrite("RIFF", 1, 4, f);
    v = 36 + bytes;
    fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    v = 16;
    fwrite(&v, 4, 1, f);
    fwrite(fmt, 2, 2, f);
    fwrite(r, 4, 2, f);
    fwrite(al, 2, 2, f);
    fwrite("data", 1, 4, f);
    fwrite(&bytes, 4, 1, f);
    fwrite(s, 4, frames, f);
    fclose(f);
}

typedef struct {
    double peak, ss_l, ss_r, ss_lr, us_sum, us_max;
    uint64_t ins_sum, ins_max;
    uint32_t frames, notes[EN_NPART], voices_max, sections;
} stats_t;

/* piece i from the top, its levels changed by solo (-1: as the recipe), for secs; pcm may be 0 */
static void play(int piece, int solo, int secs, int16_t *pcm, stats_t *st)
{
    static const uint8_t LEVEL[EN_NPART] = {P_UP_LEVEL, P_LO_LEVEL, P_BS_LEVEL, P_LD_LEVEL, P_CH_LEVEL};
    int16_t par[P_NPARAMS];
    int32_t blk[512];
    uint32_t n = (uint32_t)secs * 44100u / 256u, b, k, notes0[EN_NPART], sec0;
    int p;
    memset(st, 0, sizeof *st);
    en_init();
    en_seed(1000u + (uint32_t)piece);
    en_piece(piece, par);
    if (solo >= 0) {
        for (p = 0; p < EN_NPART; p++)
            if (p != solo)
                par[LEVEL[p]] = 0;
        par[P_BUILD] = 0;
    }
    for (p = 0; p < P_NPARAMS; p++)
        en_set(p, par[p]);
    en_regrow();
    en_play(1);
    en_render(blk, 0, 2048);                     /* the commands */
    for (p = 0; p < EN_NPART; p++)
        notes0[p] = en_notes[p];
    sec0 = en_section;
    for (b = 0; b < n; b++) {
        uint64_t i0 = instr_now(), di;
        double t0 = now(), us;
        en_render(blk, 256, 2048);
        us = (now() - t0) * 1e6;
        di = instr_now() - i0;
        st->ins_sum += di;
        if (di > st->ins_max)
            st->ins_max = di;
        st->us_sum += us;
        if (us > st->us_max)
            st->us_max = us;
        if (en_voices > st->voices_max)
            st->voices_max = en_voices;
        for (k = 0; k < 256; k++) {
            double l = (double)blk[2 * k] / 8388608.0, r = (double)blk[2 * k + 1] / 8388608.0;
            if (fabs(l) > st->peak)
                st->peak = fabs(l);
            if (fabs(r) > st->peak)
                st->peak = fabs(r);
            st->ss_l += l * l;
            st->ss_r += r * r;
            st->ss_lr += l * r;
            if (pcm) {
                pcm[2 * (b * 256 + k)] = (int16_t)(l * 32767.0);
                pcm[2 * (b * 256 + k) + 1] = (int16_t)(r * 32767.0);
            }
        }
    }
    st->frames = n * 256;
    for (p = 0; p < EN_NPART; p++)
        st->notes[p] = en_notes[p] - notes0[p];
    st->sections = en_section - sec0;
}

static double db(double x) { return 20.0 * log10(x > 1e-9 ? x : 1e-9); }

int main(int argc, char **argv)
{
    int parts = 0, secs, only, i, p;
    const char *dir;
    int16_t *pcm;
    if (argc > 1 && !strcmp(argv[1], "-parts")) {
        parts = 1;
        argv++;
        argc--;
    }
    secs = argc > 1 ? atoi(argv[1]) : 60;
    dir = argc > 2 ? argv[2] : "build/render";
    only = argc > 3 ? atoi(argv[3]) : -1;
    pcm = malloc((size_t)secs * 44100u * 4u + 4096u);
    printf("%-10s %6s %6s %5s  %5s %5s %5s %5s %5s  %3s %3s  %6s %6s  %s\n", "piece", "peak", "rms", "corr", "upper", "lower",
           "bass", "lead", "chord", "vox", "sec", "us avg", "us max", "FM-1 load (insn/frame): avg, worst block");
    for (i = 0; i < EN_NPIECES; i++) {
        stats_t st;
        char path[512];
        double rms;
        if (only >= 0 && i != only)
            continue;
        play(i, -1, secs, pcm, &st);
        rms = sqrt((st.ss_l + st.ss_r) / (2.0 * st.frames));
        printf("%-10s %6.1f %6.1f %5.2f  %5u %5u %5u %5u %5u  %3u %3u  %6.1f %6.1f  %3.0f%%, %3.0f%% (%.0f, %.0f)\n",
               en_piece_name(i), db(st.peak), db(rms), st.ss_lr / sqrt(st.ss_l * st.ss_r + 1e-30), st.notes[0], st.notes[1],
               st.notes[2], st.notes[3], st.notes[4], st.voices_max, st.sections, st.us_sum / (st.frames / 256), st.us_max,
               (double)st.ins_sum / st.frames * DEV_PCT_PER_INSN, (double)st.ins_max / 256.0 * DEV_PCT_PER_INSN,
               (double)st.ins_sum / st.frames, (double)st.ins_max / 256.0);
        snprintf(path, sizeof path, "%s/%d-%s.wav", dir, i + 1, en_piece_name(i));
        wav_write(path, pcm, st.frames);
        if (parts) {
            int16_t par[P_NPARAMS];
            static const uint8_t LEVEL[EN_NPART] = {P_UP_LEVEL, P_LO_LEVEL, P_BS_LEVEL, P_LD_LEVEL, P_CH_LEVEL};
            en_piece(i, par);
            printf("    alone:");
            for (p = 0; p < EN_NPART; p++) {
                stats_t ps;
                if (!par[LEVEL[p]]) {
                    printf("  %s -", EN_PART_NAME[p]);
                    continue;
                }
                play(i, p, secs < 30 ? secs : 30, 0, &ps);
                printf("  %s %.1f", EN_PART_NAME[p], db(sqrt((ps.ss_l + ps.ss_r) / (2.0 * ps.frames))));
            }
            printf("  (dBFS rms)\n");
        }
    }
    free(pcm);
    return 0;
}
