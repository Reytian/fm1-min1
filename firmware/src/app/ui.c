/* SPDX-License-Identifier: GPL-3.0-only */
/* MIN-1 UI: the panel and the screen. Part of the unity build (after gfx.c and project.c); the host
 * simulator includes it too. (AMB-1's, from X0X's.)
 *
 *   keys           the chord. EASY keys (Setup): each white key a chord of the key, the tonic leftmost,
 *                  the black ones the same with a seventh; several make a richer one. NOTES: the notes
 *                  held (one key: the key's triad on it). Let go and the cycle comes back at its next
 *                  chord; with HOLD on the chord stays until the same keys are played again
 *   PLAY           play / stop (in SONGS: the song)        EDIT  an IDEA for the page (a new cycle,
 *   REC            record a SONG from the top; again: kept        figure, line, bass, rhythm)
 *   SEL held       BY HAND: ENV ARP LFO SEQ FX a part in or out, EDIT back to the idea before, REC learn a
 *                  cycle from chords played, PLAY songs / live, keys 1-8: grow, shrink, shift, flip,
 *                  still, slower, faster, a new section
 *   OCT- / OCT+    the upper part's octave
 *   SELECT pages (HOME held: the tempo)   ALGORITHM the cycle   PRESETS the piece (in SONGS: the song)
 *   HOME ENV ARP LFO SEQ FX GLO   the pages: FORM, UPPER, LOWER, BASS, LEAD, CHORDS, SETUP (GLO again:
 *                  SYSTEM)
 *   KNOB 1-4       the page's four values
 *   SAVE           saves (it also saves by itself a few seconds after a change, while stopped);
 *                  held 1.5 s: the piece back to its recipe (in SONGS: the song deleted) */
#include "../dsp/fastmath.h"

/* the pages, one a part (a button each); SELECT steps through them in this order */
enum { V_FORM, V_UPPER, V_LOWER, V_BASS, V_LEAD, V_CHORDS, V_SETUP, V_SYSTEM, NVIEWS };
static const char *const VIEW_NAME[NVIEWS] = {"Form", "Upper", "Lower", "Bass", "Lead", "Chords", "Setup", "System"};
static const uint8_t VIEW_BTN[NVIEWS] = {B_HOME, B_ENV, B_ARP, B_LFO, B_SEQ, B_FX, B_GLO, B_GLO};
static const uint8_t PART_BTN[EN_NPART] = {B_ENV, B_ARP, B_LFO, B_SEQ, B_FX};
static const uint8_t PART_LEVEL[EN_NPART] = {P_UP_LEVEL, P_LO_LEVEL, P_BS_LEVEL, P_LD_LEVEL, P_CH_LEVEL};
/* the four knobs of each page: a parameter, or one of these; and what the knob is called there */
enum { K_LEDS = 100, K_CPU, K_NONE };
static const uint8_t VIEW_KNOB[NVIEWS][4] = {
    {P_EVOLVE, P_BUILD, P_PROCESS, P_CHORDLEN},
    {P_UP_LEVEL, P_UP_SOUND, P_UP_PULSE, P_UP_FIGURE},
    {P_LO_LEVEL, P_LO_SOUND, P_LO_PULSE, P_LO_FIGURE},
    {P_BS_LEVEL, P_BS_SOUND, P_BS_STYLE, P_BS_OCT},
    {P_LD_LEVEL, P_LD_SOUND, P_LD_ACT, P_LD_HARM},
    {P_CH_LEVEL, P_CH_SOUND, P_CH_RHYTHM, P_ROOM},
    {P_TEMPO, P_ROOT, P_KEYS, P_HOLD},
    {P_MODEL, P_MIDI, K_LEDS, K_CPU},
};
static const char *const KNOB_LABEL[NVIEWS][4] = {
    {"Evolve", "Build", "Process", "Chord"},
    {"Level", "Sound", "Pulse", "Figure"},
    {"Level", "Sound", "Pulse", "Figure"},
    {"Level", "Sound", "Style", "Octave"},
    {"Level", "Sound", "Activity", "Harmony"},
    {"Level", "Sound", "Rhythm", "Room"},
    {"Tempo", "Root", "Keys", "Hold"},
    {"Model", "MIDI out", "Lights", "CPU"},
};

/* the screen's colours: cool paper and a dark blue ink, a colour a part (cobalt, teal, slate, amber,
 * violet), each with a pale tint for the shapes at rest; coral for the player: the chord held, LEARN,
 * a song recording */
#define K_BG RGB(236, 240, 242)
#define K_PANEL RGB(250, 252, 253)
#define K_LINE RGB(200, 210, 216)
#define K_DIM RGB(110, 124, 134)
#define K_TEXT RGB(22, 32, 44)
#define K_ACC RGB(36, 99, 196)
#define K_YOURS RGB(214, 82, 70)
static const uint16_t PART_COL[EN_NPART] = {RGB(36, 99, 196), RGB(22, 140, 150), RGB(76, 86, 116), RGB(212, 116, 44),
                                            RGB(128, 94, 170)};
static const uint16_t PART_TINT[EN_NPART] = {RGB(178, 204, 236), RGB(170, 220, 222), RGB(196, 202, 216),
                                             RGB(240, 206, 176), RGB(214, 200, 232)};

enum { MODE_LIVE, MODE_SONGS };
#define UNDO_N 12
static struct {
    uint8_t view, mode;
    uint32_t btn, keys, btn_used;
    uint32_t enc_t[NE];
    uint8_t enc_run[NE];               /* detents in the current quick run, per knob (accel) */
    uint32_t down_t[NB];               /* when each button went down */
    uint32_t turn_t;                   /* the last knob turn */
    uint8_t help;                      /* a button's card is up: the button + 1 */
    uint8_t reset_done;                /* SAVE held: the piece went back to its recipe this hold */
    uint8_t dirty;                     /* the project changed since the last save */
    uint32_t act_t, saved_t;
    char msg[2][24];
    uint32_t msg_until;
    int8_t touched;                    /* the knob turned last (0..3), shown bright for a moment */
    uint32_t touch_until;
    uint32_t frame;
    uint32_t sig[3];                   /* what each band shows: redrawn when it changes */
    uint32_t midi_held[4];             /* MIDI notes held, a bit a note (they play chords as NOTES keys do) */
    uint32_t held_mask;                /* the chord sent to the engine (0: none) */
    int held_bass;
    uint32_t g_mask;                   /* the chord being played (all the notes since the first went down) */
    int g_low, g_bass;                 /* its lowest key's note, and its bass */
    uint8_t gesture, latched;          /* keys went down since all were up; HOLD: a chord stays */
    uint32_t latch_mask;
    /* LEARN */
    uint8_t learning, learn_n;
    uint16_t learn_mask[EN_CYC_MAX];   /* relative to the root */
    uint8_t learn_bass[EN_CYC_MAX];
    /* SONGS */
    uint8_t song_was, song_dirty, rec_slot;
    int8_t blob_slot;                  /* the slot whose take is in memory (-1: none) */
    /* IDEAS: what an idea changed, to go back */
    struct {
        uint8_t n, p[4];
        int16_t v[4];
    } undo[UNDO_N];
    uint8_t undo_n;
    uint32_t idea_r;
} ui;

static void say(const char *a, const char *b)
{
    uint32_t i;
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
}
void ui_say(const char *a, const char *b) { say(a, b); }

