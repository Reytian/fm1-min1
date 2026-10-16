/* SPDX-License-Identifier: GPL-3.0-only */
/* The instrument (min.h). One compilation unit, -O2. */
#include "min.h"
#include "fastmath.h"

#ifdef EN_HOST
#define EN_POOL
#else
#define EN_POOL __attribute__((section(".pool")))
#endif
#define BARRIER() __asm__ volatile("" ::: "memory")

const char *const EN_NOTE_NAME[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
const char *const EN_INST_NAME[EN_NINST] = {"Piano", "Organ", "Flute", "Strings", "Marimba", "Sax", "Horn", "Synth"};
const char *const EN_PART_NAME[EN_NPART] = {"Upper", "Lower", "Bass", "Lead", "Chords"};
const char *const EN_MODEL_NAME[EN_NMODELS] = {"Arpeggio", "Phase", "Patterns", "Bells"};

static const en_param_t PARAMS[P_NPARAMS] = {
    {"Evolve", 0, 100, 30},   {"Build", 0, 100, 20},     {"Process", 0, 100, 20},   {"Chord", 0, 3, 1},
    {"Upper", 0, 100, 75},    {"Up sound", 0, 4, 0},     {"Up pulse", 0, 3, 1},     {"Up figure", 0, EN_NFIG, 2},
    {"Lower", 0, 100, 70},    {"Lo sound", 0, 3, 0},     {"Lo pulse", 0, 3, 0},     {"Lo figure", 0, EN_NFIG, 3},
    {"Bass", 0, 100, 0},      {"Bass sound", 0, 3, 3},   {"Bass style", 0, 3, 0},   {"Bass oct", -1, 1, 0},
    {"Lead", 0, 100, 0},      {"Lead sound", 0, 4, 0},   {"Activity", 0, 100, 30},  {"Harmony", 0, 2, 0},
    {"Chords", 0, 100, 0},    {"Ch sound", 0, 2, 0},     {"Rhythm", 0, 5, 0},       {"Room", 0, 100, 50},
    {"Tempo", 40, 200, 100},  {"Root", 0, 11, 9},        {"Keys", 0, 1, 0},         {"Hold", 0, 1, 0},
    {"Model", 0, EN_NMODELS - 1, 0}, {"MIDI out", 0, 1, 1}, {"Cycle", 0, EN_CYC_ROLLED, 5}, {"Up oct", -1, 1, 0},
    {"Cycle idea", 0, EN_SEED_MAX, 1}, {"Upper idea", 0, EN_SEED_MAX, 1}, {"Lower idea", 0, EN_SEED_MAX, 2},
    {"Line idea", 0, EN_SEED_MAX, 3},
};
const en_param_t *en_param_info(int i) { return (i >= 0 && i < P_NPARAMS) ? &PARAMS[i] : &PARAMS[0]; }

/* each part's sounds (its Sound value picks one) */
static const uint8_t NSOUNDS[EN_NPART] = {5, 4, 4, 5, 3};
static const uint8_t SOUNDS[EN_NPART][5] = {
    {I_PIANO, I_ORGAN, I_FLUTE, I_STRINGS, I_MARIMBA},
    {I_PIANO, I_ORGAN, I_STRINGS, I_MARIMBA, I_MARIMBA},
    {I_SYNBASS, I_ORGAN, I_STRINGS, I_PIANO, I_PIANO},
    {I_SAX, I_FLUTE, I_HORN, I_STRINGS, I_PIANO},
    {I_STRINGS, I_ORGAN, I_HORN, I_HORN, I_HORN},
};
static const uint8_t LEVEL_PAR[EN_NPART] = {P_UP_LEVEL, P_LO_LEVEL, P_BS_LEVEL, P_LD_LEVEL, P_CH_LEVEL};
static const uint8_t SOUND_PAR[EN_NPART] = {P_UP_SOUND, P_LO_SOUND, P_BS_SOUND, P_LD_SOUND, P_CH_SOUND};
static const uint8_t PULSE_DIV[4] = {2, 3, 4, 6};
static const char *const PULSE_NAME[4] = {"8ths", "3lets", "16ths", "6lets"};
static const uint8_t CHORD_BEATS[4] = {2, 4, 8, 16};
static const char *const CHORD_NAME[4] = {"2 beats", "1 bar", "2 bars", "4 bars"};
static const char *const STYLE_NAME[4] = {"Long", "Pulse", "Octaves", "Root-5"};
static const char *const HARM_NAME[3] = {"Alone", "Pair", "Octave"};
static const char *const RHYTHM_NAME[6] = {"Hold", "Swell", "8ths", "Beats", "Breath", "Pulse"};
static const char *const BELL_NAME[4] = {"Above", "Below", "Both", "Above 2"};

/* -------------------------------------------------------------- state --- */
static int16_t par[P_NPARAMS];

/* ------------------------------------------------------------ harmony --- */
/* chord qualities as interval sets (bit i: i semitones above the root) */
enum { Q_MAJ, Q_MIN, Q_DIM, Q_AUG, Q_SUS2, Q_SUS4, Q_MAJ7, Q_MIN7, Q_DOM7, Q_ADD9, Q_MADD9, NQ };
static const uint16_t QUAL[NQ] = {0x091, 0x089, 0x049, 0x111, 0x085, 0x0A1, 0x891, 0x489, 0x491, 0x095, 0x08D};

/* the cycles: plain progressions of the common practice, none taken from a piece, each with a name
 * for those who don't read numerals. A chord is its root (semitones above the key's root), its quality
 * and its bass (-1: its root) */
typedef struct {
    int8_t off;
    uint8_t q;
    int8_t bass;
} tch_t;
typedef struct {
    const char *nick, *name;
    uint8_t minor, n;
    tch_t c[EN_CYC_MAX];
} tcyc_t;
#define CH(o, q) {o, q, -1}
#define CHB(o, q, b) {o, q, b}
static const tcyc_t CYCLES[EN_NCYCLES] = {
    {"Cadence", "i VI iv V", 1, 4, {CH(0, Q_MIN), CH(8, Q_MAJ), CH(5, Q_MIN), CH(7, Q_MAJ)}},
    {"Epic", "i VI III VII", 1, 4, {CH(0, Q_MIN), CH(8, Q_MAJ), CH(3, Q_MAJ), CH(10, Q_MAJ)}},
    {"Lament", "i iv VI V", 1, 4, {CH(0, Q_MIN), CH(5, Q_MIN), CH(8, Q_MAJ), CH(7, Q_MAJ)}},
    {"Descent", "i VII VI V", 1, 4, {CH(0, Q_MIN), CH(10, Q_MAJ), CH(8, Q_MAJ), CH(7, Q_MAJ)}},
    {"Tide", "i VI", 1, 2, {CH(0, Q_MIN), CH(8, Q_MAJ)}},
    {"Slope", "i i/7 VI V", 1, 4, {CH(0, Q_MIN), CHB(0, Q_MIN, 10), CH(8, Q_MAJ), CH(7, Q_MAJ)}},
    {"Journey", "i III VI iv V", 1, 5, {CH(0, Q_MIN), CH(3, Q_MAJ), CH(8, Q_MAJ), CH(5, Q_MIN), CH(7, Q_MAJ)}},
    {"Shadow", "i VI bII V", 1, 4, {CH(0, Q_MIN), CH(8, Q_MAJ), CH(1, Q_MAJ), CH(7, Q_MAJ)}},
    {"Classic", "I vi IV V", 0, 4, {CH(0, Q_MAJ), CH(9, Q_MIN), CH(5, Q_MAJ), CH(7, Q_MAJ)}},
    {"Hymn", "I IV I V", 0, 4, {CH(0, Q_MAJ), CH(5, Q_MAJ), CH(0, Q_MAJ), CH(7, Q_MAJ)}},
    {"Bloom", "I iii vi IV", 0, 4, {CH(0, Q_MAJ), CH(4, Q_MIN), CH(9, Q_MIN), CH(5, Q_MAJ)}},
    {"Steps", "I V/3 vi IV", 0, 4, {CH(0, Q_MAJ), CHB(7, Q_MAJ, 11), CH(9, Q_MIN), CH(5, Q_MAJ)}},
    {"Lift", "IV V iii vi", 0, 4, {CH(5, Q_MAJ), CH(7, Q_MAJ), CH(4, Q_MIN), CH(9, Q_MIN)}},
    {"Float", "I II", 0, 2, {CH(0, Q_MAJ), CH(2, Q_MAJ)}},
    {"Folk", "I bVII IV I", 0, 4, {CH(0, Q_MAJ), CH(10, Q_MAJ), CH(5, Q_MAJ), CH(0, Q_MAJ)}},
    {"Hero", "I bVI bVII I", 0, 4, {CH(0, Q_MAJ), CH(8, Q_MAJ), CH(10, Q_MAJ), CH(0, Q_MAJ)}},
    {"Still", "i7", 1, 1, {CH(0, Q_MIN7)}},
    {"Glow", "Iadd9", 0, 1, {CH(0, Q_ADD9)}},
};
#undef CH
#undef CHB

/* the triads of a key on each of its twelve notes (major, minor): a key pressed alone */
static const uint8_t TRI_Q[2][12] = {
    {Q_MAJ, Q_MAJ, Q_MIN, Q_MAJ, Q_MIN, Q_MAJ, Q_MAJ, Q_MAJ, Q_MAJ, Q_MIN, Q_MAJ, Q_DIM},
    {Q_MIN, Q_MAJ, Q_DIM, Q_MAJ, Q_MAJ, Q_MIN, Q_MAJ, Q_MAJ, Q_MAJ, Q_MAJ, Q_MAJ, Q_DIM},
};
static const uint8_t SCALE[2][7] = {{0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 8, 10}};   /* major, natural minor */

/* the figures: steps through the chord's tones in the part's register, from its lowest (0) up; past
 * the tones of one octave the steps go on an octave up. Then "Rolled": one made from a seed */
static const struct {
    const char *name;
    uint8_t n;
    int8_t s[EN_FIG_MAX];
} FIGS[EN_NFIG] = {
    {"Up 3", 3, {0, 1, 2}},
    {"Up 4", 4, {0, 1, 2, 3}},
    {"Up-down", 6, {0, 1, 2, 3, 2, 1}},
    {"Broken", 4, {0, 2, 1, 3}},
    {"Alberti", 4, {0, 2, 1, 2}},
    {"Rocking", 2, {1, 0}},
    {"Wave", 8, {0, 1, 2, 3, 4, 3, 2, 1}},
    {"Spread", 6, {0, 2, 4, 1, 3, 5}},
};

/* the pieces: twenty recipes, eight a model for ARPEGGIO and four for each of the others (the FM-1's
 * PRESETS). Keys, Hold and MIDI out are the player's */
/*  Evo Bld Pro Chd   Up Snd Pul Fig   Lo Snd Pul Fig   Bs Snd Sty Oct   Ld Snd Act Har   Ch Snd Rhy Room
 *  Tempo Rt Ky Hd   Mdl MI Cyc Uo   seeds: cycle, upper, lower, line */
static const struct {
    const char *name;
    int16_t v[P_NPARAMS];
} PIECES[EN_NPIECES] = {
    {"Morning", {30, 20, 20, 1, 75, 0, 1, 2, 70, 0, 0, 3, 0, 3, 0, 0, 0, 0, 30, 0, 0, 0, 0, 50,
                 100, 9, 0, 0, 0, 1, 5, 0, 11, 12, 13, 14}},
    {"Glacier", {50, 60, 50, 1, 60, 1, 2, 6, 55, 1, 1, 1, 55, 0, 0, 0, 50, 2, 25, 1, 45, 0, 1, 40,
                 132, 2, 0, 0, 0, 1, 1, 0, 21, 22, 23, 24}},
    {"Lagoon", {35, 50, 15, 2, 55, 2, 1, 2, 50, 2, 0, 5, 40, 2, 0, 0, 45, 1, 30, 0, 50, 0, 0, 60,
                84, 7, 0, 0, 0, 1, 10, 0, 31, 32, 33, 34}},
    {"Machine", {55, 45, 70, 0, 65, 1, 1, 2, 60, 1, 0, 4, 60, 0, 2, 0, 45, 0, 55, 1, 35, 1, 2, 25,
                 152, 0, 0, 0, 0, 1, 2, 0, 41, 42, 43, 44}},
    {"Skyline", {30, 40, 10, 2, 45, 3, 1, 2, 40, 2, 0, 3, 40, 2, 0, 0, 70, 0, 25, 1, 35, 0, 0, 55,
                 92, 5, 0, 0, 0, 1, 3, 0, 51, 52, 53, 54}},
    {"Evening", {35, 55, 20, 1, 70, 0, 1, 0, 65, 0, 0, 4, 35, 2, 0, 0, 40, 2, 20, 0, 40, 0, 0, 55,
                 96, 3, 0, 0, 0, 1, 11, 0, 61, 62, 63, 64}},
    {"Counting", {25, 30, 100, 2, 65, 1, 2, 6, 55, 1, 0, 1, 45, 1, 1, 0, 0, 0, 30, 0, 30, 1, 0, 30,
                  120, 9, 0, 0, 0, 1, 4, 0, 71, 72, 73, 74}},
    {"Prism", {45, 50, 45, 1, 60, 4, 2, 3, 60, 0, 1, 2, 40, 0, 3, 0, 35, 1, 35, 0, 0, 0, 0, 40,
               112, 2, 0, 0, 0, 1, 13, 0, 81, 82, 83, 84}},
    {"Drift", {40, 0, 35, 3, 70, 0, 2, 8, 70, 0, 2, 8, 0, 0, 1, 0, 0, 1, 20, 0, 0, 1, 4, 35,
               96, 4, 0, 0, 1, 1, 16, 0, 91, 319, 319, 93}},
    {"Breath", {35, 40, 25, 3, 65, 4, 2, 8, 65, 3, 2, 8, 45, 0, 1, 0, 0, 1, 20, 0, 45, 1, 4, 40,
                104, 9, 0, 0, 1, 1, 10, 0, 101, 1018, 1018, 103}},
    {"Weave", {35, 30, 30, 3, 65, 0, 2, 8, 60, 3, 2, 8, 35, 2, 0, 0, 40, 1, 20, 0, 0, 0, 0, 45,
               88, 2, 0, 0, 1, 1, 4, 0, 111, 1117, 2229, 114}},
    {"Tilt", {30, 20, 45, 2, 60, 1, 0, 8, 60, 1, 0, 8, 40, 1, 1, 0, 0, 1, 20, 0, 0, 1, 0, 30,
              132, 7, 0, 0, 1, 1, 13, 0, 121, 1216, 1216, 124}},
    {"Bazaar", {50, 30, 40, 3, 60, 4, 0, 0, 60, 0, 0, 2, 0, 0, 1, 0, 50, 1, 30, 0, 35, 1, 5, 40,
                120, 0, 0, 0, 2, 1, 17, 0, 131, 132, 133, 1313}},
    {"Field", {40, 40, 30, 3, 55, 2, 0, 0, 50, 2, 0, 1, 0, 0, 1, 0, 45, 0, 30, 0, 35, 1, 5, 55,
               96, 2, 0, 0, 2, 1, 16, 0, 141, 142, 143, 1414}},
    {"Gears", {45, 30, 60, 3, 55, 1, 2, 0, 55, 1, 0, 3, 45, 0, 1, 0, 40, 0, 30, 0, 30, 1, 5, 25,
               144, 7, 0, 0, 2, 1, 4, 0, 151, 152, 153, 1515}},
    {"Murmur", {40, 30, 35, 3, 60, 0, 0, 0, 60, 3, 0, 1, 35, 2, 0, 0, 45, 2, 30, 0, 0, 0, 0, 50,
                108, 5, 0, 0, 2, 1, 17, 0, 161, 162, 163, 1616}},
    {"Vigil", {20, 30, 50, 3, 55, 0, 0, 0, 45, 0, 0, 0, 30, 2, 0, 0, 65, 3, 30, 0, 25, 0, 0, 70,
               56, 9, 0, 0, 3, 1, 16, 0, 171, 172, 173, 1717}},
    {"Snowfall", {20, 0, 40, 3, 55, 4, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 70, 4, 40, 0, 0, 0, 0, 65,
                  64, 4, 0, 0, 3, 1, 17, 0, 181, 182, 183, 1818}},
    {"Lantern", {30, 40, 45, 3, 55, 0, 0, 2, 40, 2, 0, 1, 30, 2, 0, 0, 60, 1, 35, 0, 30, 0, 1, 65,
                 60, 2, 0, 0, 3, 1, 4, 0, 191, 192, 193, 1919}},
    {"Chapel", {25, 40, 55, 3, 50, 1, 0, 0, 45, 1, 0, 0, 35, 1, 0, 0, 60, 2, 30, 0, 25, 1, 0, 60,
                52, 3, 0, 0, 3, 1, 9, 0, 201, 202, 203, 2020}},
};
const char *en_piece_name(int i) { return PIECES[(unsigned)i < EN_NPIECES ? i : 0].name; }
void en_piece(int i, int16_t *p)
{
    int k;
    i = (unsigned)i < EN_NPIECES ? i : 0;
    for (k = 0; k < P_NPARAMS; k++)
        p[k] = PIECES[i].v[k];
}

/* ------------------------------------------------------- the pure parts --- */
static uint16_t rot12(uint32_t m, int r)        /* pitch classes moved up r semitones */
{
    r = (r % 12 + 12) % 12;
    m &= 0xFFFu;
    return (uint16_t)(((m << r) | (m >> (12 - r))) & 0xFFFu);
}

/* a small dice of its own for the pure generators (xorshift32 from a seed) */
static uint32_t lr_next(uint32_t *r)
{
    uint32_t x = *r;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *r = x;
}
static uint32_t lr_seed(uint32_t seed) { return seed * 2654435761u + 0x6D2B79F5u; }
static float lr_float(uint32_t *r) { return (float)(lr_next(r) >> 8) * (1.0f / 16777216.0f); }

/* a ROLLED cycle: from the tonic, 2 to 6 chords of the key (now and then one borrowed), each the
 * likelier by a fourth, a fifth or a third from the last, none twice running; the last never the
 * tonic, so it leads home. Now and then a chord stands on its third, when that walks the bass by step */
int en_cycle_roll(uint32_t seed, int minor, uint16_t *mask, uint8_t *bass)
{
    static const int8_t R[2][8] = {{0, 2, 4, 5, 7, 9, 10, 8}, {0, 3, 5, 7, 8, 10, 1, 7}};
    static const uint8_t Q[2][8] = {{Q_MAJ, Q_MIN, Q_MIN, Q_MAJ, Q_MAJ, Q_MIN, Q_MAJ, Q_MAJ},
                                    {Q_MIN, Q_MAJ, Q_MIN, Q_MAJ, Q_MAJ, Q_MAJ, Q_MAJ, Q_MIN}};
    static const float W[2][8] = {{0.0f, 1.0f, 0.8f, 1.6f, 1.6f, 1.5f, 0.35f, 0.25f},
                                  {0.0f, 1.2f, 1.5f, 1.5f, 1.7f, 1.2f, 0.25f, 0.3f}};
    static const uint8_t LENS[8] = {2, 3, 4, 4, 4, 5, 5, 6};
    uint32_t r = lr_seed(seed);
    int f = minor != 0, n = LENS[lr_next(&r) % 8u], k, i, prev = 0, used = 1, last_bass = 0;
    mask[0] = (uint16_t)(QUAL[Q[f][0]] | 1u);
    bass[0] = 0;
    for (k = 1; k < n; k++) {
        float w[8], sum = 0.0f, x;
        for (i = 0; i < 8; i++) {
            int d = ((R[f][i] - R[f][prev]) % 12 + 12) % 12;
            float m = d == 5 || d == 7 ? 1.4f : d == 3 || d == 4 || d == 8 || d == 9 ? 1.3f : d == 2 || d == 10 ? 0.9f
                      : d == 6 ? 0.2f : 0.5f;
            w[i] = (i == 0 ? (k == n - 1 ? 0.0f : 0.3f) : W[f][i]) * m;
            if (i == prev)
                w[i] = 0.0f;
            if ((used >> i & 1) && n <= 4)
                w[i] *= 0.2f;
            if (k == n - 1 && (R[f][i] == 7 || R[f][i] == 5 || R[f][i] == 10 || R[f][i] == 8))
                w[i] *= 1.5f;                    /* last: a chord that leads home */
            sum += w[i];
        }
        x = lr_float(&r) * sum;
        for (i = 0; i < 7 && x >= w[i]; i++)
            x -= w[i];
        if (w[i] <= 0.0f)                        /* (the rounding's last resort) */
            for (i = 7; i > 0 && w[i] <= 0.0f; i--)
                ;
        prev = i;
        used |= 1 << i;
        {
            int root = R[f][i], third = (root + ((QUAL[Q[f][i]] >> 3 & 1u) ? 3 : 4)) % 12, b = root, step;
            step = ((third - last_bass) % 12 + 12) % 12;
            if ((step <= 2 || step >= 10) && lr_float(&r) < 0.3f)
                b = third;                       /* on its third: the bass walks by step */
            mask[k] = (uint16_t)(rot12(QUAL[Q[f][i]], root) | 1u << b);
            bass[k] = (uint8_t)b;
            last_bass = b;
        }
    }
    return n;
}

static uint16_t mine_mask[EN_CYC_MAX];          /* the learned cycle, relative to the root (the render's) */
static uint8_t mine_bass[EN_CYC_MAX];
static int mine_n, mine_k;

int en_cycle_get(int c, int seed, int k, uint16_t *mask, uint8_t *bass)
{
    if (c == EN_CYC_ROLLED) {
        uint16_t m[EN_CYC_MAX];
        uint8_t b[EN_CYC_MAX];
        int n = en_cycle_roll((uint32_t)seed, seed & 1, m, b);
        if (k >= 0 && k < n) {
            *mask = m[k];
            *bass = b[k];
        }
        return n;
    }
    if (c >= EN_NCYCLES) {
        if (k >= 0 && k < mine_n) {
            *mask = (uint16_t)(mine_mask[k] | 1u << mine_bass[k]);
            *bass = mine_bass[k];
        }
        return mine_n;
    }
    if (c < 0)
        c = 0;
    if (k >= 0 && k < CYCLES[c].n) {
        const tch_t *t = &CYCLES[c].c[k];
        *bass = (uint8_t)(t->bass >= 0 ? t->bass : t->off);
        *mask = (uint16_t)(rot12(QUAL[t->q], t->off) | 1u << *bass);
    }
    return CYCLES[c].n;
}

const char *en_cycle_name(int c) { return c == EN_CYC_ROLLED ? "Rolled" : c >= EN_NCYCLES ? "Mine" : CYCLES[c < 0 ? 0 : c].nick; }
const char *en_cycle_numerals(int c) { return c >= EN_NCYCLES ? "" : CYCLES[c < 0 ? 0 : c].name; }

int en_cycle_minor(int c, int seed)
{
    if (c == EN_CYC_ROLLED)                     /* a rolled cycle's key: the seed's last bit */
        return seed & 1;
    if (c >= EN_NCYCLES) {                      /* the first chord's third, above its bass */
        uint16_t m = mine_n ? rot12(mine_mask[0], -(int)mine_bass[0]) : 0;
        return (m >> 3 & 1u) && !(m >> 4 & 1u);
    }
    return CYCLES[c < 0 ? 0 : c].minor;
}

uint16_t en_triad(int pc, int root, int minor)
{
    int deg = ((pc - root) % 12 + 12) % 12;
    return rot12(QUAL[TRI_Q[minor != 0][deg]], pc);
}

uint16_t en_pad(int key, int root, int minor, int *bass)
{
    static const uint8_t WHITE[12] = {1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1};   /* by class: white (1) or black */
    int f = minor != 0, w = 0, k, d, pc;
    uint16_t m;
    if (key < 0)
        key = 0;
    if (key > 26)
        key = 26;
    for (k = 0; k < key; k++)                    /* the white keys before it: key 0 is F3, white */
        w += WHITE[(5 + k) % 12];
    if (!WHITE[(5 + key) % 12])
        w--;                                     /* a black key: the white key left of it */
    d = w % 7;
    pc = (root + SCALE[f][d]) % 12;
    m = rot12(QUAL[TRI_Q[f][SCALE[f][d]]], pc);
    if (!WHITE[(5 + key) % 12])                  /* black: its seventh in the scale too */
        m |= (uint16_t)(1u << ((root + SCALE[f][(d + 6) % 7]) % 12));
    *bass = pc;
    return m;
}

static const char *const NAME_SHARP[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const NAME_FLAT[12] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
static const char *spelled[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};

void en_spell(int root, int minor)
{
    int r = ((root + (minor ? 3 : 0)) % 12 + 12) % 12, i;     /* the major key with the same signature */
    uint16_t sc = rot12(0xAB5u, r);                          /* its scale */
    int sharps = r == 7 || r == 2 || r == 9 || r == 4 || r == 11 || r == 6;
    for (i = 0; i < 12; i++)
        spelled[i] = (sc >> i & 1u) && sharps ? NAME_SHARP[i] : NAME_FLAT[i];
    if (minor)
        spelled[((root + 11) % 12 + 12) % 12] = NAME_SHARP[((root + 11) % 12 + 12) % 12];   /* the leading tone */
}
const char *en_note_name(int pc) { return spelled[(pc % 12 + 12) % 12]; }

static char *put(char *b, const char *s)
{
    while (*s)
        *b++ = *s++;
    *b = 0;
    return b;
}

static char *itoa_s(int v, char *b)
{
    char t[12];
    int n = 0;
    unsigned u = (unsigned)(v < 0 ? -v : v);
    if (v < 0)
        *b++ = '-';
    do {
        t[n++] = (char)('0' + u % 10u);
        u /= 10u;
    } while (u);
    while (n)
        *b++ = t[--n];
    *b = 0;
    return b;
}

const char *en_chord_name(uint32_t mask, int bass, char *buf)
{
    static const struct {
        uint16_t iv;
        const char *q;
    } NAMES[] = {
        {0x091, ""},    {0x089, "m"},     {0x891, "M7"},    {0x489, "m7"},    {0x491, "7"},  {0x049, "dim"},
        {0x111, "aug"}, {0x085, "sus2"},  {0x0A1, "sus4"},  {0x4A1, "7sus4"}, {0x449, "m7b5"}, {0x249, "dim7"},
        {0x291, "6"},   {0x289, "m6"},    {0x095, "add9"},  {0x08D, "madd9"}, {0x081, "5"},  {0x001, ""},
    };
    const int nn = (int)(sizeof NAMES / sizeof NAMES[0]);
    int i, k, r = -1, q = -1;
    char *b = buf;
    mask &= 0xFFFu;
    bass = (bass % 12 + 12) % 12;
    mask |= 1u << bass;
    for (k = 0; k < nn && q < 0; k++)            /* the bass as the root, if a quality fits it ("C6") */
        if (rot12(mask, -bass) == NAMES[k].iv) {
            r = bass;
            q = k;
        }
    for (k = 0; k < nn && q < 0; k++)            /* else the commonest quality on any root ("Am7/G") */
        for (i = 0; i < 12 && q < 0; i++)
            if ((mask >> i & 1u) && rot12(mask, -i) == NAMES[k].iv) {
                r = i;
                q = k;
            }
    if (q < 0) {
        b = put(b, en_note_name(bass));
        put(b, "*");
        return buf;
    }
    b = put(b, en_note_name(r));
    b = put(b, NAMES[q].q);
    if (r != bass) {
        *b++ = '/';
        put(b, en_note_name(bass));
    }
    return buf;
}

/* a ROLLED figure: 3 to 12 steps (the seed's last digit), one of four shapes: runs up, runs down, a
 * wave, a zigzag climbing; mostly by step, now and then a leap, over the chord's lowest seven tones */
static int fig_roll(uint32_t seed, int8_t *s)
{
    uint32_t r = lr_seed(seed);
    int n = 3 + (int)(seed % 10u), i, x, kind = (int)(lr_next(&r) % 4u), dir = 1, same = 1;
    x = kind == 1 ? 3 + (int)(lr_next(&r) % 3u) : (int)(lr_next(&r) % 3u);
    for (i = 0; i < n; i++) {
        int leap = lr_next(&r) % 4u == 0u;
        s[i] = (int8_t)x;
        if (kind == 0) {
            x += 1 + leap;
            if (x > 6)
                x = (int)(lr_next(&r) % 2u);
        } else if (kind == 1) {
            x -= 1 + leap;
            if (x < 0)
                x = 4 + (int)(lr_next(&r) % 3u);
        } else if (kind == 2) {
            x += dir;
            if (x >= 4 || x <= 0 || lr_next(&r) % 5u == 0u)
                dir = -dir;
        } else {
            x += (i & 1) ? -1 : 2;
            if (x > 6)
                x = 1;
        }
        x = x < 0 ? 0 : x > 6 ? 6 : x;
        if (i && s[i] != s[0])
            same = 0;
    }
    if (same)                                    /* never one note over and over */
        s[n - 1] = (int8_t)(s[0] >= 3 ? s[0] - 2 : s[0] + 2);
    return n;
}

int en_figure(int fig, int seed, int8_t *steps)
{
    int i;
    if (fig >= EN_NFIG)
        return fig_roll((uint32_t)seed, steps);
    fig = fig < 0 ? 0 : fig;
    for (i = 0; i < FIGS[fig].n; i++)
        steps[i] = FIGS[fig].s[i];
    return FIGS[fig].n;
}
const char *en_figure_name(int fig) { return fig >= EN_NFIG ? "Rolled" : FIGS[fig < 0 ? 0 : fig].name; }

static int nearest(int from, int pc, int lo, int hi)    /* the note of class pc nearest from, in lo..hi */
{
    int n = from + ((pc - from) % 12 + 12) % 12;          /* at or above */
    if (n - from > 6)
        n -= 12;
    while (n < lo)
        n += 12;
    while (n > hi)
        n -= 12;
    return n;
}

void en_voice_lead(const int *from, const int *pcs, int nt, int *to, int lo, int hi)
{
    static const uint8_t PERM[24][4] = {
        {0, 1, 2, 3}, {0, 1, 3, 2}, {0, 2, 1, 3}, {0, 2, 3, 1}, {0, 3, 1, 2}, {0, 3, 2, 1},
        {1, 0, 2, 3}, {1, 0, 3, 2}, {1, 2, 0, 3}, {1, 2, 3, 0}, {1, 3, 0, 2}, {1, 3, 2, 0},
        {2, 0, 1, 3}, {2, 0, 3, 1}, {2, 1, 0, 3}, {2, 1, 3, 0}, {2, 3, 0, 1}, {2, 3, 1, 0},
        {3, 0, 1, 2}, {3, 0, 2, 1}, {3, 1, 0, 2}, {3, 1, 2, 0}, {3, 2, 0, 1}, {3, 2, 1, 0}};
    int t[4], p, v, best = 1 << 30;
    for (v = 0; v < 4; v++)
        t[v] = pcs[v < nt ? v : 0];                        /* three tones: the bass doubled */
    for (p = 0; p < 24; p++) {
        int c = 0, n[4], a, b;
        for (v = 0; v < 4; v++) {
            n[v] = nearest(from[v], t[PERM[p][v]], lo, hi);
            c += n[v] > from[v] ? n[v] - from[v] : from[v] - n[v];
        }
        for (a = 0; a < 4; a++)                            /* no two voices on one note */
            for (b = a + 1; b < 4; b++)
                if (n[a] == n[b])
                    c += 40;
        if (c < best) {
            best = c;
            for (v = 0; v < 4; v++)
                to[v] = n[v];
        }
    }
}

/* the knobs' names where a model plays them otherwise (0: the page's own name) */
const char *en_param_label(int i)
{
    int m = par[P_MODEL];
    switch (i) {
    case P_PROCESS: return m == M_PHASE ? "Drift" : m == M_PATTERNS ? "Spread" : m == M_BELLS ? "Phrases" : 0;
    case P_EVOLVE: return m == M_PATTERNS ? "Move on" : 0;
    case P_UP_FIGURE: return m == M_BELLS ? "Bells" : m == M_PATTERNS ? "Start" : 0;
    case P_LO_FIGURE: return m == M_PATTERNS ? "Start" : 0;
    case P_UP_PULSE: return m == M_BELLS ? "-" : 0;
    case P_LD_ACT: return m == M_BELLS || m == M_PATTERNS ? "Pace" : 0;
    default: return 0;
    }
}

void en_param_text(int i, int v, char *b)
{
    int p, m = par[P_MODEL];
    switch (i) {
    case P_UP_SOUND: case P_LO_SOUND: case P_BS_SOUND: case P_LD_SOUND: case P_CH_SOUND:
        for (p = 0; p < EN_NPART && SOUND_PAR[p] != i; p++)
            ;
        put(b, EN_INST_NAME[SOUNDS[p][(unsigned)v < NSOUNDS[p] ? v : 0]]);
        return;
    case P_UP_PULSE: case P_LO_PULSE:
        put(b, i == P_UP_PULSE && m == M_BELLS ? "-" : PULSE_NAME[v & 3]);
        return;
    case P_UP_FIGURE: case P_LO_FIGURE:
        if (m == M_BELLS && i == P_UP_FIGURE) {
            put(b, BELL_NAME[(unsigned)v % 4u]);
        } else if (m == M_PATTERNS) {
            b = put(b, "From ");
            itoa_s(v + 1, b);
        } else {
            put(b, en_figure_name(v));
        }
        return;
    case P_CHORDLEN: put(b, CHORD_NAME[v & 3]); return;
    case P_BS_STYLE: put(b, STYLE_NAME[v & 3]); return;
    case P_LD_HARM: put(b, HARM_NAME[(unsigned)v < 3u ? v : 0]); return;
    case P_CH_RHYTHM: put(b, RHYTHM_NAME[(unsigned)v < 6u ? v : 0]); return;
    case P_ROOT: put(b, en_note_name(v)); return;
    case P_KEYS: put(b, v ? "Notes" : "Easy"); return;
    case P_HOLD: case P_MIDI: put(b, v ? "On" : "Off"); return;
    case P_MODEL: put(b, EN_MODEL_NAME[(unsigned)v < EN_NMODELS ? v : 0]); return;
    case P_CYCLE: put(b, en_cycle_name(v)); return;
    case P_BS_OCT: case P_UP_OCT:
        if (v > 0)
            *b++ = '+';
        itoa_s(v, b);
        return;
    default: itoa_s(v, b); return;
    }
}

/* ------------------------------------------------------------ commands --- */
enum { C_SET, C_PLAY, C_NEXT, C_SEED, C_PANIC, C_CLOCK, C_REGROW, C_HOLD, C_MINE_CLR, C_MINE_ADD, C_MINE_END,
       C_PART, C_VERB, C_SONG };
#define QN 128u
static uint32_t q[QN];
static volatile uint32_t q_w, q_r;
static void post(uint32_t c, int a, int b)
{
    uint32_t w = q_w;
    if (w - q_r >= QN)
        return;                                   /* full: dropped (never, at the UI's rate) */
    q[w % QN] = c << 24 | ((uint32_t)a & 0xFFu) << 16 | ((uint32_t)b & 0xFFFFu);
    BARRIER();
    q_w = w + 1;
}
void en_set(int p, int v) { post(C_SET, p, v); }
void en_play(int on) { post(C_PLAY, on, 0); }
void en_next(void) { post(C_NEXT, 0, 0); }
void en_seed(uint32_t s) { post(C_SEED, 0, (int)(s & 0xFFFFu)); }
void en_panic(void) { post(C_PANIC, 0, 0); }
void en_clock(int msg) { post(C_CLOCK, msg, 0); }
void en_regrow(void) { post(C_REGROW, 0, 0); }
void en_hold(uint32_t mask, int bass, int keys) { post(C_HOLD, (bass & 15) | (keys == 1 ? 16 : 0), (int)(mask & 0xFFFu)); }
void en_mine(int n, const uint16_t *mask, const uint8_t *bass)
{
    int k;
    post(C_MINE_CLR, 0, 0);
    for (k = 0; k < n && k < EN_CYC_MAX; k++)
        post(C_MINE_ADD, bass[k] & 15, (int)(mask[k] & 0xFFFu));
    post(C_MINE_END, 0, 0);
}
void en_part(int p, int on) { post(C_PART, p, on != 0); }
void en_verb(int p, int verb) { post(C_VERB, p, verb); }
void en_song_cmd(int what, int piece) { post(C_SONG, what, piece); }

/* -------------------------------------------------------------- state --- */
volatile uint8_t en_playing, en_ext;
volatile uint8_t en_cyc_n, en_cyc_pos, en_cyc_rep, en_cyc_reps;
volatile uint16_t en_cyc_mask[EN_CYC_MAX];
volatile uint8_t en_cyc_bass[EN_CYC_MAX];
volatile uint16_t en_chord_mask;
volatile uint8_t en_chord_bass;
volatile uint8_t en_yours;
volatile float en_chord_t;
volatile int8_t en_shift;
volatile uint8_t en_part_on[EN_NPART];
volatile float en_part_glow[EN_NPART];
volatile uint8_t en_part_div[EN_NPART];
volatile uint8_t en_fig_len[EN_NPART], en_fig_full[EN_NPART], en_fig_pos[EN_NPART], en_fig_proc[EN_NPART];
volatile uint8_t en_fig_user[EN_NPART];
volatile float en_phase;
volatile uint8_t en_pat_at[EN_NPART], en_pat_n;
volatile uint32_t en_beats, en_section;
volatile float en_arc;
volatile uint32_t en_notes[EN_NPART];
volatile uint8_t en_voices;
volatile uint8_t en_song_state, en_song_full;
volatile uint32_t en_song_tick;

/* two dice: the music's (the generators, in the render's ticks and commands only, so a take plays back
 * the same) and the sound's (noise, an oscillator's phase: anything that may differ from take to take) */
static uint32_t rng = 0x9E3779B9u, nrng = 0x2545F491u;
static float evolve, build, process;
static uint8_t part_inst[EN_NPART];

static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}
static float frand(void) { return (float)(rnd() >> 8) * (1.0f / 16777216.0f); }
static int irand(int n) { return n > 0 ? (int)((rnd() >> 8) % (uint32_t)n) : 0; }
static uint32_t nrnd(void)
{
    nrng ^= nrng << 13;
    nrng ^= nrng >> 17;
    nrng ^= nrng << 5;
    return nrng;
}

static void midi(uint32_t st, uint32_t d1, uint32_t d2)
{
    if (par[P_MIDI])
        en_midi_out(st, d1, d2);
}

static float mtof(int n) { return 440.0f * fm_exp2f((float)(n - 69) * (1.0f / 12.0f)); }

/* a sine table: 1024 points a turn, plus one */
#define SN 1024
static float sint[SN + 1];
static inline float sine(float ph)              /* ph in turns, 0 <= ph < 1 */
{
    float x = ph * (float)SN;
    int i = (int)x;
    float f = x - (float)i;
    return sint[i] + (sint[i + 1] - sint[i]) * f;
}

static inline float blep_saw(float t, float dt)
{
    float y = 2.0f * t - 1.0f;
    if (t < dt) {
        float x = t / dt;
        y -= x + x - x * x - 1.0f;
    } else if (t > 1.0f - dt) {
        float x = (t - 1.0f) / dt;
        y -= x * x + x + x + 1.0f;
    }
    return y;
}

/* ------------------------------------------------------------ the voices --- */
/* Two kinds. MODAL (piano, marimba): a bank of damped resonators, each y = a1 y1 - a2 y2, struck at
 * its start with a sine of the partial's amplitude; the piano's partials are stretched as a stiff
 * string's (f_k = k f0 sqrt(1 + B k^2)), its two lowest doubled (a quick and a slow decay a hair
 * apart, the two stages of a piano's sound), and a short thump of noise. SUBTRACTIVE (organ, flute,
 * strings, sax, horn, synth bass): two oscillators and breath noise, a low-pass (TPT state variable),
 * a formant band-pass for the sax, an ADSR, vibrato; a patch each. */
#define NRES 8
enum { ST_ATT, ST_SUS, ST_REL };
typedef struct {
    uint8_t on, inst, part, note, midi, stage, dry;   /* dry: a modal note damped when its gate ends */
    int32_t gate;                                /* samples until the release (-1: until released) */
    int32_t wait;                                /* samples before it sounds (a note off the grid: PHASE) */
    float lvl;                                   /* how loud it still is, about (the quietest is taken first) */
    uint32_t age;
    float vel, gl, gr, send;
    /* modal */
    uint8_t nres;
    float a1[NRES], a2[NRES], y1[NRES], y2[NRES], pth[NRES];   /* pth: below this the partial is gone */
    float nz, nzd, nlp, nc;                      /* the strike's noise: level, decay, low-pass state, coefficient */
    float damp, dampk;                           /* the pedal: a gain, and its decay a sample once damped */
    /* subtractive */
    float ph1, ph2, inc0, r2k, f0, vt;
    float env, fenv, att, rel;
    float s1, s2, fs1, fs2;
} voice_t;

#define NV 32
static const uint8_t VB[EN_NPART + 1] = {0, 9, 17, 20, 24, 32};   /* each part's voices: VB[p] .. VB[p + 1] */
static voice_t vc[NV];
static uint32_t vage;
static float pg[EN_NPART], pg_t[EN_NPART];       /* the parts' gains, now and wanted (level, terrace) */
static float damp_k, fade_k, dry_k;              /* the pedal's damping (180 ms), a quick fade (60 ms), a dry
                                                 * note's end (250 ms) */

/* OVERLOAD GUARD (as X0X's): the platform reports each block's load after rendering it (en_load, the
 * render's context). Past the CPU (a block over 90%, or 82% on average) the subtractive voices drop
 * their second oscillator, the arpeggios keep two of them each, and the piano's partials end at -60 dB
 * instead of -90; after 3 s back under 65% all return. Measured off the device (tests/host/min_render),
 * the pieces stay well under it. */
#define GUARD_ON_LAST 90u
#define GUARD_ON_AVG 82u
#define GUARD_OFF 65u
#define GUARD_CALM_FRAMES (3u * 44100u)
static uint8_t guard;
static uint32_t guard_avg_q8, guard_calm;
volatile uint32_t en_guard_count;
volatile uint8_t en_guarding;
void en_load(uint32_t pct, uint32_t frames)
{
    guard_avg_q8 = (guard_avg_q8 * 15u + pct * 256u) / 16u;
    if (!guard) {
        if (pct > GUARD_ON_LAST || guard_avg_q8 > GUARD_ON_AVG * 256u) {
            guard = 1;
            guard_calm = 0;
            en_guard_count++;
        }
    } else if (pct >= GUARD_OFF || guard_avg_q8 >= GUARD_OFF * 256u) {
        guard_calm = 0;
    } else if ((guard_calm += frames) >= GUARD_CALM_FRAMES) {
        guard = 0;
    }
    en_guarding = guard;
}

enum { W_SAW, W_SQR, W_SIN, W_NONE };
typedef struct {
    uint8_t w1, w2;
    float r2, det, mix2;                         /* oscillator 2: its ratio to 1, detune (cents), level */
    float noise;                                 /* breath */
    float cut, kt, camp, cdec, fdec, res;        /* low-pass: Hz + key x f0 + amp env x camp + decay env x cdec */
    float form, fk, fg;                          /* a formant: Hz, damping, gain (0: none) */
    float att, dec, sus, rel;                    /* seconds, sustain level */
    float vr, vd, vdel;                          /* vibrato: Hz, cents, its delay (s) */
    float gain, send;
} patch_t;
/* gains: each sound about as loud as the piano at the same Level (measured alone, tests/host/min_render -parts) */
static const patch_t PATCH[EN_NINST] = {
    [I_ORGAN] = {W_SQR, W_SIN, 2.0f, 0.0f, 0.55f, 0.0f, 2400.0f, 1.0f, 0.0f, 0.0f, 0.1f, 0.0f, 0.0f, 1.0f, 0.0f,
                 0.004f, 0.05f, 1.0f, 0.03f, 6.2f, 5.0f, 0.0f, 0.32f, 0.30f},   /* 8' square, 4' sine */
    [I_FLUTE] = {W_SIN, W_SIN, 2.0f, 0.0f, 0.12f, 0.12f, 4500.0f, 2.0f, 0.0f, 0.0f, 0.1f, 0.0f, 0.0f, 1.0f, 0.0f,
                 0.05f, 0.15f, 0.85f, 0.10f, 5.0f, 12.0f, 0.25f, 0.28f, 0.55f},
    [I_STRINGS] = {W_SAW, W_SAW, 1.0f, 9.0f, 1.0f, 0.0f, 700.0f, 1.6f, 1500.0f, 0.0f, 0.1f, 0.1f, 0.0f, 1.0f, 0.0f,
                   0.22f, 0.3f, 0.9f, 0.35f, 5.5f, 9.0f, 0.3f, 0.175f, 0.60f},
    [I_SAX] = {W_SAW, W_SQR, 1.0f, 3.0f, 0.3f, 0.05f, 1200.0f, 1.2f, 1500.0f, 0.0f, 0.1f, 0.15f, 1250.0f, 0.6f, 1.2f,
               0.035f, 0.2f, 0.85f, 0.12f, 5.2f, 16.0f, 0.3f, 0.16f, 0.45f},
    [I_HORN] = {W_SAW, W_SAW, 1.0f, 4.0f, 0.5f, 0.0f, 250.0f, 1.0f, 1300.0f, 0.0f, 0.1f, 0.05f, 0.0f, 1.0f, 0.0f,
                0.12f, 0.3f, 0.85f, 0.25f, 4.5f, 5.0f, 0.5f, 0.34f, 0.50f},
    [I_SYNBASS] = {W_SAW, W_SQR, 0.5f, 0.0f, 0.6f, 0.0f, 140.0f, 0.6f, 250.0f, 1600.0f, 0.15f, 0.3f, 0.0f, 1.0f, 0.0f,
                   0.004f, 0.3f, 0.8f, 0.07f, 0.0f, 0.0f, 0.0f, 0.32f, 0.10f},
};
#define PIANO_GAIN 0.55f
#define PIANO_SEND 0.50f
#define MARIMBA_GAIN 0.52f
#define MARIMBA_SEND 0.45f
#define ARP_SUB_VOICES 4                         /* an arpeggio's subtractive voices (the guard: 2) */
static const float PART_PAN[EN_NPART] = {0.45f, -0.45f, 0.0f, 0.15f, 0.0f};
/* the parts' gains at the same Level: a held chord sounds louder than notes that come and go */
static const float PART_GAIN[EN_NPART] = {1.0f, 1.0f, 1.0f, 1.0f, 0.56f};

static int modal(int inst) { return inst == I_PIANO || inst == I_MARIMBA; }

static void voice_midi_off(voice_t *v)
{
    if (v->midi) {
        midi(0x80u | v->part, v->midi, 0);
        v->midi = 0;
    }
}

static void voice_release(voice_t *v)            /* its gate ends at the next block */
{
    if (v->on && v->stage != ST_REL)
        v->gate = 1;
}

static voice_t *voice_get(int p)                 /* a free voice of the part's, or the one to take */
{
    int i, best = VB[p], end = VB[p + 1];
    uint32_t score = 0;
    (void)vage;
    if (p <= PT_LOWER && !modal(part_inst[p]))  /* a quick subtractive arpeggio: a few voices are enough */
        end = VB[p] + (guard ? 2 : ARP_SUB_VOICES);
    for (i = VB[p]; i < end; i++)
        if (!vc[i].on)
            return &vc[i];
    for (i = VB[p]; i < end; i++) {             /* released or damped first, then the quietest */
        const voice_t *v = &vc[i];
        float q = v->lvl * v->vel * ((v->stage == ST_REL || v->dampk < 1.0f) ? 0.1f : 1.0f);
        uint32_t s = 0xFFFFFFFFu - (uint32_t)(fm_clampf(q, 0.0f, 1.0f) * 4.0e9f);
        if (s > score || i == VB[p]) {
            score = s;
            best = i;
        }
    }
    voice_midi_off(&vc[best]);
    return &vc[best];
}

static void res_add(voice_t *v, float f, float t60, float a)
{
    float w, r, c, s, y1;
    int i = v->nres;
    if (i >= NRES || f > 0.42f * EN_SR || a <= 0.0f)
        return;
    w = FM_TWO_PI * f * (1.0f / EN_SR);
    r = fm_expf(-6.9078f / (t60 * EN_SR));
    c = fm_cosf(w);
    s = fm_sinf(w);
    y1 = -a * s / r;                             /* y[-1], y[-2] of a r^n sin(w n): the sine starts at 0 */
    v->a1[i] = 2.0f * r * c;
    v->a2[i] = r * r;
    v->y1[i] = y1;
    v->y2[i] = 2.0f * c * y1 / r;
    v->pth[i] = 9e-10f * s * s;                  /* (3e-5 x sin w)^2: the partial's amplitude below 3e-5 */
    v->nres = (uint8_t)(i + 1);
}

static const float LOG2K[7] = {0.0f, 0.0f, 1.0f, 1.5849625f, 2.0f, 2.3219281f, 2.5849625f};
static void piano_on(voice_t *v)
{
    int n = v->note, k;
    float f0 = mtof(n), B = 0.00035f * fm_exp2f((float)(n - 60) * (1.0f / 20.0f)), t1, roll, sum = 0.0f, a[7];
    t1 = fm_clampf(12.0f * fm_exp2f(-(float)(n - 45) * (1.0f / 22.0f)), 0.8f, 16.0f);   /* the fundamental's T60 */
    roll = 1.75f - 0.9f * v->vel;                /* harder: brighter */
    for (k = 1; k <= 6; k++) {
        a[k] = fm_exp2f(-roll * LOG2K[k]);
        sum += a[k];
    }
    v->nres = 0;
    for (k = 1; k <= 6; k++) {
        float f = (float)k * f0 * fm_sqrtf(1.0f + B * (float)(k * k)), t60, ak = a[k] / sum;
        if (f > 10000.0f)
            break;
        t60 = t1 / (1.0f + 0.4f * (float)(k - 1)) / (1.0f + f * (1.0f / 3500.0f));
        if (k <= 2) {
            res_add(v, f, t60 * 0.18f, ak * 0.55f);          /* the prompt sound */
            res_add(v, f * 1.0004f, t60, ak * 0.45f);         /* the aftersound, a hair apart */
        } else {
            res_add(v, f, t60, ak);
        }
    }
    v->nz = 0.08f + 0.2f * v->vel;
    v->nzd = fm_expf(-1.0f / (0.004f * EN_SR));
    v->nc = 1.0f - fm_expf(-FM_TWO_PI * fm_minf(f0 * 5.0f, 5000.0f) * (1.0f / EN_SR));
}

static void marimba_on(voice_t *v)
{
    static const float R[3] = {1.0f, 3.93f, 9.24f}, A[3] = {0.62f, 0.26f, 0.12f}, T[3] = {1.0f, 0.3f, 0.1f};
    int k;
    float f0 = mtof(v->note), t1 = fm_clampf(1.4f * fm_exp2f(-(float)(v->note - 60) * (1.0f / 18.0f)), 0.25f, 2.5f);
    v->nres = 0;
    for (k = 0; k < 3; k++)
        res_add(v, f0 * R[k], t1 * T[k], A[k] * (k ? 0.4f + 0.6f * v->vel : 1.0f));
    v->nz = 0.12f * v->vel;
    v->nzd = fm_expf(-1.0f / (0.0015f * EN_SR));
    v->nc = 1.0f - fm_expf(-FM_TWO_PI * 2500.0f * (1.0f / EN_SR));
}

static void sub_on(voice_t *v, float att)
{
    const patch_t *pt = &PATCH[v->inst];
    v->f0 = mtof(v->note);
    v->inc0 = v->f0 * (1.0f / EN_SR);
    v->r2k = pt->r2 * fm_exp2f(pt->det * (1.0f / 1200.0f));
    v->env = 0.0f;
    v->fenv = 1.0f;
    v->vt = 0.0f;
    v->att = att > 0.0f ? att : pt->att;
    v->rel = pt->rel;
    v->ph1 = 0.0f;
    v->ph2 = (float)(nrnd() >> 8) * (1.0f / 16777216.0f);
    v->s1 = v->s2 = v->fs1 = v->fs2 = 0.0f;
}

/* a note: part p's sound, velocity 0..1, its gate in samples (-1: until released); att > 0 overrides the
 * patch's attack (strings in a quick arpeggio, a swell) */
static voice_t *note_on(int p, int note, float vel, int32_t gate, float att)
{
    static const voice_t zero;
    voice_t *v;
    int inst = part_inst[p], i;
    float pan;
    if (note < 21 || note > 108)
        return 0;
    for (i = VB[p]; i < VB[p + 1]; i++)          /* the same note still ringing in a modal part: it fades */
        if (vc[i].on && vc[i].note == note && modal(vc[i].inst) && vc[i].dampk >= 1.0f)
            vc[i].dampk = fade_k;
    v = voice_get(p);
    *v = zero;
    v->on = 1;
    v->part = (uint8_t)p;
    v->inst = (uint8_t)inst;
    v->note = (uint8_t)note;
    v->vel = vel;
    v->gate = gate;
    v->age = ++vage;
    v->damp = 1.0f;
    v->dampk = 1.0f;
    v->stage = ST_ATT;
    v->lvl = 1.0f;
    v->dry = (uint8_t)(modal(inst) && (par[P_MODEL] == M_PHASE || par[P_MODEL] == M_PATTERNS));
    if (modal(inst))                            /* low to the left, high to the right: a piano from its bench */
        pan = fm_clampf((float)(note - 62) * (1.0f / 36.0f), -0.55f, 0.55f);
    else
        pan = PART_PAN[p] + (p == PT_CHORDS ? 0.4f * (float)((note % 3) - 1) : 0.0f);
    pan = (pan + 1.0f) * (FM_PI / 4.0f);
    v->gl = fm_cosf(pan);
    v->gr = fm_sinf(pan);
    if (inst == I_PIANO) {
        v->send = PIANO_SEND;
        piano_on(v);
    } else if (inst == I_MARIMBA) {
        v->send = MARIMBA_SEND;
        marimba_on(v);
    } else {
        v->send = PATCH[inst].send;
        sub_on(v, att);
    }
    v->midi = (uint8_t)note;
    midi(0x90u | (uint32_t)p, (uint32_t)note, (uint32_t)(20.0f + 107.0f * fm_clampf(vel, 0.0f, 1.0f)));
    en_notes[p]++;
    en_part_glow[p] = 1.0f;
    return v;
}

/* the pedal: a new chord damps the piano's notes it lacks */
static void pedal(uint32_t mask)
{
    int i;
    for (i = 0; i < NV; i++) {
        voice_t *v = &vc[i];
        if (v->on && v->inst == I_PIANO && v->dampk >= 1.0f && !(mask >> (v->note % 12) & 1u))
            v->dampk = damp_k;
    }
}

/* ---------------------------------------------------------- the render --- */
#define BLK 256
static float mixl[BLK], mixr[BLK], rsend[BLK], vbuf[BLK], nzb[BLK];

static void gate_step(voice_t *v, uint32_t n)
{
    if (v->gate < 0)
        return;
    v->gate -= (int32_t)n;
    if (v->gate <= 0) {
        v->gate = -1;
        voice_midi_off(v);
        v->stage = ST_REL;
        if (v->dry && v->dampk >= 1.0f)        /* no pedal: the note ends with its gate */
            v->dampk = dry_k;
    }
}

static void modal_render(voice_t *v, uint32_t off, uint32_t n)
{
    uint32_t j;
    int r;
    float e = 0.0f;
    float g = v->vel * pg[v->part] * (v->inst == I_PIANO ? PIANO_GAIN : MARIMBA_GAIN), d = v->damp;
    const float dk = v->dampk;
    for (j = 0; j < n; j++)
        vbuf[j] = 0.0f;
    for (r = 0; r < v->nres; r++) {
        float a1 = v->a1[r], a2 = v->a2[r], y1 = v->y1[r], y2 = v->y2[r];
        for (j = 0; j < n; j++) {
            float y = a1 * y1 - a2 * y2;
            y2 = y1;
            y1 = y;
            vbuf[j] += y;
        }
        v->y1[r] = y1;
        v->y2[r] = y2;
    }
    for (r = 0; r < v->nres;) {                  /* the partials gone below hearing: dropped */
        float y1 = v->y1[r], y2 = v->y2[r], q = y1 * y1 + y2 * y2 - v->a1[r] * y1 * y2;
        e += q;
        if (q < v->pth[r] * (guard ? 1000.0f : 1.0f)) {
            int l = --v->nres;
            v->a1[r] = v->a1[l];
            v->a2[r] = v->a2[l];
            v->y1[r] = v->y1[l];
            v->y2[r] = v->y2[l];
            v->pth[r] = v->pth[l];
        } else {
            r++;
        }
    }
    if (v->nz > 1e-5f) {                         /* the hammer's or the mallet's thump */
        float nz = v->nz, lp = v->nlp;
        for (j = 0; j < n; j++) {
            lp += v->nc * (nzb[off + j] - lp);
            vbuf[j] += lp * nz;
            nz *= v->nzd;
        }
        v->nz = nz;
        v->nlp = lp;
    }
    for (j = 0; j < n; j++) {
        float o = vbuf[j] * g * d;
        d *= dk;
        mixl[off + j] += o * v->gl;
        mixr[off + j] += o * v->gr;
        rsend[off + j] += o * v->send;
    }
    v->damp = d;
    v->lvl = fm_sqrtf(e) * d * 20.0f;            /* the partials' energy (their sum x sin w, roughly) */
    gate_step(v, n);
    if ((v->nres == 0 && v->nz <= 1e-5f) || d < 1e-4f) {
        voice_midi_off(v);
        v->on = 0;
    }
}

static inline float osc(int w, float ph, float inc)
{
    if (w == W_SAW)
        return blep_saw(ph, inc);
    if (w == W_SQR) {
        float p2 = ph + 0.5f;
        if (p2 >= 1.0f)
            p2 -= 1.0f;
        return 0.5f * (blep_saw(ph, inc) - blep_saw(p2, inc));
    }
    if (w == W_SIN)
        return sine(ph);
    return 0.0f;
}

static void svf(float fc, float k, float *c)
{
    float g = fm_tanf(FM_PI * fm_clampf(fc, 30.0f, 0.42f * EN_SR) * (1.0f / EN_SR));
    c[0] = 1.0f / (1.0f + g * (g + k));
    c[1] = g * c[0];
    c[2] = g * c[1];
}

/* the subtractive voice, n <= 32 samples from off: the slow things once, then each sample. The sample
 * loop is written out for each pair of waves the patches use (SUB_LOOP), so it has no branch on the
 * wave inside: the FM-1's core is in order, and the voices are most of the render */
#define SUB_LOOP(W1, W2, FORM)                                                                          \
    for (j = 0; j < n; j++) {                                                                           \
        float x = osc(W1, ph1, inc1) + m2 * osc(W2, ph2, inc2) + nz * nzb[off + j], v1, v2, v3, o;      \
        ph1 += inc1;                                                                                    \
        if (ph1 >= 1.0f)                                                                                \
            ph1 -= 1.0f;                                                                                \
        if (W2 != W_NONE) {                                                                             \
            ph2 += inc2;                                                                                \
            if (ph2 >= 1.0f)                                                                            \
                ph2 -= 1.0f;                                                                            \
        }                                                                                               \
        v3 = x - s2;                             /* the low-pass */                                     \
        v1 = lc[0] * s1 + lc[1] * v3;                                                                   \
        v2 = s2 + lc[1] * s1 + lc[2] * v3;                                                              \
        s1 = 2.0f * v1 - s1;                                                                            \
        s2 = 2.0f * v2 - s2;                                                                            \
        o = v2;                                                                                         \
        if (FORM) {                              /* the formant, from the oscillators */                \
            v3 = x - f2;                                                                                \
            v1 = fc[0] * f1 + fc[1] * v3;                                                               \
            v2 = f2 + fc[1] * f1 + fc[2] * v3;                                                          \
            f1 = 2.0f * v1 - f1;                                                                        \
            f2 = 2.0f * v2 - f2;                                                                        \
            o += fg * v1;                                                                               \
        }                                                                                               \
        o *= e * g;                                                                                     \
        e += de;                                                                                        \
        mixl[off + j] += o * gl;                                                                        \
        mixr[off + j] += o * gr;                                                                        \
        rsend[off + j] += o * sd;                                                                       \
    }

static void sub_render(voice_t *v, uint32_t off, uint32_t n)
{
    const patch_t *pt = &PATCH[v->inst];
    const float t = (float)n * (1.0f / EN_SR), nz = pt->noise, fg = pt->fg, gl = v->gl, gr = v->gr, sd = v->send;
    float e0 = v->env, e1, e, de, vib, inc1, inc2, lc[3], fc[3] = {0.0f, 0.0f, 0.0f}, g, m2 = pt->mix2;
    float ph1 = v->ph1, ph2 = v->ph2, s1 = v->s1, s2 = v->s2, f1 = v->fs1, f2 = v->fs2;
    uint32_t j;
    gate_step(v, n);
    if (v->stage == ST_ATT) {
        e1 = e0 + t / v->att;
        if (e1 >= 1.0f) {
            e1 = 1.0f;
            v->stage = ST_SUS;
        }
    } else if (v->stage == ST_SUS) {
        e1 = pt->sus + (e0 - pt->sus) * fm_expf(-t / pt->dec);
    } else {
        e1 = e0 * fm_expf(-t / v->rel);
    }
    v->fenv *= fm_expf(-t / pt->fdec);
    v->vt += t;
    vib = pt->vd * fm_clampf((v->vt - pt->vdel) * 2.5f, 0.0f, 1.0f) * sine(v->vt * pt->vr - (float)(int)(v->vt * pt->vr));
    inc1 = v->inc0 * fm_exp2f(vib * (1.0f / 1200.0f));
    inc2 = inc1 * v->r2k;
    svf(pt->cut + pt->kt * v->f0 + pt->camp * e1 + pt->cdec * v->fenv, 1.414f - 1.3f * pt->res, lc);
    if (fg > 0.0f)
        svf(pt->form, pt->fk, fc);
    g = pt->gain * v->vel * pg[v->part];
    de = (e1 - e0) / (float)n;
    e = e0 + de;
    if (guard) {                                 /* the overload guard: one oscillator */
        m2 = 0.0f;
        if (pt->w1 == W_SIN)
            SUB_LOOP(W_SIN, W_NONE, 0)
        else if (pt->w1 == W_SQR)
            SUB_LOOP(W_SQR, W_NONE, 0)
        else if (fg > 0.0f)
            SUB_LOOP(W_SAW, W_NONE, 1)
        else
            SUB_LOOP(W_SAW, W_NONE, 0)
    } else if (pt->w1 == W_SAW && pt->w2 == W_SAW) {
        SUB_LOOP(W_SAW, W_SAW, 0)
    } else if (pt->w1 == W_SQR && pt->w2 == W_SIN) {
        SUB_LOOP(W_SQR, W_SIN, 0)
    } else if (pt->w1 == W_SIN && pt->w2 == W_SIN) {
        SUB_LOOP(W_SIN, W_SIN, 0)
    } else if (fg > 0.0f) {
        SUB_LOOP(W_SAW, W_SQR, 1)
    } else {
        SUB_LOOP(W_SAW, W_SQR, 0)
    }
    v->ph1 = ph1;
    v->ph2 = ph2;
    v->s1 = fm_flush(s1);
    v->s2 = fm_flush(s2);
    v->fs1 = fm_flush(f1);
    v->fs2 = fm_flush(f2);
    v->env = e1;
    v->lvl = e1;
    if (v->stage == ST_REL && e1 < 3e-4f) {
        voice_midi_off(v);
        v->on = 0;
    }
}
#undef SUB_LOOP

/* ------------------------------------------------- reverb: Dattorro's plate --- */
/* J. Dattorro, "Effect Design, Part 1", JAES 1997 (as in AMB-1 and FoMni): a predelay, four input
 * diffusers, then a figure-eight tank of two halves, each a modulated allpass, a delay, a damping
 * lowpass, an allpass and a delay, feeding the other half. Stereo out from seven taps a side. The
 * paper's lengths are for 29761 Hz; DS() scales them to 44.1 kHz. */
#define DS(n) (((n) * 14818 + 5000) / 10000)
#define PRE_N 1323
#define EXC DS(16)
enum { L_PRE, L_IN1, L_IN2, L_IN3, L_IN4, L_APL, L_D1L, L_AP2L, L_D2L, L_APR, L_D1R, L_AP2R, L_D2R, L_N };
static const int DL_LEN[L_N] = {PRE_N, DS(142), DS(107), DS(379), DS(277), DS(672) + EXC + 2, DS(4453), DS(1800),
                                DS(3720), DS(908) + EXC + 2, DS(4217), DS(2656), DS(3163)};
#define DL_TOTAL (PRE_N + DS(142) + DS(107) + DS(379) + DS(277) + DS(672) + DS(4453) + DS(1800) + DS(3720) + DS(908) + \
                  DS(4217) + DS(2656) + DS(3163) + 2 * (EXC + 2))