static char *itoa_u(uint32_t v, char *b)
{
    char t[12];
    int n = 0;
    do {
        t[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n)
        *b++ = t[--n];
    *b = 0;
    return b;
}
static char *puts_(char *d, const char *s)
{
    while (*s)
        *d++ = *s++;
    *d = 0;
    return d;
}
static char *put_time(char *b, uint32_t ticks, int tempo)    /* "2:41": ticks at a tempo, as minutes */
{
    uint32_t s = tempo > 0 ? ticks * 60u / ((uint32_t)tempo * EN_PPQN) : 0u;
    b = itoa_u(s / 60u, b);
    *b++ = ':';
    *b++ = (char)('0' + (s % 60u) / 10u);
    *b++ = (char)('0' + s % 10u);
    *b = 0;
    return b;
}

static void mark_dirty(void) { ui.dirty = 1; }
int ui_dirty(void) { return ui.dirty != 0; }

static int popcount12(uint32_t m)
{
    int n = 0, i;
    for (i = 0; i < 12; i++)
        n += (int)(m >> i & 1u);
    return n;
}
static uint32_t rot12(uint32_t m, int r)        /* pitch classes moved up r semitones */
{
    r = (r % 12 + 12) % 12;
    m &= 0xFFFu;
    return ((m << r) | (m >> (12 - r))) & 0xFFFu;
}

static int song_playing(void) { return en_song_state == SONG_PLAYING; }
static int key_minor(void) { return en_cycle_minor(proj.par[P_CYCLE], proj.par[P_SEED_CYC]); }
static int key_root_now(void) { return proj.par[P_ROOT] + en_shift; }

/* ------------------------------------------------------------ values --- */
static int knob_get(int k)
{
    if (k < P_NPARAMS)                           /* a song playing sets them: show what plays */
        return song_playing() ? en_params()[k] : proj.par[k];
    if (k == K_LEDS)
        return proj.leds;
    if (k == K_CPU)
        return (int)plat_cpu_pct();
    return 0;
}
static void knob_range(int k, int *lo, int *hi)
{
    if (k < P_NPARAMS) {
        *lo = en_param_info(k)->lo;
        *hi = en_param_info(k)->hi;
    } else {
        *lo = 0;
        *hi = k == K_LEDS ? 2 : k == K_CPU ? 100 : 0;
    }
}
static void knob_text(int k, char *b)
{
    int v = knob_get(k);
    if (k < P_NPARAMS) {
        en_param_text(k, v, b);
    } else if (k == K_LEDS) {                    /* On: the keys, and the unlit buttons glow; Keys: no glow */
        puts_(b, v == 2 ? "Keys" : v ? "On" : "Off");
    } else if (k == K_CPU) {
        b = itoa_u((uint32_t)v, b);
        *b++ = '%';
        *b = 0;
        if (en_guarding)
            puts_(b, " G");
    } else {
        b[0] = 0;
    }
}

static void knob_set(int k, int v)
{
    int lo, hi;
    if (k == K_CPU || k == K_NONE)
        return;
    knob_range(k, &lo, &hi);
    v = v < lo ? lo : v > hi ? hi : v;
    if (v == knob_get(k))
        return;
    if (k < P_NPARAMS) {
        en_set(k, v);
        if (song_playing())                      /* over a song: played, not kept */
            return;
        proj.par[k] = (int16_t)v;
    } else if (k == K_LEDS) {
        proj.leds = (uint8_t)v;
    }
    mark_dirty();
}

/* knob acceleration by speed (X0X's): a few clicks are one step each however quick, so small
 * corrections stay exact; a longer quick turn speeds up to 8 a detent, so half a turn sweeps 0-100.
 * Small ranges never accelerate. */
static uint8_t accel_off;              /* the host simulator's "spin": exact steps */
static int32_t accel(int role, int32_t s, int range)
{
    uint32_t now = plat_ms(), dt = now - ui.enc_t[role], a = (uint32_t)(s < 0 ? -s : s), m;
    ui.enc_t[role] = now;
    if (!a)
        return s;
    if (a > 1)
        dt /= a;                                      /* per detent */
    ui.enc_run[role] = (uint8_t)(dt < 60u ? (ui.enc_run[role] + a > 255u ? 255u : ui.enc_run[role] + a) : a);
    if (range <= 24 || accel_off || ui.enc_run[role] < 4u)
        return s;
    m = dt < 12u ? 8u : dt < 25u ? 5u : dt < 45u ? 3u : 1u;
    if (range < 100 && m > 3u)
        m = 3u;
    return s * (int32_t)m;
}

static void turn(int role, int k, int32_t e)
{
    int lo, hi;
    if (!e || k == K_NONE || k == K_CPU)
        return;
    knob_range(k, &lo, &hi);
    knob_set(k, knob_get(k) + accel(role, e, hi - lo));
}

/* ------------------------------------------------------------- ideas --- */
/* An idea is a value or two (a seed, and the figure or cycle set to Rolled): it is kept, saved, played
 * back like any other, and EDIT with SEL held goes back to the one before */
static uint32_t idea_rnd(void)
{
    uint32_t x = ui.idea_r ^ (plat_ms() * 2654435761u);
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return ui.idea_r = x ? x : 1u;
}
static int idea_seed(int lo, int hi)           /* a seed whose last digit is lo..hi (a rolled figure's length) */
{
    uint32_t r = idea_rnd();
    return (int)(r % 3200u) * 10 + lo + (int)((r >> 16) % (uint32_t)(hi - lo + 1));
}

static void idea_apply(int n, const int *p, const int *v, const char *what)
{
    int i;
    if (ui.undo_n == UNDO_N) {                   /* full: the oldest goes */
        for (i = 1; i < UNDO_N; i++)
            ui.undo[i - 1] = ui.undo[i];
        ui.undo_n--;
    }
    ui.undo[ui.undo_n].n = (uint8_t)n;
    for (i = 0; i < n; i++) {
        ui.undo[ui.undo_n].p[i] = (uint8_t)p[i];
        ui.undo[ui.undo_n].v[i] = (int16_t)knob_get(p[i]);
    }
    ui.undo_n++;
    for (i = 0; i < n; i++)
        knob_set(p[i], v[i]);
    say("IDEA", what);
}

static void idea_back(void)
{
    int i;
    if (!ui.undo_n) {
        say("NO IDEA", "TO GO BACK TO");
        return;
    }
    ui.undo_n--;
    for (i = ui.undo[ui.undo_n].n - 1; i >= 0; i--)
        knob_set(ui.undo[ui.undo_n].p[i], ui.undo[ui.undo_n].v[i]);
    say("BACK", "THE IDEA BEFORE");
}

static void idea(void)                         /* EDIT: a new idea for the page */
{
    int m = proj.par[P_MODEL], p[4], v[4];
    switch (ui.view) {
    case V_FORM:                                 /* a rolled cycle, in the same key (the seed's last bit) */
        p[0] = P_SEED_CYC;
        v[0] = (int)(idea_rnd() % 16000u) * 2 + key_minor();
        p[1] = P_CYCLE;
        v[1] = EN_CYC_ROLLED;
        idea_apply(2, p, v, "NEW CYCLE");
        break;
    case V_UPPER:
        if (m == M_BELLS) {                      /* where the bells answer */
            p[0] = P_UP_FIGURE;
            v[0] = (proj.par[P_UP_FIGURE] + 1) % 4;
            idea_apply(1, p, v, "THE BELLS MOVE");
        } else if (m == M_PATTERNS) {
            p[0] = P_UP_FIGURE;
            v[0] = (int)(idea_rnd() % 6u);
            idea_apply(1, p, v, "STARTS ELSEWHERE");
        } else if (m == M_PHASE) {               /* one cell for both: they phase against each other */
            int s = idea_seed(5, 9);
            p[0] = P_SEED_UP;
            v[0] = s;
            p[1] = P_UP_FIGURE;
            v[1] = EN_FIG_ROLLED;
            p[2] = P_SEED_LO;
            v[2] = s;
            p[3] = P_LO_FIGURE;
            v[3] = EN_FIG_ROLLED;
            idea_apply(4, p, v, "NEW CELL");
        } else {
            p[0] = P_SEED_UP;
            v[0] = idea_seed(0, 5);
            p[1] = P_UP_FIGURE;
            v[1] = EN_FIG_ROLLED;
            idea_apply(2, p, v, "NEW FIGURE");
        }
        break;
    case V_LOWER:
        if (m == M_PATTERNS) {
            p[0] = P_LO_FIGURE;
            v[0] = (int)(idea_rnd() % 6u);
            idea_apply(1, p, v, "STARTS ELSEWHERE");
        } else {
            p[0] = P_SEED_LO;
            v[0] = m == M_PHASE ? idea_seed(5, 9) : idea_seed(0, 5);
            p[1] = P_LO_FIGURE;
            v[1] = EN_FIG_ROLLED;
            idea_apply(2, p, v, "NEW FIGURE");
        }
        break;
    case V_BASS:                                 /* another way and octave */
        p[0] = P_BS_STYLE;
        v[0] = (proj.par[P_BS_STYLE] + 1 + (int)(idea_rnd() % 3u)) % 4;
        p[1] = P_BS_OCT;
        v[1] = (int)(idea_rnd() % 3u) - 1;
        idea_apply(2, p, v, "NEW BASS");
        break;
    case V_LEAD:
        p[0] = P_SEED_LD;
        v[0] = (int)(idea_rnd() % 32000u);
        idea_apply(1, p, v, m == M_BELLS ? "NEW MELODY" : m == M_PATTERNS ? "NEW PATTERNS" : "NEW LINE");
        break;
    case V_CHORDS:
        p[0] = P_CH_RHYTHM;
        v[0] = (proj.par[P_CH_RHYTHM] + 1 + (int)(idea_rnd() % 5u)) % 6;
        idea_apply(1, p, v, "NEW RHYTHM");
        break;
    default: say("NO IDEAS", "ON THIS PAGE"); break;
    }
}

/* ------------------------------------------------------------- chords --- */
static int cycle_valid(int c) { return c != EN_CYC_MINE || proj.slot[proj.piece].mine_n >= 2; }

static void set_cycle(int c)                    /* ALGORITHM: the library's cycles, Mine (if learned), Rolled */
{
    int n = EN_CYC_ROLLED + 1, step = c > proj.par[P_CYCLE] ? 1 : -1, i;
    for (i = 0; i < n; i++, c += step) {
        c = (c % n + n) % n;
        if (cycle_valid(c))
            break;
    }
    knob_set(P_CYCLE, c);
    say("CYCLE", en_cycle_name(c));
}

/* the chord of the keys held (and any MIDI notes): EASY keys a pad's chord each, NOTES the notes; HOLD
 * keeps it after they're let go, until the same chord is played again; and LEARN's gesture */
static uint32_t keys_chord(uint32_t keys, int *bass, int *low)
{
    uint32_t mask = 0;
    int k, easy = !proj.par[P_KEYS];
    *low = 128;
    *bass = 0;
    for (k = 0; k < NKEYS; k++)
        if (keys >> k & 1u) {
            int note = 53 + k, b;                /* key 0 is F3 */
            if (easy)
                mask |= en_pad(k, key_root_now(), key_minor(), &b);
            else {
                mask |= 1u << (note % 12);
                b = note % 12;
            }
            if (note < *low) {
                *low = note;
                *bass = b;
            }
        }
    for (k = 0; k < 128; k++)
        if (ui.midi_held[k >> 5] >> (k & 31) & 1u) {
            mask |= 1u << (k % 12);
            if (k < *low) {
                *low = k;
                *bass = k % 12;
            }
        }
    return mask;
}

static void learn_end(void);

static void chord_update(void)
{
    int bass, low;
    uint32_t mask = keys_chord(ui.keys, &bass, &low), send;
    int hold = proj.par[P_HOLD], easy = !proj.par[P_KEYS];
    if (mask) {
        if (!ui.gesture) {                       /* a new chord begins */
            ui.gesture = 1;
            ui.g_mask = 0;
            ui.g_low = 128;
        }
        ui.g_mask |= mask;
        if (low < ui.g_low) {
            ui.g_low = low;
            ui.g_bass = bass;
        }
        send = hold ? ui.g_mask : mask;          /* HOLD: the whole gesture, as it grows */
        bass = hold ? ui.g_bass : bass;
        if (send != ui.held_mask || bass != ui.held_bass) {
            ui.held_mask = send;
            ui.held_bass = bass;
            en_hold(send, bass, easy ? 3 : popcount12(send));
        }
        return;
    }
    if (!ui.gesture)
        return;
    ui.gesture = 0;                              /* all let go */
    if (ui.learning) {                           /* LEARN: it joins the cycle */
        int root = proj.par[P_ROOT];
        uint32_t m = ui.g_mask;
        char nm[16], t[12];
        if (!easy && popcount12(m) == 1)
            m = en_triad(ui.g_bass, root, key_minor());
        if (ui.learn_n < EN_CYC_MAX) {
            ui.learn_mask[ui.learn_n] = (uint16_t)rot12(m, -root);
            ui.learn_bass[ui.learn_n] = (uint8_t)((ui.g_bass - root + 12) % 12);
            ui.learn_n++;
        }
        itoa_u(ui.learn_n, puts_(t, "CHORD "));
        say(t, en_chord_name(m, ui.g_bass, nm));
        if (ui.learn_n >= EN_CYC_MAX)
            learn_end();
    }
    if (hold) {                                  /* the same chord again lets it go; another stays */
        if (ui.latched && ui.g_mask == ui.latch_mask) {
            ui.latched = 0;
            ui.held_mask = 0;
            en_hold(0, 0, 0);
        } else {
            ui.latched = 1;
            ui.latch_mask = ui.g_mask;
        }
        return;
    }
    ui.held_mask = 0;
    en_hold(0, 0, 0);
}

static void learn_start(void)
{
    ui.learning = 1;
    ui.learn_n = 0;
    say("LEARN", "PLAY THE CHORDS");
}

static void learn_end(void)
{
    ui.learning = 0;
    if (ui.learn_n >= 2) {
        char t[16];
        project_mine(ui.learn_n, ui.learn_mask, ui.learn_bass);
        en_mine(ui.learn_n, ui.learn_mask, ui.learn_bass);
        knob_set(P_CYCLE, EN_CYC_MINE);
        puts_(itoa_u(ui.learn_n, t), " CHORDS");
        say("LEARNED", t);
        mark_dirty();
    } else {
        say("NOT LEARNED", "2 CHORDS AT LEAST");
    }
}

static void set_piece(int i)                    /* the piece playing kept as it is; piece i as it was left */
{
    ui.learning = 0;
    ui.undo_n = 0;
    project_piece(i);
    project_apply();
    en_seed(plat_ms());
    en_regrow();
    say("PIECE", piece_name(proj.piece));
    mark_dirty();
}

/* ------------------------------------------------------------- songs --- */
static void song_say(const char *a)             /* "SONG 3", what it holds */
{
    char t[16], b[24], *e;
    const song_info_t *s = &proj.songs[proj.song];
    itoa_u(proj.song + 1u, puts_(t, "SONG "));
    if (!s->used) {
        say(t, a ? a : "EMPTY");
        return;
    }
    e = puts_(b, a ? a : piece_name(s->piece));
    if (!a) {
        *e++ = ' ';
        put_time(e, s->ticks, s->tempo);
    }
    say(t, b);
}

static void song_rec(void)                      /* REC: a take from the top into the slot; again: kept */
{
    if (en_song_state == SONG_RECORDING) {
        en_song_cmd(EN_SONG_STOP, 0);
        return;                                  /* kept when the render says it ended (ui_frame) */
    }
    if (song_playing())
        en_song_cmd(EN_SONG_STOP, 0);
    ui.rec_slot = proj.song;
    en_song_cmd(EN_SONG_REC, proj.piece);
    song_say(proj.songs[proj.song].used ? "REC, OVER IT" : "REC");
}

static void song_play(void)                     /* PLAY in SONGS: the slot's take, from its top */
{
    int k = proj.song;
    if (song_playing()) {
        en_song_cmd(EN_SONG_STOP, 0);
        song_say("STOPPED");
        return;
    }
    if (!proj.songs[k].used) {
        song_say("EMPTY: REC MAKES ONE");
        return;
    }
    if (ui.blob_slot != k) {
        if (song_load(k) != 0) {
            song_say("CAN'T BE READ");
            return;
        }
        ui.blob_slot = (int8_t)k;
    }
    en_song_cmd(EN_SONG_PLAY, 0);
    song_say("PLAYING");
}

static void songs_watch(void)                   /* the render's song state changed: kept, or ended */
{
    uint8_t now = en_song_state;
    if (now == ui.song_was)
        return;
    if (ui.song_was == SONG_RECORDING && now == SONG_IDLE) {
        const en_song_t *s = en_song();
        if (s->end > 0u) {
            song_info_t *i = &proj.songs[ui.rec_slot];
            i->used = 1;
            i->piece = s->piece;
            i->tempo = (uint16_t)s->par[P_TEMPO];
            i->ticks = s->end;
            ui.blob_slot = (int8_t)ui.rec_slot;
            ui.song_dirty = 1;
            ui.mode = MODE_SONGS;
            proj.song = ui.rec_slot;
            mark_dirty();
            song_say(en_song_full ? "FULL: KEPT SO FAR" : 0);
        }
    } else if (ui.song_was == SONG_PLAYING && now == SONG_IDLE) {
        project_apply();                         /* the piece's own values back (the take set its own) */
    }
    ui.song_was = now;
}

static void mode_toggle(void)                   /* SEL + PLAY: songs / live */
{
    if (ui.mode == MODE_SONGS) {
        if (song_playing())
            en_song_cmd(EN_SONG_STOP, 0);
        ui.mode = MODE_LIVE;
        say("LIVE", piece_name(proj.piece));
    } else {
        ui.mode = MODE_SONGS;
        song_say(0);
    }
}

/* ------------------------------------------------------------ buttons --- */
static void set_view(int v)
{
    ui.view = (uint8_t)v;
    ui.touched = -1;
}

static int sel_held(void) { return (int)(ui.btn >> B_SEL & 1u); }

/* SEL held: by hand. The page buttons bring their part in or out (at the next chord; Build goes to 0, the
 * arrangement is yours), EDIT goes back an idea, REC learns a cycle, PLAY switches songs / live */
static void sel_button(int b)
{
    int p;
    ui.btn_used |= 1u << B_SEL;
    for (p = 0; p < EN_NPART; p++)
        if (PART_BTN[p] == b) {
            int on = !en_part_on[p];
            if (proj.par[PART_LEVEL[p]] == 0) {
                say(EN_PART_NAME[p], "IS AT 0");
                return;
            }
            knob_set(P_BUILD, 0);
            en_part(p, on);
            say(EN_PART_NAME[p], on ? "IN, AT THE CHORD" : "OUT, AT THE CHORD");
            return;
        }
    switch (b) {
    case B_EDIT: idea_back(); break;
    case B_REC:
        if (ui.learning)
            learn_end();
        else
            learn_start();
        break;
    case B_PLAY: mode_toggle(); break;
    default: break;
    }
}

/* SEL held, a white key: processes by hand on the page's arpeggio (both, off the arpeggios' pages) */
static void sel_key(int w)
{
    static const char *const VERB_NAME[EN_NVERBS] = {"GROW", "SHRINK", "SHIFT", "FLIP", "STILL"};
    int target = ui.view == V_UPPER ? PT_UPPER : ui.view == V_LOWER ? PT_LOWER : 2;
    ui.btn_used |= 1u << B_SEL;
    if (w < EN_NVERBS) {
        en_verb(target, w);
        say(VERB_NAME[w], target == 2 ? "BOTH, AT THE PASS" : target ? "LOWER, AT THE PASS" : "UPPER, AT THE PASS");
    } else if (w == 5 || w == 6) {               /* the pulse slower or faster */
        int p, d = w == 5 ? -1 : 1;
        for (p = PT_UPPER; p <= PT_LOWER; p++)
            if (target == p || target == 2)
                knob_set(p == PT_UPPER ? P_UP_PULSE : P_LO_PULSE, proj.par[p == PT_UPPER ? P_UP_PULSE : P_LO_PULSE] + d);
        say(w == 5 ? "SLOWER" : "FASTER", target == 2 ? "BOTH" : target ? "LOWER" : "UPPER");
    } else if (w == 7) {
        en_next();
        say("NEW SECTION", "AT THE CHORD");
    }
}

static void button(int b)
{
    int v;
    if (sel_held() && b != B_SEL) {
        sel_button(b);
        return;
    }
    if (b == B_GLO) {                            /* SETUP, and again SYSTEM */
        set_view(ui.view == V_SETUP ? V_SYSTEM : V_SETUP);
        return;
    }
    for (v = 0; v < NVIEWS; v++)                 /* a page's button: its page */
        if (VIEW_BTN[v] == b) {
            set_view(v);
            return;
        }
    switch (b) {
    case B_EDIT: idea(); break;
    case B_PLAY:
        if (ui.mode == MODE_SONGS && en_song_state != SONG_RECORDING) {
            song_play();
        } else if (en_playing) {
            en_play(0);                          /* (a take recording ends with it) */
            say("STOPPED", 0);
        } else {
            en_seed(plat_ms());
            en_play(1);
            say("PLAYING", 0);
        }
        break;
    case B_REC:
        if (ui.learning)
            learn_end();
        else
            song_rec();
        break;
    case B_OCTDN: case B_OCTUP:
        knob_set(P_UP_OCT, proj.par[P_UP_OCT] + (b == B_OCTUP ? 1 : -1));
        {
            char t[8];
            en_param_text(P_UP_OCT, proj.par[P_UP_OCT], t);
            say("UPPER OCTAVE", t);
        }
        break;
    case B_SAVE: {
        int rc = project_save();
        if (rc == 0 && ui.song_dirty && ui.blob_slot >= 0 && !song_playing() && en_song_state != SONG_RECORDING) {
            rc = song_save(ui.blob_slot);
            if (rc == 0)
                ui.song_dirty = 0;
        }
        if (rc == 0) {
            ui.dirty = 0;
            say("SAVED", 0);
        } else {
            say("NOT SAVED", "FLASH ERROR");
        }
        break;
    }
    default: break;
    }
}

/* MIDI in (USB and the TRS jack): notes, any channel, are held as NOTES keys are (chords); clock and
 * start / stop drive the grid */
static void midi_in(void)
{
    uint32_t pkt;
    int changed = 0;
    while (plat_midi_in(&pkt)) {
        uint32_t st = (pkt >> 8) & 0xFFu, n = (pkt >> 16) & 0x7Fu, v = pkt >> 24, hi = st & 0xF0u;
        if (st == 0xF8u || st == 0xFAu || st == 0xFBu || st == 0xFCu) {
            en_clock(st == 0xF8u ? EN_CLK_TICK : st == 0xFAu ? EN_CLK_START : st == 0xFBu ? EN_CLK_CONTINUE : EN_CLK_STOP);
        } else if (hi == 0x90u && v) {
            ui.midi_held[n >> 5] |= 1u << (n & 31u);
            changed = 1;
        } else if (hi == 0x80u || hi == 0x90u) {
            ui.midi_held[n >> 5] &= ~(1u << (n & 31u));
            changed = 1;
        } else if (hi == 0xB0u && (n == 123u || n == 120u)) {   /* all notes off */
            ui.midi_held[0] = ui.midi_held[1] = ui.midi_held[2] = ui.midi_held[3] = 0;
            changed = 1;
        }
    }
    if (changed)
        chord_update();
}

/* HELP CARDS (X0X's): hold one button alone for HELP_HOLD and a card says what it does. A key, a
 * turn or another button puts it away; letting go after its card does nothing else. PLAY acts on
 * its press: no card. SEL's card is its map, while it is held. */
#define HELP_HOLD 900u
#define RESET_HOLD 1500u
static void help_tick(uint32_t btn, uint32_t keys)
{
    uint32_t now = plat_ms();
    int i;
    if (ui.help) {
        uint32_t m = 1u << (ui.help - 1);
        if (btn != m || keys || (int32_t)(ui.turn_t - ui.down_t[ui.help - 1]) > 0)
            ui.help = 0;
        return;
    }
    if (!btn || (btn & (btn - 1u)) || keys)
        return;
    for (i = 0; i < NB; i++)
        if (btn == 1u << i && i != B_PLAY && i != B_SEL && !(ui.btn_used & btn) && now - ui.down_t[i] >= HELP_HOLD &&
            (int32_t)(ui.turn_t - ui.down_t[i]) <= 0) {
            ui.help = (uint8_t)(i + 1);
            ui.btn_used |= btn;                  /* its release now does nothing */
        }
}

static const uint8_t WHITE_KEY[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};

static void input(void)
{
    uint32_t btn = plat_buttons(), keys = plat_keys(), ch, i;
    int32_t e;
    ch = btn ^ ui.btn;
    ui.btn = btn;
    for (i = 0; i < NB; i++) {
        uint32_t m = 1u << i;
        if (!(ch & m))
            continue;
        if (btn & m) {
            ui.btn_used &= ~m;
            ui.down_t[i] = plat_ms();
            if (i == B_SAVE)
                ui.reset_done = 0;
            if (i == B_PLAY) {                   /* transport on the press */
                ui.btn_used |= m;
                button((int)i);
            }
        } else if (!(ui.btn_used & m) && i != B_PLAY && i != B_SEL) {
            button((int)i);
        }
    }
    ch = keys ^ ui.keys;
    if (sel_held()) {                            /* SEL: the white keys are processes */
        for (i = 0; i < 16; i++)
            if ((ch & keys) >> WHITE_KEY[i] & 1u)
                sel_key((int)i);
        ui.keys = keys;
    } else {
        ui.keys = keys;
        if (ch)
            chord_update();
    }
    if (btn || keys || ch)
        ui.act_t = plat_ms();
    midi_in();
    if ((e = plat_enc(EN_SELECT)) != 0) {        /* SELECT: the pages; with HOME held, the tempo */
        if (btn >> B_HOME & 1u) {
            turn(EN_SELECT, P_TEMPO, e);
            ui.btn_used |= 1u << B_HOME;
            {
                char t[8];
                itoa_u((uint32_t)knob_get(P_TEMPO), t);
                say("TEMPO", t);
            }
        } else {
            int v = (int)ui.view + (e > 0 ? 1 : -1);
            if (v >= 0 && v < NVIEWS)
                set_view(v);
        }
        ui.turn_t = plat_ms();
    }
    if ((e = plat_enc(EN_ALGO)) != 0) {
        set_cycle(proj.par[P_CYCLE] + (e > 0 ? 1 : -1));
        ui.turn_t = plat_ms();
    }
    if ((e = plat_enc(EN_PRESET)) != 0) {
        if (ui.mode == MODE_SONGS) {             /* SONGS: the slot */
            if (song_playing())
                en_song_cmd(EN_SONG_STOP, 0);
            proj.song = (uint8_t)((proj.song + (e > 0 ? 1 : NSONGS - 1)) % NSONGS);
            song_say(0);
        } else {
            set_piece(proj.piece + (e > 0 ? 1 : -1));
        }
        ui.turn_t = plat_ms();
    }
    if ((btn >> B_SAVE & 1u) && !ui.reset_done && plat_ms() - ui.down_t[B_SAVE] > RESET_HOLD) {
        ui.reset_done = 1;                       /* held: the piece back to its recipe, or the song deleted */
        ui.btn_used |= 1u << B_SAVE;
        ui.help = 0;
        if (ui.mode == MODE_SONGS) {
            proj.songs[proj.song].used = 0;
            if (ui.blob_slot == (int)proj.song)
                ui.blob_slot = -1;
            song_say("DELETED");
        } else {
            project_piece_reset();
            project_apply();
            en_regrow();
            ui.undo_n = 0;
            say("RESET", piece_name(proj.piece));
        }
        mark_dirty();
    }
    for (i = 0; i < 4; i++)
        if ((e = plat_enc(EN_K1 + (int)i)) != 0) {
            turn(EN_K1 + (int)i, VIEW_KNOB[ui.view][i], e);
            ui.touched = (int8_t)i;
            ui.touch_until = plat_ms() + 1200u;
            ui.act_t = ui.turn_t = plat_ms();
        }
    help_tick(sel_held() ? 0u : btn, keys);
}

/* ------------------------------------------------------------- screen --- */
/* three bands: the header (28 px), the music (144), the knobs (68) */
#define HDR_H 28
#define MAIN_Y 28
#define MAIN_H 144
#define KNB_Y 172
#define KNB_H 68

static uint32_t hash(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }

/* a rectangle with rounded corners (radius r, up to 6) */
static void cv_round(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c)
{
    static const uint8_t IN[7][6] = {{0}, {1}, {1, 0}, {2, 1, 0}, {2, 1, 0, 0}, {3, 2, 1, 0, 0}, {4, 2, 1, 1, 0, 0}};
    int32_t j;
    if (r > 6)
        r = 6;
    if (r > h / 2)
        r = h / 2;
    cv_rect(x, y + r, w, h - 2 * r, c);
    for (j = 0; j < r; j++) {
        int32_t d = IN[r][j];
        cv_rect(x + d, y + j, w - 2 * d, 1, c);
        cv_rect(x + d, y + h - 1 - j, w - 2 * d, 1, c);
    }
}

/* a filled circle of radius r (0..6) centred on (cx, cy): HW[r][d] is the half width d rows from the middle */
static void cv_disc(int32_t cx, int32_t cy, int32_t r, uint16_t c)
{
    static const uint8_t HW[7][7] = {{0}, {1, 0}, {2, 1, 0}, {3, 2, 2, 0}, {4, 4, 3, 2, 0}, {5, 5, 4, 4, 3, 0},
                                     {6, 6, 5, 5, 4, 3, 0}};
    int32_t j;
    if (r <= 0) {
        cv_rect(cx, cy, 1, 1, c);
        return;
    }
    if (r > 6)
        r = 6;
    for (j = -r + 1; j < r; j++) {
        int32_t w = HW[r][j < 0 ? -j : j];
        cv_rect(cx - w, cy + j, 2 * w + 1, 1, c);
    }
}

static void text_c(int32_t cx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(cx - text_w(f, s) / 2, y, f, s, c);
}

/* anti-aliased drawing (X0X's): a pixel blended over the canvas, a disc, and a knob's ring */
static void px_blend(int32_t x, int32_t y, uint16_t c, uint32_t a)
{
    uint16_t *q;
    uint32_t bg;
    y += cv_oy;
    if ((uint32_t)x >= cv_w || y < cv_y0 || y >= cv_y1 || !a)
        return;
    q = &cv_px[(uint32_t)y * cv_w + (uint32_t)x];
    if (a >= 16u) {
        *q = swap16(c);
        return;
    }
    bg = swap16(*q);
    *q = swap16((uint32_t)(((((c >> 11) * a + (bg >> 11) * (16u - a)) >> 4) << 11) |
                           (((((c >> 5) & 63u) * a + ((bg >> 5) & 63u) * (16u - a)) >> 4) << 5) |
                           (((c & 31u) * a + (bg & 31u) * (16u - a)) >> 4)));
}

static void disc(float cx, float cy, float r, uint16_t c)
{
    int32_t x0 = (int32_t)fm_floorf(cx - r - 1.0f), x1 = (int32_t)(cx + r + 1.0f);
    int32_t y0 = (int32_t)fm_floorf(cy - r - 1.0f), y1 = (int32_t)(cy + r + 1.0f), x, y;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) {
            float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy, d = fm_sqrtf(dx * dx + dy * dy);
            px_blend(x, y, c, (uint32_t)(fm_clampf(r - d + 0.5f, 0.0f, 1.0f) * 16.0f + 0.5f));
        }
}

/* atan2 in turns, 0..1 from +x going clockwise on screen (y down) */
static float turns(float y, float x)
{
    float ax = fm_fabsf(x), ay = fm_fabsf(y), a = ax < ay ? ax / (ay + 1e-9f) : ay / (ax + 1e-9f), s = a * a, r;
    r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;   /* atan on [0, 1] */
    if (ay > ax)
        r = 1.57079637f - r;
    if (x < 0.0f)
        r = FM_PI - r;
    if (y < 0.0f)
        r = FM_TWO_PI - r;
    return r * (1.0f / FM_TWO_PI);
}

/* a knob: a 270-degree track from 7:30 to 4:30, anti-aliased, round ends; the value filled from
 * the start (or from the top, for a centred value) and a round handle */
static void arc(int32_t cx, int32_t cy, int32_t r, float v, uint16_t track, uint16_t fill, uint16_t bg, int bipolar)
{
    const float th = 3.4f, a0 = 0.375f, sweep = 0.75f;
    float ro = (float)r, ri = ro - th, rm = ro - th * 0.5f, fx = (float)cx + 0.5f, fy = (float)cy + 0.5f, lo, hi;
    int32_t x, y;
    v = fm_clampf(v, 0.0f, 1.0f);
    lo = bipolar ? (v < 0.5f ? v : 0.5f) : 0.0f;
    hi = bipolar ? (v < 0.5f ? 0.5f : v) : v;
    for (y = -r - 1; y <= r + 1; y++)
        for (x = -r - 1; x <= r + 1; x++) {
            float dx = (float)x, dy = (float)y, d = fm_sqrtf(dx * dx + dy * dy), t, a;
            a = fm_clampf(ro + 0.5f - d, 0.0f, 1.0f) * fm_clampf(d - ri + 0.5f, 0.0f, 1.0f);
            if (a <= 0.0f)
                continue;
            t = turns(dy, dx) - a0;
            if (t < 0.0f)
                t += 1.0f;
            t /= sweep;
            if (t > 1.0f)
                continue;
            px_blend(cx + x, cy + y, (t >= lo && t <= hi) ? fill : track, (uint32_t)(a * 16.0f + 0.5f));
        }
    {
        float e0 = a0 * FM_TWO_PI, e1 = (a0 + sweep) * FM_TWO_PI, ev = (a0 + sweep * v) * FM_TWO_PI;
        float el = (a0 + sweep * lo) * FM_TWO_PI;
        disc(fx + fm_cosf(e0) * rm - 0.5f, fy + fm_sinf(e0) * rm - 0.5f, th * 0.5f, lo <= 0.0f && hi > 0.0f ? fill : track);
        disc(fx + fm_cosf(e1) * rm - 0.5f, fy + fm_sinf(e1) * rm - 0.5f, th * 0.5f, hi >= 1.0f ? fill : track);
        if (hi > lo)
            disc(fx + fm_cosf(el) * rm - 0.5f, fy + fm_sinf(el) * rm - 0.5f, th * 0.5f, fill);
        disc(fx + fm_cosf(ev) * rm - 0.5f, fy + fm_sinf(ev) * rm - 0.5f, th * 0.5f + 1.3f, bg);
        disc(fx + fm_cosf(ev) * rm - 0.5f, fy + fm_sinf(ev) * rm - 0.5f, th * 0.5f + 0.6f, fill);
    }
}