static float pl_buf[DL_TOTAL] EN_POOL;
typedef struct {
    float *b;
    int n, i;
} dl_t;
static dl_t dl[L_N];
static float pl_bw, pl_damp_l, pl_damp_r, pl_decay = 0.5f, pl_decay_t = 0.5f, pl_dd2 = 0.5f, lfo_s, lfo_c = 1.0f;
static float rev_send = 0.25f, rev_wet = 1.0f;

static inline float tap(const dl_t *d, int k)
{
    int j = d->i - k;
    return d->b[j < 0 ? j + d->n : j];
}
static inline void push(dl_t *d, float x)
{
    d->b[d->i] = x;
    if (++d->i >= d->n)
        d->i = 0;
}
static inline float allpass(dl_t *d, int len, float x, float g)
{
    float z = tap(d, len), v = x - g * z;
    push(d, fm_flush(v));
    return z + g * v;
}
static inline float allpass_mod(dl_t *d, float len, float x, float g)
{
    int k = (int)len;
    float f = len - (float)k, z = tap(d, k) + (tap(d, k + 1) - tap(d, k)) * f, v = x - g * z;
    push(d, fm_flush(v));
    return z + g * v;
}

static inline void reverb(float in, float *ol, float *or_)
{
    float x, a, b, fb_l = tap(&dl[L_D2R], DS(3163)), fb_r = tap(&dl[L_D2L], DS(3720));
    push(&dl[L_PRE], in);
    x = tap(&dl[L_PRE], PRE_N);
    pl_bw += 0.6f * (x - pl_bw);
    x = allpass(&dl[L_IN1], DS(142), pl_bw, 0.75f);
    x = allpass(&dl[L_IN2], DS(107), x, 0.75f);
    x = allpass(&dl[L_IN3], DS(379), x, 0.625f);
    x = allpass(&dl[L_IN4], DS(277), x, 0.625f);
    {
        float s = lfo_s + 0.0001f * lfo_c, c = lfo_c - 0.0001f * s;
        lfo_s = s;
        lfo_c = c;
    }
    a = allpass_mod(&dl[L_APL], (float)DS(672) + (float)EXC * lfo_s, x + pl_decay * fb_l, -0.7f);
    push(&dl[L_D1L], a);
    b = tap(&dl[L_D1L], DS(4453));
    pl_damp_l += 0.45f * (b - pl_damp_l);
    b = allpass(&dl[L_AP2L], DS(1800), pl_damp_l * pl_decay, pl_dd2);
    push(&dl[L_D2L], b);
    a = allpass_mod(&dl[L_APR], (float)DS(908) + (float)EXC * lfo_c, x + pl_decay * fb_r, -0.7f);
    push(&dl[L_D1R], a);
    b = tap(&dl[L_D1R], DS(4217));
    pl_damp_r += 0.45f * (b - pl_damp_r);
    b = allpass(&dl[L_AP2R], DS(2656), pl_damp_r * pl_decay, pl_dd2);
    push(&dl[L_D2R], b);
    *ol = 0.6f * (tap(&dl[L_D1R], DS(266)) + tap(&dl[L_D1R], DS(2974)) - tap(&dl[L_AP2R], DS(1913)) +
                  tap(&dl[L_D2R], DS(1996)) - tap(&dl[L_D1L], DS(1990)) - tap(&dl[L_AP2L], DS(187)) -
                  tap(&dl[L_D2L], DS(1066)));
    *or_ = 0.6f * (tap(&dl[L_D1L], DS(353)) + tap(&dl[L_D1L], DS(3627)) - tap(&dl[L_AP2L], DS(1228)) +
                   tap(&dl[L_D2L], DS(2673)) - tap(&dl[L_D1R], DS(2111)) - tap(&dl[L_AP2R], DS(335)) -
                   tap(&dl[L_D2R], DS(121)));
}

static void plate_init(void)
{
    float *p = pl_buf;
    int k, i;
    for (k = 0; k < L_N; k++) {
        dl[k].b = p;
        dl[k].n = DL_LEN[k];
        dl[k].i = 0;
        p += DL_LEN[k];
    }
    for (i = 0; i < DL_TOTAL; i++)
        pl_buf[i] = 0.0f;
    pl_bw = pl_damp_l = pl_damp_r = 0.0f;
    lfo_s = 0.0f;
    lfo_c = 1.0f;
}

/* ------------------------------------------------------------ the form --- */
static uint16_t tpl_mask[EN_CYC_MAX];          /* the cycle, relative to the root (the bass's class included) */
static uint8_t tpl_bass[EN_CYC_MAX];
static int tpl_n = 1, tpl_minor;
static int var_k = -1;                          /* a chord standing in for this one, this section */
static uint16_t var_mask;
static uint8_t var_bass;
static int shift;                               /* the cycle moved a third away, this section */
static uint16_t cur_mask = 0x091;               /* the chord sounding (absolute, the bass's class in it) */
static uint8_t cur_bass;
static uint16_t held_mask;                      /* the player's */
static uint8_t held_bass, keys_down, yours, chord_dirty, next_pending;
static int cyc_pos, cyc_rep, cyc_reps = 4;
static uint32_t ticks, ctick, chord_ticks = 4 * EN_PPQN;
static uint32_t stick;                          /* a take's ticks: from its top, never set back within it */
static float tick_len = 1000.0f, tick_left;
static uint8_t present[EN_NPART];               /* in this section */
static uint8_t part_req[EN_NPART];              /* by hand, at the next chord: 1 out, 2 in */
static float terr = 0.85f;                      /* the section's dynamic (a terrace) */
static float arc = -0.45f, arc_tgt = 0.85f;
static uint32_t arc_left;                       /* ticks until the arc picks a new way */
static uint32_t ext_age;