static void caps(char *d, const char *s, int n)  /* s in capitals, at most n - 1 characters */
{
    int i;
    for (i = 0; i < n - 1 && s[i]; i++)
        d[i] = (char)(s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32 : s[i]);
    d[i] = 0;
}

static uint16_t mix(uint16_t a, uint16_t b, int t)   /* t 0..16: a -> b */
{
    int r, g, bl;
    if (t <= 0)
        return a;
    if (t >= 16)
        return b;
    r = ((a >> 11) * (16 - t) + (b >> 11) * t) / 16;
    g = (((a >> 5) & 63) * (16 - t) + ((b >> 5) & 63) * t) / 16;
    bl = ((a & 31) * (16 - t) + (b & 31) * t) / 16;
    return (uint16_t)(r << 11 | g << 5 | bl);
}
static int q16(float x) { return x <= 0.0f ? 0 : x >= 1.0f ? 16 : (int)(x * 16.0f + 0.5f); }

/* the header: the piece in a tag, the key and the cycle (in SONGS: the song in a coral tag, its piece and
 * time); on the right the tempo with a beat light (a play triangle while stopped), a red dot and the time
 * while a take records, and a dot while there is something unsaved. Messages take its place a moment */
static void draw_header(void)
{
    char t[32];
    int msg = plat_ms() < ui.msg_until, playing = en_playing, rec = en_song_state == SONG_RECORDING;
    int songs = ui.mode == MODE_SONGS, sp = song_playing();
    uint32_t secs = (uint32_t)en_song_tick * 60u / ((uint32_t)(knob_get(P_TEMPO) > 0 ? knob_get(P_TEMPO) : 100) * EN_PPQN);
    uint32_t h = hash(hash(hash(2166136261u, proj.piece | (uint32_t)proj.par[P_ROOT] << 8 | (uint32_t)proj.par[P_CYCLE] << 12 |
                                                 (uint32_t)proj.song << 20 | (uint32_t)songs << 24),
                           (uint32_t)playing | (uint32_t)rec << 1 | (uint32_t)sp << 2 | (uint32_t)(ui.view == V_SYSTEM) << 3 |
                               ui.dirty << 4 | (uint32_t)msg << 5 | (uint32_t)en_ext << 6 | (uint32_t)en_guarding << 7),
                      (uint32_t)knob_get(P_TEMPO) | ((rec || sp) ? secs << 8 : 0u));
    if (msg)
        h = hash(hash(h, (uint32_t)ui.msg[0][0] << 8 | ui.msg[0][1]), ui.msg_until);
    if (playing)
        h = hash(h, en_beats & 1u);
    if (h == ui.sig[0])
        return;
    ui.sig[0] = h;
    cv_begin(240, HDR_H, K_BG);
    if (msg) {
        int32_t x = cv_text(10, 6, &FONT_B, ui.msg[0], K_TEXT);
        cv_text(x + 8, 6, &FONT_B, ui.msg[1], ui.learning || rec ? K_YOURS : K_ACC);
    } else {
        int32_t x = 232, w;
        if (ui.dirty) {                            /* unsaved */
            cv_round(x - 5, 12, 5, 5, 2, K_DIM);
            x -= 11;
        }
        if (en_ext)
            puts_(t, "EXT");
        else
            itoa_u((uint32_t)knob_get(P_TEMPO), t);
        x -= text_w(&FONT_S, t);
        cv_text(x, 6, &FONT_S, t, playing ? K_TEXT : K_DIM);
        x -= 16;
        if (playing) {                            /* the beat */
            int on = (en_beats & 1u) == 0u;
            cv_round(x, 9, 10, 10, 5, on ? (rec ? K_YOURS : K_ACC) : PART_TINT[PT_UPPER]);
        } else {
            int32_t j;                            /* stopped: a play triangle */
            for (j = 0; j < 5; j++)
                cv_rect(x + 2 + j, 9 + j, 1, 11 - 2 * j, K_DIM);
        }
        if (rec || sp) {                          /* a take recording: a red dot and its time; a song: its time */
            put_time(t, en_song_tick, knob_get(P_TEMPO));
            x -= 8 + text_w(&FONT_XS, t);
            cv_text(x, 8, &FONT_XS, t, rec ? K_YOURS : K_TEXT);
            if (rec) {
                x -= 12;
                cv_round(x, 10, 8, 8, 4, K_YOURS);
            }
        }
        if (songs) {                              /* SONGS: the slot, coral; its piece */
            const song_info_t *s = &proj.songs[proj.song];
            char *e = itoa_u(proj.song + 1u, puts_(t, "SONG "));
            (void)e;
            w = text_w(&FONT_B, t) + 12;
            cv_round(4, 4, w, 20, 6, K_YOURS);
            cv_text(10, 6, &FONT_B, t, K_BG);
            if (s->used) {
                e = puts_(t, piece_name(s->piece));
                if (!sp && !rec) {
                    *e++ = ' ';
                    put_time(e, s->ticks, s->tempo);
                }
            } else {
                puts_(t, "empty");
            }
            cv_text(w + 10, 8, &FONT_XS, t, K_DIM);
        } else {
            w = text_w(&FONT_B, piece_name(proj.piece)) + 12;   /* the piece, in an ink tag */
            cv_round(4, 4, w, 20, 6, K_TEXT);
            cv_text(10, 6, &FONT_B, piece_name(proj.piece), K_BG);
            w = cv_text(w + 10, 8, &FONT_XS, en_note_name(knob_get(P_ROOT)), K_TEXT);
            if (w + 4 + text_w(&FONT_XS, en_cycle_name(knob_get(P_CYCLE))) < x - 4)
                cv_text(w + 4, 8, &FONT_XS, en_cycle_name(knob_get(P_CYCLE)), K_DIM);
        }
    }
    cv_blit(0, 0);
}