static int model(void) { return par[P_MODEL]; }

static void cycle_chord(int k, uint16_t *m, uint8_t *b)  /* chord k of the cycle as this section plays it */
{
    int t = par[P_ROOT] + shift;
    uint16_t rm = k == var_k ? var_mask : tpl_mask[k];
    uint8_t rb = k == var_k ? var_bass : tpl_bass[k];
    *m = rot12(rm, t);
    *b = (uint8_t)((((int)rb + t) % 12 + 12) % 12);
}

static void cycle_publish(void)
{
    int k;
    for (k = 0; k < tpl_n; k++) {
        uint16_t m;
        uint8_t b;
        cycle_chord(k, &m, &b);
        en_cyc_mask[k] = m;
        en_cyc_bass[k] = b;
    }
    en_cyc_n = (uint8_t)tpl_n;
    en_shift = (int8_t)shift;
}

static void cycle_load(void)                    /* the Cycle's chords (the library's, Mine, or Rolled) */
{
    int c = par[P_CYCLE], s = par[P_SEED_CYC], k;
    if (c == EN_CYC_MINE && mine_n < 1)
        c = 0;
    tpl_n = en_cycle_get(c, s, -1, 0, 0);
    for (k = 0; k < tpl_n; k++)
        en_cycle_get(c, s, k, &tpl_mask[k], &tpl_bass[k]);
    tpl_minor = en_cycle_minor(c, s);
    var_k = -1;
    shift = 0;
    if (cyc_pos >= tpl_n)
        cyc_pos = 0;
    cycle_publish();
}