/* the cycle: a cell a chord, the one playing filled (coral when it is the player's chord), a line
 * under it for how far through it is */
#define CYC_Y 4
#define CYC_H 24
static void draw_cycle(void)
{
    int n, k;
    int32_t gap = 4, w, x = 4;
    if (ui.learning) {                           /* LEARN: the chords so far, then the empty places */
        n = EN_CYC_MAX;
        w = (232 - gap * (n - 1)) / n;
        for (k = 0; k < n; k++, x += w + gap) {
            char nm[16];
            if (k < ui.learn_n) {
                int root = proj.par[P_ROOT];
                cv_round(x, CYC_Y, w, CYC_H, 5, K_YOURS);
                en_chord_name(rot12(ui.learn_mask[k], root), (ui.learn_bass[k] + root) % 12, nm);
                if (text_w(&FONT_XS, nm) > w - 2)    /* too wide: the root alone */
                    nm[nm[1] == 'b' || nm[1] == '#' ? 2 : 1] = 0;
                text_c(x + w / 2, CYC_Y + 4, &FONT_XS, nm, K_BG);
            } else {
                cv_round(x, CYC_Y, w, CYC_H, 5, k == ui.learn_n ? PART_TINT[PT_LEAD] : K_PANEL);
            }
        }
        return;
    }
    n = en_cyc_n < 1 ? 1 : en_cyc_n;
    w = (232 - gap * (n - 1)) / n;
    for (k = 0; k < n; k++, x += w + gap) {
        int cur = k == en_cyc_pos, yours = cur && en_yours;
        uint16_t bg = yours ? K_YOURS : cur && en_playing ? K_ACC : K_PANEL, fg = (cur && en_playing) || yours ? K_BG : K_TEXT;
        const felucca_font_t *f = &FONT_S;
        char nm[16];
        if (yours)
            en_chord_name(en_chord_mask, en_chord_bass, nm);
        else
            en_chord_name(en_cyc_mask[k], en_cyc_bass[k], nm);
        cv_round(x, CYC_Y, w, CYC_H, 5, bg);
        if (text_w(f, nm) > w - 4)
            f = &FONT_XS;
        if (text_w(f, nm) > w - 2) {             /* still too wide: no slash bass, then the root alone */
            char *s = nm;
            while (*s && *s != '/')
                s++;
            *s = 0;
            if (text_w(f, nm) > w - 2)
                nm[nm[1] == 'b' || nm[1] == '#' ? 2 : 1] = 0;
        }
        text_c(x + w / 2, CYC_Y + (f == &FONT_S ? 4 : 5), f, nm, fg);
        if (cur && en_playing) {
            int32_t pw = (int32_t)(en_chord_t * (float)w + 0.5f);
            cv_rect(x, CYC_Y + CYC_H + 2, w, 2, K_LINE);
            if (pw > 0)
                cv_rect(x, CYC_Y + CYC_H + 2, pw, 2, yours ? K_YOURS : K_ACC);
        }
    }
}

/* where the form is: the model, the round of the section; YOURS while the keys have the chord, the shift */
static void draw_formline(void)
{
    char t[32], *e;
    int32_t x;
    if (ui.learning) {
        e = itoa_u(ui.learn_n, puts_(t, "LEARN "));
        puts_(e, "/8");
        cv_text(6, 34, &FONT_XS, t, K_YOURS);
        cv_text(230 - text_w(&FONT_XS, "REC: DONE"), 34, &FONT_XS, "REC: DONE", K_DIM);
        return;
    }
    e = puts_(t, EN_MODEL_NAME[knob_get(P_MODEL)]);
    e = puts_(e, "  ROUND ");
    e = itoa_u((uint32_t)en_cyc_rep + 1u, e);
    e = puts_(e, " OF ");
    itoa_u(en_cyc_reps ? en_cyc_reps : 1u, e);
    {
        char u[32];
        int i;
        for (i = 0; t[i] && i < 31; i++)         /* in capitals */
            u[i] = (char)(t[i] >= 'a' && t[i] <= 'z' ? t[i] - 32 : t[i]);
        u[i] = 0;
        cv_text(6, 34, &FONT_XS, u, K_DIM);
    }
    x = 230;
    if (en_yours) {
        x -= text_w(&FONT_XS, "YOURS");
        cv_text(x, 34, &FONT_XS, "YOURS", K_YOURS);
        x -= 8;
    }
    if (en_shift) {
        char s[8];
        s[0] = en_shift > 0 ? '+' : '-';
        itoa_u((uint32_t)(en_shift > 0 ? en_shift : -en_shift), s + 1);
        x -= text_w(&FONT_XS, s);
        cv_text(x, 34, &FONT_XS, s, K_ACC);
    }
}