static int triad_kind(uint32_t m, int *root)    /* 1 a major triad, 2 a minor one, 0 neither */
{
    int r;
    for (r = 0; r < 12; r++) {
        if (rot12(QUAL[Q_MAJ], r) == m) {
            *root = r;
            return 1;
        }
        if (rot12(QUAL[Q_MIN], r) == m) {
            *root = r;
            return 2;
        }
    }
    return 0;
}

/* a section's harmony: maybe one chord (never the first) gives way to a neighbour a third away, sharing
 * two of its tones; maybe the whole cycle moves a third for the section, coming home the next */
static void cycle_vary(void)
{
    int k, r, kind;
    var_k = -1;
    if (tpl_n >= 2 && frand() < evolve * 0.35f) {
        k = 1 + irand(tpl_n - 1);
        kind = triad_kind(tpl_mask[k], &r);
        if (kind) {
            int nr = kind == 1 ? (frand() < 0.5f ? r + 9 : r + 4) : (frand() < 0.5f ? r + 3 : r + 8);
            var_mask = rot12(QUAL[kind == 1 ? Q_MIN : Q_MAJ], nr);
            var_bass = (uint8_t)(nr % 12);
            var_k = k;
        }
    }
    if (shift)
        shift = 0;
    else if (frand() < evolve * 0.12f) {
        static const int8_t S[4] = {3, -3, 4, -4};
        shift = S[irand(4)];
    }
    cycle_publish();
}

static int deg_semis(int deg)                   /* scale degree (any, 0 the key's root) to semitones */
{
    int o = deg >= 0 ? deg / 7 : -((6 - deg) / 7);
    return SCALE[tpl_minor != 0][deg - 7 * o] + 12 * o;
}

static int key_root(void) { return ((par[P_ROOT] + shift) % 12 + 12) % 12; }

/* ------------------------------------------------------- the arpeggios --- */
typedef struct {
    uint8_t fig, full, len, pos, proc, reps, rep_n, div, user, flip, rot, smax;
    int8_t vpend;                               /* a process by hand, waiting for its pass to start (-1: none) */
    int8_t s[EN_FIG_MAX];                       /* the figure's steps */
    int start, tones[16];                       /* the chord's tones from the figure's lowest, up */
} arp_t;
static arp_t arp[2];

/* PHASE: the lower's lead over the upper, in steps; locked a while at each whole step */
static float ph_phi;
static uint8_t ph_lock;
static uint32_t ph_left;                        /* the lower's steps left locked */

static int seed_of(int p) { return par[p == PT_UPPER ? P_SEED_UP : P_SEED_LO]; }

static void arp_load(int p)                     /* the figure's steps (a rolled one from its seed) */
{
    arp_t *a = &arp[p];
    int i;
    a->full = (uint8_t)en_figure(a->fig, seed_of(p), a->s);
    a->smax = 0;
    for (i = 0; i < a->full; i++)
        if (a->s[i] > (int8_t)a->smax)
            a->smax = (uint8_t)a->s[i];
    if (!a->proc || a->len > a->full || a->len < 1)
        a->len = a->full;
    if (a->pos >= a->len)
        a->pos = 0;
}

static int tone_near(int from, uint32_t m, int lo, int hi)   /* the chord tone nearest from, in lo..hi */
{
    int x, best = lo, bd = 1000;
    for (x = lo; x <= hi; x++)
        if (m >> (x % 12) & 1u) {
            int d = x > from ? x - from : from - x;
            if (d < bd) {
                bd = d;
                best = x;
            }
        }
    return best;
}

/* the upper's figure starts on the chord tone nearest where it started before, around middle C (a
 * smooth top line); the lower's on the bass's class, near where it was, so its line moves by step */
static void arp_tones(int p)
{
    arp_t *a = &arp[p];
    int lo, n = 0, x;
    if (p == PT_UPPER) {
        lo = 57 + 12 * par[P_UP_OCT];
        a->start = tone_near(a->start, cur_mask, lo, lo + 11);
    } else {
        lo = 40;
        a->start = nearest(a->start, cur_bass, lo, lo + 14);
    }
    for (x = a->start; n < 16; x++)
        if (cur_mask >> (x % 12) & 1u)
            a->tones[n++] = x;
}

static void arp_publish(int p)
{
    en_fig_len[p] = arp[p].len;
    en_fig_full[p] = arp[p].full;
    en_fig_pos[p] = arp[p].pos;
    en_fig_proc[p] = arp[p].proc;
    en_fig_user[p] = arp[p].user;
    en_part_div[p] = arp[p].div;
}

/* a section's figure: the home one (the Figure), or with Evolve another for the section; with Process
 * the additive process: from 2 notes up to the whole figure, each length a few times, and back. A
 * process by hand holds the figure as it is */
static void arp_section(int p)
{
    arp_t *a = &arp[p];
    if (a->user)
        return;
    a->fig = (uint8_t)par[p == PT_UPPER ? P_UP_FIGURE : P_LO_FIGURE];
    if (model() != M_PHASE && frand() < evolve * 0.3f)
        a->fig = (uint8_t)irand(EN_NFIG);
    a->proc = 0;
    a->rot = a->flip = 0;
    arp_load(p);
    a->pos = 0;
    a->rep_n = 0;
    a->reps = (uint8_t)(4 - (int)(2.0f * process + 0.5f));
    if (model() != M_PHASE && a->full >= 3 && frand() < (process >= 0.99f ? 1.0f : process * 0.85f)) {
        if (frand() < 0.7f) {
            a->proc = 1;
            a->len = 2;
        } else {
            a->proc = 2;
            a->len = a->full;
        }
    }
    arp_publish(p);
}

static void arp_pass(int p)                     /* a pass of the figure ended: the process moves on */
{
    arp_t *a = &arp[p];
    if (!a->proc || ++a->rep_n < a->reps)
        return;
    a->rep_n = 0;
    if (a->proc == 1) {
        if (a->len < a->full)
            a->len++;
        else {
            a->proc = 2;
            a->len--;
        }
    } else if (a->len > 2) {
        a->len--;
    } else {
        a->proc = 1;
        a->len++;
    }
}

static float step_secs(int div) { return tick_len * (float)(EN_PPQN / div) * (1.0f / EN_SR); }

static uint32_t ph_lock_steps(void)             /* how long the phase stays locked: Evolve shortens it */
{
    arp_t *a = &arp[PT_LOWER];
    return (uint32_t)a->full * (1u + (uint32_t)((1.0f - evolve) * 6.0f * (0.7f + 0.6f * frand())));
}

/* a process by hand, at the start of a pass: GROW from two notes, SHRINK to two, SHIFT the figure a step
 * (in PHASE the lower a step ahead), FLIP it upside down, STILL: the figure as it was */
static void arp_verb(int p, int v)
{
    arp_t *a = &arp[p];
    switch (v) {
    case V_GROW:
        a->user = 1;
        a->rep_n = 0;
        if (a->full >= 3) {
            a->proc = 1;
            a->len = 2;
        }
        break;
    case V_SHRINK:
        a->user = 1;
        a->rep_n = 0;
        if (a->full >= 3) {
            a->proc = 2;
            if (a->len < 3)
                a->len = a->full;
        }
        break;
    case V_SHIFT:
        if (model() == M_PHASE && p == PT_LOWER) {
            ph_phi = fm_floorf(ph_phi) + 1.0f;
            if (ph_phi >= (float)a->len)
                ph_phi -= (float)a->len;
            ph_lock = 1;
            ph_left = ph_lock_steps();
        } else {
            a->user = 1;
            a->rot = (uint8_t)((a->rot + 1) % EN_FIG_MAX);
        }
        break;
    case V_FLIP:
        a->user = 1;
        a->flip ^= 1;
        break;
    default:                                     /* STILL */
        a->user = 0;
        a->proc = 0;
        a->rot = a->flip = 0;
        a->len = a->full;
        break;
    }
    arp_publish(p);
}

static int arp_idx(const arp_t *a, int k)       /* step k of the figure as it now plays: turned, mirrored */
{
    int len = a->len ? a->len : 1, i = a->s[(k + a->rot) % len];
    return a->flip ? a->smax - i : i;
}

static void arp_note(int p, int idx, float vel, int32_t wait)
{
    arp_t *a = &arp[p];
    int inst = part_inst[p];
    float st = step_secs(a->div), gf = modal(inst) ? 1.0f : inst == I_ORGAN ? 0.85f : inst == I_FLUTE ? 0.8f : 0.9f;
    voice_t *v = note_on(p, a->tones[idx < 16 ? (idx < 0 ? 0 : idx) : 15], vel, (int32_t)(st * gf * EN_SR),
                         inst == I_STRINGS ? fm_minf(0.22f, 0.35f * st) : 0.0f);
    if (v) {
        v->wait = wait;
        if (!modal(inst))                        /* a quick note's tail: no longer than half its step */
            v->rel = fm_minf(v->rel, 0.5f * st);
    }
}

static void arp_step(int p)
{
    arp_t *a = &arp[p];
    float vel;
    if (a->pos == 0 && a->vpend >= 0) {
        arp_verb(p, a->vpend);
        a->vpend = -1;
    }
    vel = (a->pos == 0 ? 0.95f : 0.74f) + 0.06f * frand();
    if (p == PT_LOWER)
        vel *= 0.9f;
    arp_note(p, arp_idx(a, a->pos), vel, 0);
    if (++a->pos >= a->len) {
        a->pos = 0;
        arp_pass(p);
    }
    arp_publish(p);
}