/* the parts: a lane each, its name lit while it plays this section. The arpeggios' figures as dots (the
 * figure's length, the notes the additive process has filled, the one sounding brightest; + or - while
 * the process grows or shrinks it, a hand when it is held by hand); in PHASE the lower's lead; in
 * PATTERNS each player's pattern; the others their sound and way of playing */
#define LANE_Y 52
#define LANE_H 18
static void draw_lanes(void)
{
    static const uint8_t SOUND_PAR_UI[EN_NPART] = {P_UP_SOUND, P_LO_SOUND, P_BS_SOUND, P_LD_SOUND, P_CH_SOUND};
    static const uint8_t WAY_PAR[EN_NPART] = {P_UP_FIGURE, P_LO_FIGURE, P_BS_STYLE, P_LD_HARM, P_CH_RHYTHM};
    int p, m = knob_get(P_MODEL);
    for (p = 0; p < EN_NPART; p++) {
        int32_t y = LANE_Y + p * LANE_H, cy = y + 7, x;
        int on = en_part_on[p], g = q16(en_part_glow[p]);
        uint16_t col = PART_COL[p], tint = PART_TINT[p];
        char t[24], w[16];
        caps(t, EN_PART_NAME[p], sizeof t);
        cv_text(6, y, &FONT_XS, t, on ? col : K_DIM);
        cv_disc(56, cy, on ? 2 + g / 6 : 1, on ? mix(tint, col, 6 + g * 10 / 16) : K_LINE);
        if (!knob_get(PART_LEVEL[p])) {          /* at 0: not in the piece */
            cv_text(66, y, &FONT_XS, "OFF", K_LINE);
            continue;
        }
        if (m == M_PATTERNS && (p <= PT_LOWER || p == PT_LEAD)) {   /* a player: its pattern */
            char *e = itoa_u(en_pat_at[p] + 1u, puts_(t, "PATTERN "));
            puts_(itoa_u(en_pat_n, puts_(e, " OF ")), "");
            cv_text(66, y, &FONT_XS, t, on ? K_TEXT : K_DIM);
            continue;
        }
        if (p <= PT_LOWER && !(m == M_BELLS && p == PT_UPPER)) {
            int i, full = en_fig_full[p], len = en_fig_len[p] ? en_fig_len[p] : 1, pos = en_fig_pos[p];
            int32_t step = full > 8 ? 11 : 16;
            itoa_u(en_part_div[p], t);
            cv_text(66, y, &FONT_XS, t, on ? K_TEXT : K_DIM);
            for (i = 0; i < full && i < EN_FIG_MAX; i++) {
                int32_t cx = 84 + i * step;
                int cur = i == (pos + len - 1) % len;
                if (!on)
                    cv_disc(cx, cy, 1, K_LINE);
                else if (i < len)
                    cv_disc(cx, cy, cur ? 4 : 3, cur ? col : mix(tint, col, 7));
                else
                    cv_disc(cx, cy, 2, K_LINE);
            }
            x = 214;
            if (on && m == M_PHASE && p == PT_LOWER) {   /* the lower's lead */
                char s[12];
                int tenth = (int)(en_phase * 10.0f + 0.5f);
                s[0] = '+';
                puts_(itoa_u((uint32_t)tenth % 10u, puts_(itoa_u((uint32_t)tenth / 10u, s + 1), ".")), "");
                cv_text(240 - 4 - text_w(&FONT_XS, s), y, &FONT_XS, s, col);
            } else if (on && en_fig_proc[p]) {
                cv_text(x + 6, y, &FONT_XS, en_fig_proc[p] == 1 ? "+" : "-", col);
            }
            if (on && en_fig_user[p])            /* held by hand */
                cv_round(x - 4, cy - 2, 5, 5, 2, K_YOURS);
            continue;
        }
        en_param_text(SOUND_PAR_UI[p], knob_get(SOUND_PAR_UI[p]), w);
        caps(t, w, sizeof t);
        x = cv_text(66, y, &FONT_XS, t, on ? K_TEXT : K_DIM);
        if (m == M_BELLS && p == PT_UPPER)       /* the bells: where they answer */
            puts_(w, "BELLS ");
        else
            w[0] = 0;
        {
            char v[16];
            en_param_text(WAY_PAR[p], knob_get(WAY_PAR[p]), v);
            puts_(w + (w[0] ? 6 : 0), v);
        }
        if (m == M_BELLS && p == PT_LEAD)
            puts_(w, "MELODY");
        caps(t, w, sizeof t);
        cv_text(x + 8, y, &FONT_XS, t, K_DIM);
    }
}

/* SEL held: the map of what it does, in the music's place */
static void draw_sel(void)
{
    static const char *const LINES[7] = {
        "ENV ARP LFO SEQ FX: A PART IN / OUT",
        "EDIT: BACK TO THE IDEA BEFORE",
        "REC: LEARN A CYCLE   PLAY: SONGS / LIVE",
        "WHITE KEYS:",
        "1 GROW   2 SHRINK   3 SHIFT   4 FLIP",
        "5 STILL   6 SLOWER   7 FASTER",
        "8 NEW SECTION",
    };
    int k;
    cv_round(8, 4, 224, 136, 6, K_PANEL);
    cv_round(8, 4, 224, 22, 6, K_YOURS);
    cv_rect(8, 20, 224, 6, K_YOURS);
    text_c(120, 7, &FONT_B, "SEL: BY HAND", K_BG);
    for (k = 0; k < 7; k++)
        cv_text(16, 32 + k * 15, &FONT_XS, LINES[k], k == 3 ? K_DIM : K_TEXT);
}

/* a button's card (help_tick): its name, then what it does, in the music's place */
#define HELP_LINES 4
static const char *const BTN_LABEL[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                          "PLAY", "REC", "OCT-", "OCT+"};
static int help_lines(int b, const char *l[HELP_LINES])
{
    int n = 0;
#define L(t) (l[n++] = (t))
    switch (b) {
    case B_HOME: L("THE FORM"); L("EVOLVE, BUILD, PROCESS,"); L("CHORD LENGTH"); L("+ SELECT: TEMPO"); break;
    case B_ENV: L("THE UPPER PART"); L("LEVEL, SOUND, PULSE,"); L("FIGURE"); L("SEL + ENV: IN / OUT"); break;
    case B_ARP: L("THE LOWER PART"); L("LEVEL, SOUND, PULSE,"); L("FIGURE"); L("SEL + ARP: IN / OUT"); break;
    case B_LFO: L("THE BASS"); L("LEVEL, SOUND, STYLE,"); L("OCTAVE"); L("SEL + LFO: IN / OUT"); break;
    case B_SEQ: L("THE LEAD"); L("LEVEL, SOUND, ACTIVITY,"); L("HARMONY"); L("SEL + SEQ: IN / OUT"); break;
    case B_FX: L("THE CHORDS"); L("LEVEL, SOUND, RHYTHM,"); L("ROOM (THE REVERB)"); L("SEL + FX: IN / OUT"); break;
    case B_GLO: L("SETUP: TEMPO, ROOT,"); L("KEYS (EASY / NOTES), HOLD"); L("AGAIN: SYSTEM: THE MODEL,"); L("MIDI OUT, LIGHTS, CPU"); break;
    case B_EDIT: L("AN IDEA FOR THIS PAGE:"); L("A CYCLE, A FIGURE, A LINE,"); L("A BASS, A RHYTHM"); L("SEL + EDIT: THE ONE BEFORE"); break;
    case B_REC: L("RECORD A SONG FROM THE TOP,"); L("ALL YOU DO; REC AGAIN: KEPT"); L("PLAY PLAYS IT BACK"); L("SEL + REC: LEARN A CYCLE"); break;
    case B_OCTDN: L("THE UPPER AN OCTAVE DOWN"); L("WITH OCT+, 5 S: UPDATE MODE"); break;
    case B_OCTUP: L("THE UPPER AN OCTAVE UP"); L("WITH OCT-, 5 S: UPDATE MODE"); break;
    case B_SAVE: L("SAVES THE PIECES AND SONGS"); L("KEEP HOLDING: THE PIECE"); L("BACK TO ITS RECIPE (SONGS:"); L("THE SONG DELETED)"); break;
    default: break;
    }
#undef L
    return n;
}

static void draw_help(void)
{
    const char *l[HELP_LINES];
    int n = help_lines(ui.help - 1, l), k;
    cv_round(16, 8, 208, 128, 6, K_PANEL);
    cv_round(16, 8, 208, 26, 6, K_TEXT);
    cv_rect(16, 28, 208, 6, K_TEXT);
    text_c(120, 12, &FONT_B, BTN_LABEL[ui.help - 1], K_BG);
    for (k = 0; k < n; k++)
        text_c(120, 46 + k * 20, &FONT_S, l[k], K_TEXT);
}