/* ------------------------------------------------------------- PHASE --- */
/* The lower plays its figure on the same grid as the upper but ph_phi steps ahead: at each of its steps
 * it plays the figure's next note early by the fraction (a voice that waits the rest of the step), so
 * the drift is smooth. It drifts (PROCESS: from a step in 25 bars to a step in 2), locks in at the
 * next whole step for a while (EVOLVE: shorter), and drifts on, round to unison */
static float ph_speed(void) { return process <= 0.0f ? 0.0f : (0.04f + 0.5f * process) * (1.0f / (4.0f * (float)EN_PPQN)); }

static void phase_drift(void)                   /* a tick */
{
    arp_t *a = &arp[PT_LOWER];
    float len = (float)(a->len ? a->len : 1), before;
    if (!ph_lock) {
        before = fm_floorf(ph_phi);
        ph_phi += ph_speed();
        if (fm_floorf(ph_phi) > before) {        /* a whole step ahead: it locks in */
            ph_phi = before + 1.0f;
            if (ph_phi >= len)
                ph_phi -= len;
            ph_lock = 1;
            ph_left = ph_lock_steps();
        }
    }
    en_phase = ph_phi;
}

static void phase_step(void)                    /* the lower's step in PHASE */
{
    arp_t *a = &arp[PT_LOWER];
    int n = (int)ph_phi, extra, len, k;
    float f = ph_phi - (float)n, vel;
    int32_t wait = 0;
    if (a->pos == 0 && a->vpend >= 0) {
        arp_verb(PT_LOWER, a->vpend);
        a->vpend = -1;
    }
    len = a->len ? a->len : 1;
    extra = f > 0.01f;
    if (extra)                                   /* between steps: the next note, early by f */
        wait = (int32_t)((1.0f - f) * step_secs(a->div) * EN_SR);
    k = (a->pos + n + extra) % len;
    vel = (k == 0 ? 0.9f : 0.72f) + 0.06f * frand();
    arp_note(PT_LOWER, arp_idx(a, k - (a->rot % len)), vel * 0.95f, wait);
    if (++a->pos >= a->len) {
        a->pos = 0;
        arp_pass(PT_LOWER);
    }
    if (ph_lock && ph_left && --ph_left == 0u)
        ph_lock = 0;
    arp_publish(PT_LOWER);
}

/* -------------------------------------------------------------- the bass --- */
static int bass_note = 40, bass_fifth = 47;

static void bass_notes(void)                     /* the bass line moves to the nearest note of the new bass */
{
    int x, o = 12 * par[P_BS_OCT];
    bass_note = nearest(bass_note - o, cur_bass, 33, 47) + o;
    bass_fifth = bass_note + 12;
    for (x = bass_note + 5; x <= bass_note + 9; x++)
        if (cur_mask >> (x % 12) & 1u) {
            bass_fifth = x;
            break;
        }
}

static void bass_play(int note, float vel, uint32_t len, float gf)
{
    note_on(PT_BASS, note, vel, (int32_t)((float)len * tick_len * gf), 0.0f);
}

static void bass_tick(void)
{
    switch (par[P_BS_STYLE]) {
    case 0:                                      /* long: a note a chord */
        if (ctick == 0)
            bass_play(bass_note, 0.9f, chord_ticks, 0.97f);
        break;
    case 1:                                      /* a pulse of eighths */
        if (ticks % 12u == 0u)
            bass_play(bass_note, ticks % 24u ? 0.7f : 0.9f, 12, 0.6f);
        break;
    case 2:                                      /* octaves in eighths */
        if (ticks % 12u == 0u)
            bass_play(bass_note + (int)((ticks / 12u) & 1u) * 12, ticks % 24u ? 0.75f : 0.9f, 12, 0.55f);
        break;
    default:                                     /* root and fifth, half notes */
        if (ticks % 48u == 0u)
            bass_play((ticks / 48u) & 1u ? bass_fifth : bass_note, 0.85f, 48, 0.9f);
        break;
    }
}

/* -------------------------------------------------------------- the lead --- */
/* ARPEGGIO, PHASE: a line over the whole cycle: on each chord, a rest, one note, two or four (Activity),
 * each a chord tone a step along the line's way from the last; the same note across a chord change is
 * tied. It is made from the Line idea (a seed), so the same idea gives the same line, each time round.
 * BELLS: the melody: phrases from the key's root by step, each a note longer than the last (to Phrases,
 * then from one again), up and down by turns, a breath between; the upper answers each note */
#define LD_MAX 48
static struct {
    uint16_t at, len;
    uint8_t note;
} ld[LD_MAX];
static int ld_n, ld_i, ld_x = 69, ld_dir = 1;

static int tone_step(uint32_t m, int x, int dir)   /* the next chord tone from x, up or down, in 62..81 */
{
    int y;
    for (y = x + dir; y >= 62 && y <= 81; y += dir)
        if (m >> (y % 12) & 1u)
            return y;
    for (y = x - dir; y >= 62 && y <= 81; y -= dir)  /* the edge: back the other way */
        if (m >> (y % 12) & 1u) {
            ld_dir = -dir;
            return y;
        }
    return x;
}

static void ld_add(int at, int len, int note)
{
    if (ld_n && ld[ld_n - 1].note == note && ld[ld_n - 1].at + ld[ld_n - 1].len == at)
        ld[ld_n - 1].len = (uint16_t)(ld[ld_n - 1].len + len);   /* tied over */
    else if (ld_n < LD_MAX) {
        ld[ld_n].at = (uint16_t)at;
        ld[ld_n].len = (uint16_t)len;
        ld[ld_n].note = (uint8_t)note;
        ld_n++;
    }
}

static void lead_compose(void)
{
    const float act = (float)par[P_LD_ACT] * 0.01f;
    uint32_t r = lr_seed((uint32_t)par[P_SEED_LD]);
    int k, i, beats = CHORD_BEATS[par[P_CHORDLEN]];
    ld_n = 0;
    ld_x = 69;
    ld_dir = 1;
    for (k = 0; k < tpl_n; k++) {
        uint16_t m;
        uint8_t b;
        float x = lr_float(&r);
        int cnt;
        cycle_chord(k, &m, &b);
        if (x < 0.28f * (1.0f - act))
            cnt = 0;
        else if (x < 0.78f - 0.38f * act)
            cnt = 1;
        else if (x < 0.96f - 0.2f * act)
            cnt = 2;
        else
            cnt = 4;
        if (cnt > beats)
            cnt = beats;
        for (i = 0; i < cnt; i++) {
            int len = (int)chord_ticks / cnt, at = k * (int)chord_ticks + i * len, y;
            if ((m >> (ld_x % 12) & 1u) && lr_float(&r) < 0.3f) {
                y = ld_x;                        /* it stays */
            } else {
                if (lr_float(&r) < 0.2f)
                    ld_dir = -ld_dir;
                y = tone_step(m, ld_x, ld_dir);
            }
            ld_add(at, len, y);
            ld_x = y;
        }
    }
    ld_i = 0;
}

static void bells_compose(void)
{
    const float act = (float)par[P_LD_ACT] * 0.01f;
    uint32_t r = lr_seed((uint32_t)par[P_SEED_LD] + 31u);
    int total = tpl_n * (int)chord_ticks, t = 0, plen = 1, dir = (lr_next(&r) & 1u) ? 1 : -1;
    int maxlen = 2 + (int)(process * 6.0f + 0.5f), pivot = nearest(69, key_root(), 62, 73);
    ld_n = 0;
    while (t < total && ld_n < LD_MAX) {
        int i;
        for (i = 0; i < plen && t < total; i++) {
            int last = i == plen - 1, dur = last ? (act < 0.5f ? 72 : 48) : (act < 0.5f ? 48 : 24);
            if (t + dur > total)
                dur = total - t;
            ld_add(t, dur, pivot + deg_semis(dir * i));
            t += dur;
        }
        t += lr_float(&r) < 0.5f ? 24 : 48;      /* a breath */
        if (++plen > maxlen)
            plen = 1;
        dir = -dir;
    }
    ld_i = 0;
}

static int snap(int x, uint32_t m)              /* the chord tone nearest x (a little lower first) */
{
    int d;
    if (m >> (x % 12) & 1u)
        return x;
    for (d = 1; d < 7; d++) {
        if (m >> ((x - d) % 12) & 1u)
            return x - d;
        if (m >> ((x + d) % 12) & 1u)
            return x + d;
    }
    return x;
}

/* BELLS: the chord's triad, and the bell for a note: the nearest triad tone above it, below it, by
 * turns, or the second above */
static uint32_t triad_mask(void)
{
    int b = cur_bass, third = (cur_mask >> ((b + 4) % 12) & 1u) ? 4 : (cur_mask >> ((b + 3) % 12) & 1u) ? 3 : 4;
    int fifth = (cur_mask >> ((b + 7) % 12) & 1u) ? 7 : (cur_mask >> ((b + 6) % 12) & 1u) ? 6
                : (cur_mask >> ((b + 8) % 12) & 1u) ? 8 : 7;
    return 1u << b | 1u << ((b + third) % 12) | 1u << ((b + fifth) % 12);
}

static int bell_for(int x, int k)
{
    uint32_t m = triad_mask();
    int pos = par[P_UP_FIGURE] % 4, y, up = pos == 0 || pos == 3 || (pos == 2 && !(k & 1)), skip = pos == 3;
    if (up) {
        for (y = x + 1; y < x + 13; y++)
            if (m >> (y % 12) & 1u) {
                if (!skip)
                    return y;
                skip = 0;
            }
    } else {
        for (y = x - 1; y > x - 13; y--)
            if (m >> (y % 12) & 1u)
                return y;
    }
    return x;
}

static void lead_tick(void)
{
    uint32_t at = (uint32_t)cyc_pos * chord_ticks + ctick;
    int bells = model() == M_BELLS;
    if (at == 0u)
        ld_i = 0;
    while (ld_i < ld_n && (uint32_t)ld[ld_i].at < at)
        ld_i++;
    while (ld_i < ld_n && (uint32_t)ld[ld_i].at == at) {
        int x = bells ? ld[ld_i].note : snap(ld[ld_i].note, cur_mask), h = par[P_LD_HARM];
        int32_t gate = (int32_t)((float)ld[ld_i].len * tick_len * 0.96f);
        float vel = 0.8f + 0.1f * frand();
        if (en_part_on[PT_LEAD]) {
            note_on(PT_LEAD, x, vel, gate, 0.0f);
            if (h == 1) {                        /* a second voice, the chord tone below */
                int y;
                for (y = x - 3; y > x - 10; y--)
                    if (cur_mask >> (y % 12) & 1u)
                        break;
                note_on(PT_LEAD, y, vel * 0.8f, gate, 0.0f);
            } else if (h == 2) {
                note_on(PT_LEAD, x - 12, vel * 0.8f, gate, 0.0f);
            }
        }
        if (bells && en_part_on[PT_UPPER])       /* the bell answers */
            note_on(PT_UPPER, bell_for(x, ld_i), vel * 0.8f, gate, 0.0f);
        ld_i++;
    }
}

/* ----------------------------------------------------------- PATTERNS --- */
/* A chain of PAT_N short patterns, made from the Line idea: the first a note or three about the root,
 * each next the last a little changed (a note more or less, one moved a step, one longer or shorter, a
 * breath). Upper, lower and lead each play their pattern (scale degrees from the chord's root, in their
 * own register, at their pulse) as many times as they like (MOVE ON: fewer), then move on, never more
 * than SPREAD ahead of the slowest; when the last has played the last pattern all start over */
#define PAT_N 16
#define PAT_NOTES 8
#define PAT_REST 127
typedef struct {
    uint8_t n;
    int8_t deg[PAT_NOTES];
    uint8_t dur[PAT_NOTES];                      /* in units of the player's pulse */
} pat_t;
static pat_t pats[PAT_N];
static struct {
    uint8_t at, pos, reps;
    uint32_t next;                               /* the tick of its next note */
} pl[3];
static const uint8_t PL_PART[3] = {PT_UPPER, PT_LOWER, PT_LEAD};
static const uint8_t PL_BASE[3] = {74, 50, 62}; /* where each plays: high, low, the middle */

static void pats_make(void)
{
    uint32_t r = lr_seed((uint32_t)par[P_SEED_LD] + 77u);
    int i, k;
    pat_t *p = &pats[0];
    p->n = (uint8_t)(1 + lr_next(&r) % 3u);
    for (k = 0; k < p->n; k++) {
        p->deg[k] = (int8_t)(k == 0 ? 0 : (int)(lr_next(&r) % 5u) - 1);
        p->dur[k] = (uint8_t)(1 + lr_next(&r) % 2u);
    }
    for (i = 1; i < PAT_N; i++) {
        pat_t *t = &pats[i];
        int m, changes = 1 + (int)(lr_next(&r) % 2u), tot, sounds = 0;
        *t = pats[i - 1];
        for (m = 0; m < changes; m++) {
            uint32_t op = lr_next(&r) % 10u;
            int at = (int)(lr_next(&r) % t->n);
            if (op < 3 && t->n < PAT_NOTES) {    /* a note more, a step or two from the last */
                int last = t->deg[t->n - 1] == PAT_REST ? 0 : t->deg[t->n - 1];
                t->deg[t->n] = (int8_t)(last + (int)(lr_next(&r) % 5u) - 2);
                t->dur[t->n] = (uint8_t)(1 + lr_next(&r) % 2u);
                t->n++;
            } else if (op < 5 && t->n > 1) {     /* a note less */
                for (k = at; k < t->n - 1; k++) {
                    t->deg[k] = t->deg[k + 1];
                    t->dur[k] = t->dur[k + 1];
                }
                t->n--;
            } else if (op < 8) {                 /* a note moves */
                if (t->deg[at] != PAT_REST)
                    t->deg[at] = (int8_t)(t->deg[at] + ((lr_next(&r) & 1u) ? 1 : -1) * (1 + (int)(lr_next(&r) % 2u)));
            } else if (op < 9) {                 /* a note longer or shorter */
                t->dur[at] = (uint8_t)(t->dur[at] == 1 ? 2 : t->dur[at] >= 3 ? 1 : 3);
            } else if (t->n < PAT_NOTES) {       /* a breath */
                t->deg[t->n] = PAT_REST;
                t->dur[t->n] = (uint8_t)(1 + lr_next(&r) % 2u);
                t->n++;
            }
        }
        for (k = 0, tot = 0; k < t->n; k++) {    /* within -4..+9, at most 12 units, one note at least */
            if (t->deg[k] != PAT_REST) {
                t->deg[k] = (int8_t)(t->deg[k] < -4 ? -4 : t->deg[k] > 9 ? 9 : t->deg[k]);
                sounds++;
            }
            tot += t->dur[k];
        }
        while (tot > 12 && t->n > 1)
            tot -= t->dur[--t->n];
        if (!sounds)
            t->deg[0] = 0;
    }
    en_pat_n = PAT_N;
}

static int pat_reps(void) { return 2 + irand(2 + (int)((1.0f - evolve) * 8.0f)); }

static void players_reset(void)
{
    int j;
    for (j = 0; j < 3; j++) {
        int s = j == 0 ? par[P_UP_FIGURE] : j == 1 ? par[P_LO_FIGURE] : 0;
        pl[j].at = (uint8_t)(s < PAT_N ? s : PAT_N - 1);
        pl[j].pos = 0;
        pl[j].reps = (uint8_t)pat_reps();
        pl[j].next = ticks;
        en_pat_at[PL_PART[j]] = pl[j].at;
    }
}

static void player_tick(int j)
{
    int p = PL_PART[j], unit, k, lowest = PAT_N, all_end = 1;
    const pat_t *t = &pats[pl[j].at];
    if (!en_part_on[p] || ticks < pl[j].next)
        return;
    unit = EN_PPQN / (j == 2 ? (par[P_LD_ACT] >= 50 ? 2 : 1) : arp[j].div);
    {
        int d = t->deg[pl[j].pos], len = t->dur[pl[j].pos] * unit;
        if (d != PAT_REST) {
            int rn = nearest(PL_BASE[j], cur_bass, PL_BASE[j] - 6, PL_BASE[j] + 5);
            voice_t *v = note_on(p, rn + deg_semis(d), pl[j].pos == 0 ? 0.85f : 0.7f + 0.08f * frand(),
                                 (int32_t)((float)len * tick_len * 0.9f), 0.0f);
            if (v && !modal(v->inst))
                v->rel = fm_minf(v->rel, 0.4f * (float)len * tick_len * (1.0f / EN_SR));
        }
        pl[j].next = ticks + (uint32_t)len;
    }
    if (++pl[j].pos < t->n)
        return;
    pl[j].pos = 0;                               /* a pass: again, or on */
    if (pl[j].reps > 1) {
        pl[j].reps--;
        return;
    }
    for (k = 0; k < 3; k++)
        if (en_part_on[PL_PART[k]]) {
            if (pl[k].at < lowest)
                lowest = pl[k].at;
            if (pl[k].at < PAT_N - 1)
                all_end = 0;
        }
    if (all_end) {                               /* all at the last: all start over */
        players_reset();
        return;
    }
    if (pl[j].at + 1 < PAT_N && pl[j].at - lowest < 1 + (int)(process * 4.0f + 0.5f)) {
        pl[j].at++;
        pl[j].reps = (uint8_t)pat_reps();
    } else {
        pl[j].reps = 1;                          /* the others are too far behind: once more */
    }
    en_pat_at[p] = pl[j].at;
}

static void line_compose(void)                  /* the lead's line, the bells' melody, or the patterns */
{
    if (model() == M_PATTERNS)
        pats_make();
    else if (model() == M_BELLS)
        bells_compose();
    else
        lead_compose();
}

/* ------------------------------------------------------------ the chords --- */
static int chn[4] = {57, 61, 65, 69};          /* the four voices' notes */
static int8_t chv[4] = {-1, -1, -1, -1};       /* their voices, while held */

static void chords_free(void)                   /* the held chord lets go */
{
    int i;
    for (i = 0; i < 4; i++) {
        if (chv[i] >= 0 && vc[chv[i]].on && vc[chv[i]].part == PT_CHORDS && vc[chv[i]].note == chn[i])
            voice_release(&vc[chv[i]]);
        chv[i] = -1;
    }
}

static void chords_voicing(int *to)
{
    int pcs[4], n = 0, i;
    for (i = 0; i < 12 && n < 4; i++) {          /* the bass's class first, then up */
        int pc = (cur_bass + i) % 12;
        if (cur_mask >> pc & 1u)
            pcs[n++] = pc;
    }
    en_voice_lead(chn, pcs, n, to, 52, 74);
}

static void chords_strike(float vel, int32_t gate, float att, int keep)   /* all four (keep: held notes stay) */
{
    int to[4], i;
    chords_voicing(to);
    for (i = 0; i < 4; i++) {
        int same = to[i] == chn[i] && chv[i] >= 0 && vc[chv[i]].on && vc[chv[i]].note == to[i] && vc[chv[i]].stage != ST_REL;
        if (keep && same)
            continue;
        if (chv[i] >= 0 && vc[chv[i]].on && vc[chv[i]].part == PT_CHORDS && vc[chv[i]].note == chn[i])
            voice_release(&vc[chv[i]]);
        chn[i] = to[i];
        {
            voice_t *v = note_on(PT_CHORDS, to[i], vel, gate, att);
            chv[i] = (int8_t)(v ? v - vc : -1);
            if (v && gate > 0 && !modal(v->inst))    /* pulsed: the tail a little of the pulse */
                v->rel = fm_minf(v->rel, 0.4f * (float)gate * (1.0f / EN_SR));
        }
    }
}

/* a chord change (mid: not on the cycle's beat, the player's): the chords follow by their rhythm */
static void chords_change(int mid)
{
    int r = par[P_CH_RHYTHM];
    if (!en_part_on[PT_CHORDS])
        return;
    if (r == 0) {
        chords_strike(0.8f, -1, 0.0f, 1);
    } else if (r == 1) {                         /* swell: each chord grows to its end */
        uint32_t left = chord_ticks - (mid ? ctick : 0u);
        float secs = (float)left * tick_len * (1.0f / EN_SR);
        chords_strike(0.85f, (int32_t)((float)left * tick_len * 0.98f), 0.7f * secs, 0);
    }
}

static void chords_tick(void)
{
    int r = par[P_CH_RHYTHM];
    if (r == 2 && ticks % 12u == 0u) {
        chords_strike(ticks % 24u ? 0.62f : 0.75f, (int32_t)(12.0f * tick_len * 0.5f), 0.0f, 0);
    } else if (r == 3 && ticks % 24u == 0u) {
        chords_strike(0.75f, (int32_t)(24.0f * tick_len * 0.75f), 0.0f, 0);
    } else if (r == 4 && ticks % 12u == 0u) {    /* breath: eighths swelling and fading over two bars */
        float s = fm_sinf(FM_PI * (float)(ticks % 192u) * (1.0f / 192.0f));
        chords_strike(0.2f + 0.65f * s * s, (int32_t)(12.0f * tick_len * 0.6f), 0.0f, 0);
    } else if (r == 5 && ticks % 12u == 0u) {    /* a pulse: the chord's root, high, in eighths */
        voice_t *v;
        chords_free();
        v = note_on(PT_CHORDS, nearest(84, cur_bass, 79, 90), ticks % 24u ? 0.85f : 1.0f,   /* one voice: louder */
                    (int32_t)(12.0f * tick_len * 0.5f), 0.0f);
        if (v && !modal(v->inst))
            v->rel = fm_minf(v->rel, 0.1f);
    } else if (r == 0 && chv[0] < 0) {           /* held, and not sounding yet (the part came in) */
        chords_strike(0.8f, -1, 0.0f, 1);
    }
}

/* ------------------------------------------------------- the chord, the form --- */
static void chord_set(uint16_t m, uint8_t b, int mid)
{
    cur_mask = (uint16_t)(m | 1u << b);
    cur_bass = b;
    en_chord_mask = cur_mask;
    en_chord_bass = b;
    en_yours = yours;
    arp_tones(PT_UPPER);
    arp_tones(PT_LOWER);
    bass_notes();
    pedal(cur_mask);
    chords_change(mid);
    if (mid && en_part_on[PT_BASS] && par[P_BS_STYLE] == 0)   /* a long bass moves now */
        bass_play(bass_note, 0.85f, chord_ticks - ctick, 0.97f);
}

static void chord_begin(void)
{
    uint16_t m;
    uint8_t b;
    chord_dirty = 0;
    if (yours) {
        m = held_mask;
        b = held_bass;
    } else {
        cycle_chord(cyc_pos, &m, &b);
    }
    chord_set(m, b, 0);
    en_cyc_pos = (uint8_t)cyc_pos;
}

static int level_of(int p) { return par[LEVEL_PAR[p]]; }

static void parts_publish(void)
{
    int p;
    for (p = 0; p < EN_NPART; p++)
        en_part_on[p] = (uint8_t)(present[p] && level_of(p) > 0);
    if (!en_part_on[PT_CHORDS])
        chords_free();
}

/* BUILD: the parts in and out, one a section, toward the arc's count (in the order an ensemble piece
 * builds: the lower ostinato, the upper, the line over them, then the bass and the chords); returns how
 * many more the arc wants. Build at 0: by hand (all in at the start, then as the player leaves them) */
static int presence_update(int first)
{
    static const uint8_t ORDER[EN_NPART] = {PT_LOWER, PT_UPPER, PT_LEAD, PT_BASS, PT_CHORDS};
    int elig = 0, on = 0, target, i, p;
    for (i = 0; i < EN_NPART; i++) {
        p = ORDER[i];
        if (level_of(p) > 0) {
            elig++;
            on += present[p];
        }
    }
    target = elig;
    if (build <= 0.0f || !elig) {
        if (first)
            for (p = 0; p < EN_NPART; p++)
                present[p] = (uint8_t)(level_of(p) > 0);
        target = 0;
    } else {
        float frac = 1.0f - build * (1.0f - arc) * 0.5f;
        target = (int)((float)elig * frac + 0.5f);
        if (target < 1)
            target = 1;
        if (first) {
            int c = 0;
            for (i = 0; i < EN_NPART; i++) {
                p = ORDER[i];
                present[p] = (uint8_t)(level_of(p) > 0 && c < target);
                c += present[p];
            }
        } else if (on < target) {
            for (i = 0; i < EN_NPART; i++) {
                p = ORDER[i];
                if (level_of(p) > 0 && !present[p]) {
                    present[p] = 1;
                    break;
                }
            }
        } else if (on > target) {
            for (i = EN_NPART - 1; i >= 0; i--) {
                p = ORDER[i];
                if (present[p] && level_of(p) > 0) {
                    present[p] = 0;
                    break;
                }
            }
        }
    }
    parts_publish();
    for (on = 0, p = 0; p < EN_NPART; p++)
        on += en_part_on[p];
    return target - on;
}

static void section_begin(int first)
{
    int p, cyc_beats, reps, building;
    float bars, u;
    en_section++;
    cyc_rep = 0;
    if (first) {
        var_k = -1;
        shift = 0;
        cycle_publish();
    } else {
        cycle_vary();
    }
    building = presence_update(first) > 0;
    for (p = PT_UPPER; p <= PT_LOWER; p++)
        arp_section(p);
    if (first || var_k >= 0 || shift)            /* the chords moved: the line again (from its idea) */
        line_compose();
    u = fm_clampf(0.5f + 0.45f * arc + 0.5f * (frand() - 0.5f), 0.0f, 1.0f);
    terr = 0.68f + 0.32f * u;                    /* the section's dynamic, a terrace: p .. f */
    cyc_beats = tpl_n * CHORD_BEATS[par[P_CHORDLEN]];
    bars = (6.0f + 26.0f * (1.0f - evolve)) * (0.7f + 0.6f * frand());
    if (building)                                /* still building: a part every 8 bars or so */
        bars = 8.0f;
    reps = (int)(bars * 4.0f / (float)cyc_beats + 0.5f);
    cyc_reps = reps < 1 ? 1 : reps > 12 ? 12 : reps;
    en_cyc_rep = 0;
    en_cyc_reps = (uint8_t)cyc_reps;
}

static void chord_end(void)
{
    int p, moved = 0;
    if (!keys_down)
        yours = 0;                               /* let go: the cycle again, from this chord on */
    for (p = 0; p < EN_NPART; p++)               /* parts in or out by hand, on the chord */
        if (part_req[p]) {
            present[p] = (uint8_t)(part_req[p] - 1);
            part_req[p] = 0;
            moved = 1;
        }
    if (moved)
        parts_publish();
    if (next_pending) {
        next_pending = 0;
        cyc_pos = 0;
        section_begin(0);
        return;
    }
    if (++cyc_pos >= tpl_n) {
        cyc_pos = 0;
        if (++cyc_rep >= cyc_reps)
            section_begin(0);
        else
            en_cyc_rep = (uint8_t)cyc_rep;
    }
}

/* ------------------------------------------------------------- songs --- */
static en_song_t song EN_POOL;
static uint32_t song_i;
en_song_t *en_song(void) { return &song; }
const int16_t *en_params(void) { return par; }

static void music_reset(void)                   /* the music's own state, as at power-on (not the sound's) */
{
    int i;
    ld_x = 69;
    ld_dir = 1;
    ld_n = ld_i = 0;
    bass_note = 40;
    bass_fifth = 47;
    for (i = 0; i < 2; i++) {
        arp[i].user = arp[i].flip = arp[i].rot = 0;
        arp[i].proc = 0;
        arp[i].pos = 0;
        arp[i].len = 0;
        arp[i].vpend = -1;
    }
    arp[PT_UPPER].start = 62;
    arp[PT_LOWER].start = 45;
    for (i = 0; i < 4; i++) {
        chn[i] = 57 + 4 * i;
        chv[i] = -1;
    }
    held_mask = 0;
    held_bass = 0;
    keys_down = yours = chord_dirty = next_pending = 0;
    var_k = -1;
    shift = 0;
    ph_phi = 0.0f;
    ph_lock = 0;
    ph_left = 0;
    for (i = 0; i < EN_NPART; i++) {
        part_req[i] = 0;
        present[i] = 0;
    }
    cyc_pos = cyc_rep = 0;
    terr = 0.85f;
}

/* ------------------------------------------------------------ parameters --- */
static void gains_target(void)
{
    int p;
    for (p = 0; p < EN_NPART; p++) {
        float l = (float)level_of(p) * 0.01f;
        pg_t[p] = l * l * 1.6f * terr * PART_GAIN[p];
    }
}

static void arp_param(int p)                    /* the figure, its idea or the model changed: as set */
{
    arp_t *a = &arp[p];
    a->fig = (uint8_t)par[p == PT_UPPER ? P_UP_FIGURE : P_LO_FIGURE];
    a->user = a->proc = a->rot = a->flip = 0;
    a->len = 0;
    arp_load(p);
    a->pos = 0;
    arp_publish(p);
}

static void apply(int p, int v)
{
    const en_param_t *pi = &PARAMS[p];
    int i;
    if (v < pi->lo)
        v = pi->lo;
    if (v > pi->hi)
        v = pi->hi;
    par[p] = (int16_t)v;
    switch (p) {
    case P_EVOLVE: evolve = (float)v * 0.01f; break;
    case P_BUILD: build = (float)v * 0.01f; break;
    case P_PROCESS:
        process = (float)v * 0.01f;
        if (model() == M_BELLS)                  /* the phrases' length */
            line_compose();
        break;
    case P_CHORDLEN:
        chord_ticks = (uint32_t)CHORD_BEATS[v] * EN_PPQN;
        if (ctick >= chord_ticks)
            ctick = 0;
        line_compose();
        break;
    case P_UP_LEVEL: case P_LO_LEVEL: case P_BS_LEVEL: case P_LD_LEVEL: case P_CH_LEVEL:
        for (i = 0; i < EN_NPART; i++)
            if (LEVEL_PAR[i] == p && v > 0 && en_playing)
                present[i] = 1;                  /* turned up: it plays now */
        parts_publish();
        break;
    case P_UP_SOUND: case P_LO_SOUND: case P_BS_SOUND: case P_LD_SOUND: case P_CH_SOUND:
        for (i = 0; i < EN_NPART; i++)
            if (SOUND_PAR[i] == p)
                part_inst[i] = SOUNDS[i][v < NSOUNDS[i] ? v : 0];
        if (p == P_CH_SOUND)
            chords_free();
        break;
    case P_UP_PULSE: arp[PT_UPPER].div = PULSE_DIV[v]; arp_publish(PT_UPPER); break;
    case P_LO_PULSE: arp[PT_LOWER].div = PULSE_DIV[v]; arp_publish(PT_LOWER); break;
    case P_UP_FIGURE: arp_param(PT_UPPER); break;
    case P_LO_FIGURE: arp_param(PT_LOWER); break;
    case P_SEED_UP: case P_SEED_LO: {
        int part = p == P_SEED_UP ? PT_UPPER : PT_LOWER;
        if (arp[part].fig >= EN_NFIG)            /* the rolled figure, anew (a process by hand goes on) */
            arp_load(part);
        arp_publish(part);
        break;
    }
    case P_BS_OCT: bass_notes(); break;
    case P_LD_ACT: case P_SEED_LD: line_compose(); break;
    case P_CH_RHYTHM: chords_free(); break;
    case P_ROOM:
        rev_send = (float)(v * v) * 0.00012f;
        pl_decay_t = 0.45f + 0.45f * (float)v * 0.01f;
        rev_wet = 0.8f + 0.4f * (float)v * 0.01f;
        break;
    case P_TEMPO: tick_len = EN_SR * 60.0f / ((float)v * (float)EN_PPQN); break;
    case P_ROOT:
        cycle_publish();
        line_compose();
        chord_dirty = 1;
        break;
    case P_MIDI:
        if (!v)
            for (i = 0; i < EN_NPART; i++)
                en_midi_out(0xB0u | (uint32_t)i, 123, 0);
        break;
    case P_CYCLE: case P_SEED_CYC:
        if (p == P_CYCLE || par[P_CYCLE] == EN_CYC_ROLLED) {
            cycle_load();
            line_compose();
            chord_dirty = 1;
        }
        break;
    case P_UP_OCT: arp_tones(PT_UPPER); break;
    case P_MODEL:                                /* another model: the parts start their ways afresh */
        ph_phi = 0.0f;
        ph_lock = 0;
        arp_param(PT_UPPER);
        arp_param(PT_LOWER);
        chords_free();
        pats_make();
        line_compose();
        players_reset();
        break;
    default: break;
    }
    gains_target();
}