static void draw_main(void)
{
    uint32_t h = hash(2166136261u, (uint32_t)en_cyc_n | (uint32_t)en_cyc_pos << 4 | (uint32_t)en_yours << 8 |
                                        (uint32_t)en_playing << 9 | (uint32_t)ui.learning << 10 | (uint32_t)ui.learn_n << 11 |
                                        (uint32_t)(uint8_t)en_shift << 16 | (uint32_t)en_cyc_rep << 24);
    int k;
    if (ui.frame % 2u)                           /* 30 frames a second is plenty */
        return;
    if (ui.help || sel_held()) {
        h = hash(0x48454C50u, ui.help | (uint32_t)sel_held() << 8);
        if (h != ui.sig[1]) {
            ui.sig[1] = h;
            cv_begin(240, MAIN_H, K_BG);
            if (ui.help)
                draw_help();
            else
                draw_sel();
            cv_blit(0, MAIN_Y);
        }
        return;
    }
    for (k = 0; k < en_cyc_n && k < EN_CYC_MAX; k++)
        h = hash(h, en_cyc_mask[k] | (uint32_t)en_cyc_bass[k] << 12);
    h = hash(h, en_chord_mask | (uint32_t)en_chord_bass << 12 | (uint32_t)(en_chord_t * 40.0f) << 16 | (uint32_t)en_cyc_reps << 24);
    for (k = 0; k < EN_NPART; k++)
        h = hash(h, (uint32_t)en_part_on[k] | (uint32_t)q16(en_part_glow[k]) << 1 | (uint32_t)en_fig_len[k] << 6 |
                        (uint32_t)en_fig_pos[k] << 10 | (uint32_t)en_fig_proc[k] << 14 | (uint32_t)en_part_div[k] << 16 |
                        (uint32_t)en_fig_full[k] << 20 | (uint32_t)en_fig_user[k] << 25 | (uint32_t)en_pat_at[k] << 26);
    h = hash(h, (uint32_t)(en_phase * 10.0f) | (uint32_t)knob_get(P_MODEL) << 12);
    for (k = P_UP_LEVEL; k <= P_CH_RHYTHM; k++)
        h = hash(h, (uint32_t)knob_get(k));
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    draw_cycle();
    draw_formline();
    draw_lanes();
    cv_blit(0, MAIN_Y);
}

/* the knob strip (X0X's): a rule with the page's name and the page dots at its right end; under
 * it a cell a knob: its name (the model's, where it plays it otherwise), a ring (or pips, for a switch
 * of a few positions), the value. The knob being turned is drawn on a tint. Only redrawn when what it
 * shows changes (the rings cost a square root and an arctangent per pixel). */
static void draw_knobs(void)
{
    uint32_t h = hash(2166136261u, ui.view | (uint32_t)ui.touched << 8 | (uint32_t)knob_get(P_MODEL) << 16), i;
    int now_touch = plat_ms() < ui.touch_until;
    for (i = 0; i < 4; i++)
        h = hash(h, (uint32_t)knob_get(VIEW_KNOB[ui.view][i]));
    h = hash(h, (uint32_t)now_touch | (uint32_t)en_guarding << 1);
    if (h == ui.sig[2])
        return;
    ui.sig[2] = h;
    cv_begin(240, KNB_H, K_BG);
    {   /* the rule; at its right end, where you are: the page, then the dots */
        char t[12];
        int k;
        int32_t xd = 237 - NVIEWS * 6, xt;
        caps(t, VIEW_NAME[ui.view], sizeof t);
        xt = xd - 5 - text_w(&FONT_XS, t);
        cv_rect(4, 5, xt - 9, 1, K_LINE);
        cv_text(xt, -1, &FONT_XS, t, K_ACC);
        for (k = 0; k < NVIEWS; k++)
            cv_round(xd + k * 6, 3, 4, 4, 2, k == ui.view ? K_ACC : K_LINE);
    }
    for (i = 0; i < 4; i++) {
        int k = VIEW_KNOB[ui.view][i], lo, hi, v, hot = now_touch && ui.touched == (int)i;
        int32_t x = (int32_t)i * 60, cx = x + 30;
        char t[16];
        const char *lab = k < P_NPARAMS ? en_param_label(k) : 0;
        uint16_t bg = hot ? K_PANEL : K_BG, fill = hot ? K_ACC : PART_COL[ui.view >= V_UPPER && ui.view <= V_CHORDS ? ui.view - V_UPPER : 0];
        if (k == K_NONE)
            continue;
        if (i)
            cv_rect(x, 16, 1, 46, K_LINE);
        if (hot)
            cv_round(x + 3, 11, 54, 57, 6, K_PANEL);
        knob_range(k, &lo, &hi);
        v = knob_get(k);
        caps(t, lab ? lab : KNOB_LABEL[ui.view][i], sizeof t);
        text_c(cx, 12, &FONT_XS, t, hot ? K_TEXT : K_DIM);
        if (k != K_CPU && hi - lo < 6) {         /* a switch: its positions as pips */
            int n = hi - lo + 1, j, pw = n > 4 ? 6 : 9, gap = 3, w0 = n * (pw + gap) - gap;
            for (j = 0; j < n; j++)
                cv_round(cx - w0 / 2 + j * (pw + gap), 33, pw, 8, 2, j == v - lo ? fill : K_LINE);
        } else if (k != K_CPU) {
            arc(cx, 37, 12, hi > lo ? (float)(v - lo) / (float)(hi - lo) : 0.0f, K_LINE, fill, bg, lo < 0);
        }
        knob_text(k, t);
        if (text_w(&FONT_B, t) > 56)             /* a long name (a cycle): the small face */
            text_c(cx, 52, &FONT_XS, t, hot ? K_ACC : K_TEXT);
        else
            text_c(cx, 50, &FONT_B, t, hot ? K_ACC : K_TEXT);
    }
    cv_blit(0, KNB_Y);
}

/* the lights: the page's button, PLAY, REC while a take records or LEARN; with SEL held the parts' buttons
 * (lit: playing) and the eight process keys. The keys show the chord: in NOTES its notes, all the way
 * up; in EASY the pads whose chord is in it, so the cycle can be followed and played along */
static void leds(void)
{
    uint32_t b = 0, k = 0, i, m = ui.learning && ui.gesture ? ui.g_mask : en_chord_mask;
    if (sel_held()) {
        int p;
        b |= 1u << B_SEL;
        for (p = 0; p < EN_NPART; p++)
            if (en_part_on[p])
                b |= 1u << PART_BTN[p];
        for (i = 0; i < 8; i++)
            k |= 1u << WHITE_KEY[i];
    } else {
        b |= 1u << VIEW_BTN[ui.view];
        if (proj.leds) {
            for (i = 0; i < NKEYS; i++) {
                if (proj.par[P_KEYS]) {
                    if (m >> ((5u + i) % 12u) & 1u)
                        k |= 1u << i;
                } else {
                    int bs;
                    uint32_t pm = en_pad((int)i, key_root_now(), key_minor(), &bs);
                    if (m && !(pm & ~m))
                        k |= 1u << i;
                }
            }
        }
    }
    if (en_playing)
        b |= 1u << B_PLAY;
    if (ui.learning || en_song_state == SONG_RECORDING)
        b |= 1u << B_REC;
    if (ui.mode == MODE_SONGS)
        b |= 1u << B_SAVE;                       /* SONGS: SAVE lit (held: delete) */
    plat_glow(proj.leds == 1);
    plat_play_red(en_song_state == SONG_RECORDING);
    plat_leds(b, k);
}

/* AUTOSAVE: a few seconds after a change, while stopped and quiet, nothing touched (a flash erase
 * silences the audio for a moment: never while it plays; SAVE saves then) */
#define AUTOSAVE_QUIET 4000u
static void autosave(void)
{
    uint32_t now = plat_ms();
    if (!ui.dirty || en_playing || ui.btn || ui.keys || now - ui.act_t < AUTOSAVE_QUIET || en_voices)
        return;
    if (project_save() == 0 && (!ui.song_dirty || ui.blob_slot < 0 || song_save(ui.blob_slot) == 0)) {
        ui.dirty = 0;
        ui.song_dirty = 0;
        ui.saved_t = now;
    }
}

void ui_init(void)
{
    memset(&ui, 0, sizeof ui);
    ui.touched = -1;
    ui.g_low = 128;
    ui.blob_slot = -1;
    ui.idea_r = 0x2545F491u;
    lcd_fill(0, 0, 240, 240, K_BG);
    if (!en_playing)
        en_play(1);                              /* it plays from the start */
}

void ui_frame(void)
{
    input();
    songs_watch();
    autosave();
    en_spell(key_root_now(), key_minor());       /* the names as the key spells them */
    draw_header();
    draw_main();
    draw_knobs();
    leds();
    ui.frame++;
}

void ui_input_only(void) { input(); }