/* ------------------------------------------------------------ transport --- */
static void notes_off(void)
{
    int i;
    for (i = 0; i < EN_NPART; i++)
        midi(0xB0u | (uint32_t)i, 123, 0);
}

static void quiet(void)
{
    int i;
    for (i = 0; i < NV; i++)
        vc[i].on = 0;
    for (i = 0; i < 4; i++)
        chv[i] = -1;
    notes_off();
}

static void form_restart(void)                  /* the cycle from the top, a fresh form, building again */
{
    ticks = 0;
    ctick = 0;
    cyc_pos = 0;
    arc = -0.45f + 0.2f * frand();
    arc_tgt = 0.85f;
    arc_left = (uint32_t)(80.0f * EN_SR / tick_len);
    ph_phi = 0.0f;
    ph_lock = 0;
    section_begin(1);
    players_reset();
}

static void play_start(int from_top)
{
    if (from_top)
        form_restart();
    tick_left = 0.0f;
    en_playing = 1;
    if (!en_ext)
        midi(from_top ? 0xFA : 0xFB, 0, 0);
}

static void play_stop(void)
{
    int i;
    en_playing = 0;
    for (i = 0; i < NV; i++)                     /* the held ones let go; the piano rings on */
        if (vc[i].on && vc[i].gate < 0 && vc[i].stage != ST_REL)
            voice_release(&vc[i]);
    for (i = 0; i < 4; i++)
        chv[i] = -1;
    if (!en_ext)
        midi(0xFC, 0, 0);
    notes_off();
}

/* a take from the top: the dice as they were, the music's state as at power-on, every value as it was */
static void song_begin(void)
{
    int i;
    rng = song.seed ? song.seed : 1u;
    music_reset();
    mine_n = song.mine_n > EN_CYC_MAX ? EN_CYC_MAX : song.mine_n;
    for (i = 0; i < EN_CYC_MAX; i++) {
        mine_mask[i] = song.mine_mask[i];
        mine_bass[i] = song.mine_bass[i];
    }
    for (i = 0; i < P_NPARAMS; i++)
        if (i != P_MIDI && i != P_KEYS && i != P_HOLD)
            apply(i, song.par[i]);
    for (i = 0; i < 2; i++)
        arp_param(i);
    stick = 0;
    song_i = 0;
    en_song_tick = 0;
    form_restart();
    tick_left = 0.0f;
    en_playing = 1;
    if (!en_ext)
        midi(0xFA, 0, 0);
}

static int recordable(uint32_t c)
{
    uint32_t t = c >> 24, a = c >> 16 & 0xFFu;
    if (t == C_SET)
        return a != P_MIDI && a != P_KEYS && a != P_HOLD;
    return t == C_NEXT || t == C_SEED || t == C_REGROW || t == C_HOLD || t == C_MINE_CLR || t == C_MINE_ADD ||
           t == C_MINE_END || t == C_PART || t == C_VERB;
}

static void song_record(uint32_t c)
{
    en_song_t *s = &song;
    if (s->n && (c >> 24) == C_SET && (s->ev[s->n - 1][1] >> 16) == (c >> 16) && s->ev[s->n - 1][0] + 12u > stick) {
        s->ev[s->n - 1][1] = c;                  /* the same knob within half a beat: its last value */
        return;
    }
    if (s->n >= EN_SONG_MAX) {                   /* full: the take ends here */
        s->end = stick;
        en_song_state = SONG_IDLE;
        en_song_full = 1;
        return;
    }
    s->ev[s->n][0] = stick;
    s->ev[s->n][1] = c;
    s->n++;
}

static void exec(uint32_t c);

static void song_tick(void)                     /* playing a take: its commands due at this tick */
{
    while (song_i < song.n && song.ev[song_i][0] <= stick)
        exec(song.ev[song_i++][1]);
}

static void song_cmd(int what, int piece)
{
    int i;
    if (what == EN_SONG_REC) {
        song.magic = EN_SONG_MAGIC;
        song.n = 0;
        song.end = 0;
        song.seed = (rnd() | 1u);
        song.piece = (uint8_t)piece;
        for (i = 0; i < P_NPARAMS; i++)
            song.par[i] = par[i];
        song.mine_n = (uint8_t)mine_n;
        for (i = 0; i < EN_CYC_MAX; i++) {
            song.mine_mask[i] = mine_mask[i];
            song.mine_bass[i] = mine_bass[i];
        }
        song_begin();
        en_song_full = 0;
        en_song_state = SONG_RECORDING;
    } else if (what == EN_SONG_PLAY) {
        if (song.magic == EN_SONG_MAGIC && song.n <= EN_SONG_MAX && song.end > 0u) {
            song_begin();
            en_song_state = SONG_PLAYING;
        }
    } else if (en_song_state == SONG_RECORDING) {
        song.end = stick;
        en_song_state = SONG_IDLE;
    } else if (en_song_state == SONG_PLAYING) {
        en_song_state = SONG_IDLE;
        play_stop();
    }
}

static void tick(void)
{
    int p, m = model();
    if (en_song_state == SONG_PLAYING) {
        song_tick();
        if (stick >= song.end) {                 /* the take's end */
            en_song_state = SONG_IDLE;
            play_stop();
            return;
        }
    }
    if (arc_left) {                              /* the arc: a new way now and then, minutes long */
        arc_left--;
    } else {
        float r = frand() * 2.0f - 1.0f;
        arc_tgt = r * (0.6f + 0.4f * frand()) + (r > 0.0f ? 0.25f : -0.25f);
        arc_left = (uint32_t)((60.0f + 90.0f * frand()) * EN_SR / tick_len);
    }
    arc += (fm_clampf(arc_tgt, -1.0f, 1.0f) - arc) * tick_len * (1.0f / (30.0f * EN_SR));
    en_arc = arc;
    if (ctick == 0u)
        chord_begin();
    else if (chord_dirty) {                      /* the player's chord (or a new cycle, root) now */
        uint16_t cm;
        uint8_t cb;
        chord_dirty = 0;
        if (yours) {
            cm = held_mask;
            cb = held_bass;
        } else {
            cycle_chord(cyc_pos, &cm, &cb);
        }
        chord_set(cm, cb, 1);
    }
    if (m == M_PATTERNS) {
        for (p = 0; p < 3; p++)
            player_tick(p);
    } else {
        if (m != M_BELLS && en_part_on[PT_UPPER] && ticks % (uint32_t)(EN_PPQN / arp[PT_UPPER].div) == 0u)
            arp_step(PT_UPPER);
        if (en_part_on[PT_LOWER] && ticks % (uint32_t)(EN_PPQN / arp[PT_LOWER].div) == 0u) {
            if (m == M_PHASE)
                phase_step();
            else
                arp_step(PT_LOWER);
        }
        if (m == M_PHASE && en_part_on[PT_LOWER])
            phase_drift();
        lead_tick();
    }
    if (en_part_on[PT_BASS])
        bass_tick();
    if (en_part_on[PT_CHORDS])
        chords_tick();
    if (!en_ext)
        midi(0xF8, 0, 0);
    if (ticks % EN_PPQN == 0u)
        en_beats = ticks / EN_PPQN;
    ticks++;
    stick++;
    en_song_tick = stick;
    if (++ctick >= chord_ticks) {
        ctick = 0;
        chord_end();
    }
    en_chord_t = (float)ctick / (float)chord_ticks;
}

static void exec(uint32_t c)
{
    int a = (int)(c >> 16 & 0xFFu), b = (int)(int16_t)(c & 0xFFFFu);
    switch (c >> 24) {
    case C_SET:
        if (a < P_NPARAMS)
            apply(a, b);
        break;
    case C_PLAY:
        if (a && !en_playing) {
            play_start(1);
        } else if (!a && en_playing) {
            if (en_song_state == SONG_RECORDING) {
                song.end = stick;
                en_song_state = SONG_IDLE;
            }
            en_song_state = SONG_IDLE;
            play_stop();
        }
        break;
    case C_NEXT:
        if (en_playing)
            next_pending = 1;
        break;
    case C_SEED:
        rng ^= (uint32_t)(b & 0xFFFF) * 2654435761u;
        if (!rng)
            rng = 1u;
        break;
    case C_CLOCK:
        en_ext = 1;
        ext_age = 0;
        if (a == EN_CLK_TICK) {
            if (en_playing)
                tick();
        } else if (a == EN_CLK_START) {
            play_start(1);
        } else if (a == EN_CLK_CONTINUE) {
            if (!en_playing)
                play_start(0);
        } else if (en_playing) {
            play_stop();
        }
        break;
    case C_PANIC: quiet(); break;
    case C_REGROW:
        cycle_load();
        line_compose();
        if (en_playing)
            form_restart();
        break;
    case C_HOLD: {
        uint32_t m = (uint32_t)b & 0xFFFu;
        int bs = a & 15;
        if (!m) {
            keys_down = 0;
            break;
        }
        if (a & 16)                              /* one key: the key's triad on it */
            m = en_triad(bs, par[P_ROOT] + shift, tpl_minor);
        held_mask = (uint16_t)(m | 1u << bs);
        held_bass = (uint8_t)bs;
        keys_down = 1;
        yours = 1;
        chord_dirty = 1;
        en_yours = 1;
        if (!en_playing) {                       /* resting: the screen shows it */
            en_chord_mask = held_mask;
            en_chord_bass = held_bass;
        }
        break;
    }
    case C_MINE_CLR: mine_k = 0; break;
    case C_MINE_ADD:
        if (mine_k < EN_CYC_MAX) {
            mine_bass[mine_k] = (uint8_t)(a & 15);
            mine_mask[mine_k] = (uint16_t)((uint32_t)b & 0xFFFu);
            mine_k++;
        }
        break;
    case C_MINE_END:
        mine_n = mine_k;
        if (par[P_CYCLE] == EN_CYC_MINE) {
            cycle_load();
            line_compose();
            chord_dirty = 1;
        }
        break;
    case C_PART:
        if (a < EN_NPART) {
            if (en_playing) {
                part_req[a] = (uint8_t)(b ? 2 : 1);
            } else {
                present[a] = (uint8_t)(b != 0);
                parts_publish();
            }
        }
        break;
    case C_VERB: {
        int p;
        for (p = PT_UPPER; p <= PT_LOWER; p++)
            if (a == p || a == 2) {
                if (en_part_on[p] && en_playing)
                    arp[p].vpend = (int8_t)b;    /* at its next pass */
                else
                    arp_verb(p, b);
            }
        break;
    }
    case C_SONG: song_cmd(a, b); break;
    }
}

static void drain(void)
{
    while (q_r != q_w) {
        uint32_t c = q[q_r % QN];
        BARRIER();
        q_r++;
        exec(c);
        if (en_song_state == SONG_RECORDING && recordable(c))
            song_record(c);
    }
}

/* ------------------------------------------------------------- the render --- */
static void control(uint32_t n)                 /* once a chunk: the slow things (the sound's, not the music's) */
{
    int p, i, nv = 0;
    const float k = fm_minf((float)n * (1.0f / 1300.0f), 1.0f), gd = fm_maxf(1.0f - (float)n * (1.0f / 5000.0f), 0.0f);
    gains_target();
    for (p = 0; p < EN_NPART; p++) {
        pg[p] += (pg_t[p] - pg[p]) * k;
        en_part_glow[p] *= gd;
    }
    pl_decay += (pl_decay_t - pl_decay) * 0.002f;
    pl_dd2 = fm_clampf(pl_decay + 0.15f, 0.25f, 0.5f);
    for (i = 0; i < NV; i++)
        nv += vc[i].on;
    en_voices = (uint8_t)nv;
}

static void render_chunk(int32_t *out, uint32_t n, float gain)
{
    uint32_t j, o;
    int i;
    for (j = 0; j < n; j++) {
        mixl[j] = mixr[j] = rsend[j] = 0.0f;
        nzb[j] = (float)(int32_t)nrnd() * (1.0f / 2147483648.0f);
    }
    for (i = 0; i < NV; i++) {
        voice_t *v = &vc[i];
        uint32_t o0 = 0;
        if (!v->on)
            continue;
        if (v->wait > 0) {                       /* a note that starts later in the chunk, or later still */
            if ((uint32_t)v->wait >= n) {
                v->wait -= (int32_t)n;
                continue;
            }
            o0 = (uint32_t)v->wait;
            v->wait = 0;
        }
        if (modal(v->inst)) {
            modal_render(v, o0, n - o0);
        } else {
            for (o = o0; o < n && v->on; o += 32u)
                sub_render(v, o, n - o > 32u ? 32u : n - o);
        }
    }
    for (j = 0; j < n; j++) {
        float wl, wr, l, r;
        reverb(rsend[j] * rev_send, &wl, &wr);
        l = (mixl[j] + rev_wet * wl) * gain;
        r = (mixr[j] + rev_wet * wr) * gain;
        out[2 * j] = (int32_t)(fm_tanhf(l) * 8300000.0f);
        out[2 * j + 1] = (int32_t)(fm_tanhf(r) * 8300000.0f);
    }
}

void en_render(int32_t *out, uint32_t n, uint32_t gain_q12)
{
    const float gain = (float)gain_q12 * (1.0f / 4096.0f) * 2.0f;
    drain();
    if (en_ext && ++ext_age > 100u)
        en_ext = 0;
    while (n) {
        uint32_t k = n > BLK ? BLK : n;
        int timed = en_playing && !en_ext;
        if (timed) {
            if (tick_left <= 0.0f) {
                tick();
                tick_left += tick_len;
            }
            if ((float)k > tick_left)
                k = (uint32_t)tick_left + 1u;
        }
        if (k > n)
            k = n;
        if (timed)
            tick_left -= (float)k;
        control(k);
        render_chunk(out, k, gain);
        out += 2 * k;
        n -= k;
    }
}

void en_init(void)
{
    int i;
    for (i = 0; i <= SN; i++)
        sint[i] = fm_sinf((float)i * (FM_TWO_PI / (float)SN));
    plate_init();
    for (i = 0; i < NV; i++)
        vc[i].on = 0;
    rng = 0x9E3779B9u;                           /* everything as at power-on: the same piece, the same start */
    nrng = 0x2545F491u;
    vage = 0;
    for (i = 0; i < EN_NPART; i++) {
        en_notes[i] = 0;
        en_part_glow[i] = 0.0f;
    }
    ticks = ctick = stick = 0;
    cyc_reps = 4;
    arc = -0.45f;
    arc_tgt = 0.85f;
    arc_left = 0;
    ext_age = 0;
    en_ext = 0;
    en_playing = 0;
    en_yours = 0;
    en_section = 0;
    en_beats = 0;
    tick_left = 0.0f;
    mine_n = mine_k = 0;
    guard = 0;
    guard_avg_q8 = guard_calm = 0;
    damp_k = fm_expf(-6.9078f / (0.18f * EN_SR));
    fade_k = fm_expf(-6.9078f / (0.06f * EN_SR));
    dry_k = fm_expf(-6.9078f / (0.25f * EN_SR));
    q_r = q_w = 0;
    song.magic = 0;
    song.n = 0;
    song_i = 0;
    en_song_state = SONG_IDLE;
    en_song_full = 0;
    en_song_tick = 0;
    music_reset();
    arp[PT_UPPER].div = 3;
    arp[PT_LOWER].div = 2;
    for (i = 0; i < P_NPARAMS; i++)              /* every value first, then their effects from a known state */
        par[i] = PARAMS[i].def;
    chord_ticks = (uint32_t)CHORD_BEATS[par[P_CHORDLEN]] * EN_PPQN;
    cycle_load();
    {
        uint16_t m;
        uint8_t b;
        cycle_chord(0, &m, &b);
        cur_mask = (uint16_t)(m | 1u << b);
        cur_bass = b;
    }
    for (i = 0; i < P_NPARAMS; i++)
        apply(i, PARAMS[i].def);
    cycle_load();
    for (i = 0; i < 2; i++)
        arp_section(i);
    {
        uint16_t m;
        uint8_t b;
        cycle_chord(0, &m, &b);
        cur_mask = (uint16_t)(m | 1u << b);
        cur_bass = b;
        en_chord_mask = cur_mask;
        en_chord_bass = b;
    }
    arp_tones(PT_UPPER);
    arp_tones(PT_LOWER);
    bass_notes();
    pats_make();
    line_compose();
    players_reset();
    for (i = 0; i < EN_NPART; i++)
        pg[i] = pg_t[i];
    pl_decay = pl_decay_t;
}
