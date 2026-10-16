/* SPDX-License-Identifier: GPL-3.0-only */
/* The instrument: a minimalist ensemble in four MODELS, each a way the music of the 1960s-80s was made
 * from very little. Five parts play from one chord, which goes round a short cycle:
 *
 *   UPPER   an arpeggio high up; in BELLS the "bell" voice that answers the line with the chord's tones
 *   LOWER   an arpeggio lower down; in PHASE it drifts ahead of the upper and locks in again
 *   BASS    the chord's bass: long notes, a pulse, octaves, or root and fifth
 *   LEAD    a long line over the cycle; in PATTERNS a third player of the patterns; in BELLS the melody
 *   CHORDS  the chord held, swelling, pulsed, breathing, or a single high pulse
 *
 * The MODELS:
 *   ARPEGGIO  figures of the chord's tones, two against three; the ADDITIVE PROCESS grows and shrinks
 *             them a note at a time
 *   PHASE     upper and lower play figures at one pulse; the lower slowly gets ahead (PROCESS: how fast),
 *             locks in a step ahead for a while (EVOLVE: how long), and drifts on, round to unison
 *   PATTERNS  a chain of short patterns, each a little changed from the one before; upper, lower and
 *             lead each repeat their pattern as long as they like and move on, never far apart (PROCESS:
 *             how far), over a steady high pulse
 *   BELLS     the lead walks the scale by step in phrases that grow from one note; the upper answers
 *             each note with the nearest tone of the chord above it (or below), the lower turns the
 *             chord slowly
 *
 * The CYCLE is 1 to 8 chords, each a few beats long; a SECTION is a few times round. At a section's
 * start the texture moves (BUILD: parts in or out along a slow arc; EVOLVE: a figure, a chord, the key
 * a third away). The KEYS take the harmony: in EASY keys each white key is a chord of the key (the
 * black ones the same, richer), in NOTES the notes held are the chord.
 * IDEAS are seeds: a rolled cycle, a rolled figure, the lead's line and the patterns are each made from
 * a seed parameter, so an idea is a value, saved, undone, and played back as any other.
 * A SONG is a take: from the top, every command the render applies, with its tick; played back it is
 * the same music, note for note (the generators' dice are seeded with the take).
 *
 * The voices: a modal piano and marimba (damped resonators, the piano's partials stretched as a
 * string's), a combo organ, and one subtractive voice with patches for flute, strings, soprano sax,
 * horn and a synth bass; then Dattorro's plate (as AMB-1).
 *
 * Threads: en_render() runs in the audio interrupt. Everything else posts commands, which the render
 * drains at the start of a block; the state marked (UI) is written by the render and only read
 * elsewhere. Float only, no libm: fastmath.h. */
#pragma once
#include <stdint.h>

#define EN_SR 44100.0f
#define EN_PPQN 24                     /* clock ticks a beat (as MIDI clock) */
#define EN_NPART 5
enum { PT_UPPER, PT_LOWER, PT_BASS, PT_LEAD, PT_CHORDS };
#define EN_CYC_MAX 8                   /* chords in a cycle, at most */
#define EN_FIG_MAX 12                  /* a figure's steps, at most */
#define EN_NCYCLES 18                  /* the library's cycles; then Mine (learned) and Rolled (an idea) */
#define EN_CYC_MINE EN_NCYCLES
#define EN_CYC_ROLLED (EN_NCYCLES + 1)
#define EN_NFIG 8                      /* the library's figures; then Rolled */
#define EN_FIG_ROLLED EN_NFIG
#define EN_NPIECES 20
enum { M_ARPEGGIO, M_PHASE, M_PATTERNS, M_BELLS, EN_NMODELS };
extern const char *const EN_MODEL_NAME[EN_NMODELS];

/* the voices' instruments */
enum { I_PIANO, I_ORGAN, I_FLUTE, I_STRINGS, I_MARIMBA, I_SAX, I_HORN, I_SYNBASS, EN_NINST };
extern const char *const EN_INST_NAME[EN_NINST];
extern const char *const EN_NOTE_NAME[12];
extern const char *const EN_PART_NAME[EN_NPART];

/* parameters: value ranges in en_param_info() */
enum {
    P_EVOLVE, P_BUILD, P_PROCESS, P_CHORDLEN,             /* form */
    P_UP_LEVEL, P_UP_SOUND, P_UP_PULSE, P_UP_FIGURE,      /* upper */
    P_LO_LEVEL, P_LO_SOUND, P_LO_PULSE, P_LO_FIGURE,      /* lower */
    P_BS_LEVEL, P_BS_SOUND, P_BS_STYLE, P_BS_OCT,         /* bass */
    P_LD_LEVEL, P_LD_SOUND, P_LD_ACT, P_LD_HARM,          /* lead */
    P_CH_LEVEL, P_CH_SOUND, P_CH_RHYTHM, P_ROOM,          /* chords, and the room */
    P_TEMPO, P_ROOT, P_KEYS, P_HOLD,                      /* setup (Keys and Hold are the panel's) */
    P_MODEL, P_MIDI, P_CYCLE, P_UP_OCT,                   /* the model, MIDI out, the cycle (ALGORITHM), OCT */
    P_SEED_CYC, P_SEED_UP, P_SEED_LO, P_SEED_LD,          /* the ideas: a rolled cycle, figures, the line */
    P_NPARAMS
};
#define EN_SEED_MAX 32767
typedef struct {
    const char *name;
    int16_t lo, hi, def;
} en_param_t;
const en_param_t *en_param_info(int i);
void en_param_text(int i, int v, char *buf);   /* the value as the screen shows it (16 bytes) */
const char *en_param_label(int i);             /* the knob's name in the playing model ("Figure", "Bells" ...) */

/* commands (main loop -> render) */
void en_init(void);
void en_set(int param, int value);
void en_play(int on);                          /* play / stop (the tails ring on) */
void en_next(void);                            /* a new section, from the cycle's start, at the next chord */
void en_seed(uint32_t s);                      /* stir some entropy into the generators */
void en_panic(void);                           /* everything quiet, now */
void en_regrow(void);                          /* a new piece: the cycle from the top, a fresh form */
/* the player's chord: pitch classes (bit i = class i, absolute) and the bass's class; mask 0 = let go.
 * keys = how many keys make it: one key asks for the triad of the cycle's key on that note */
void en_hold(uint32_t mask, int bass, int keys);
/* "Mine": the learned cycle, relative to the Root (bit 0 = the root): n chords (0: none) */
void en_mine(int n, const uint16_t *mask, const uint8_t *bass);
/* a part in (1) or out (0) by hand, at the next chord */
void en_part(int part, int on);
/* a process by hand on an arpeggio (PT_UPPER, PT_LOWER, or 2 for both), at the start of its next pass */
enum { V_GROW, V_SHRINK, V_SHIFT, V_FLIP, V_STILL, EN_NVERBS };
void en_verb(int part, int verb);
/* MIDI clock in: ticks drive the grid (24 a beat) while they keep coming */
enum { EN_CLK_TICK, EN_CLK_START, EN_CLK_CONTINUE, EN_CLK_STOP };
void en_clock(int msg);

/* the render (audio ISR): n stereo frames, 24-bit in int32; gain Q12 (the MASTER pot) */
void en_render(int32_t *out_lr, uint32_t n, uint32_t gain_q12);
/* the overload guard: each block's load in % of its time, after rendering it (the render's context) */
void en_load(uint32_t pct, uint32_t frames);
extern volatile uint8_t en_guarding;           /* (UI) the guard is lightening the load */
extern volatile uint32_t en_guard_count;       /* (UI) times it came on */

/* MIDI out, from the render: the app sends it (USB); status, data 1, data 2 */
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2);

/* SONGS: a take, from the top: its start (every parameter, Mine, the dice) and each command with its
 * tick. The app keeps the takes in flash; the render records into and plays from this one */
#define EN_SONG_MAX 1880                       /* commands in a take */
#define EN_SONG_MAGIC 0x31474E53u              /* "SNG1" */
typedef struct {
    uint32_t magic, n, seed, end;              /* the commands, the dice at the start, the length (ticks) */
    int16_t par[P_NPARAMS];                    /* every value as the take began */
    uint8_t mine_n, piece, rsv[2];
    uint16_t mine_mask[EN_CYC_MAX];
    uint8_t mine_bass[EN_CYC_MAX];
    uint32_t ev[EN_SONG_MAX][2];               /* tick, command */
} en_song_t;
#define EN_SONG_BYTES(n) ((uint32_t)(sizeof(en_song_t) - sizeof(((en_song_t *)0)->ev) + 8u * (uint32_t)(n)))
en_song_t *en_song(void);                      /* the take in memory: the app reads it to save, writes it to
                                                * load only while no take records or plays */
enum { EN_SONG_REC, EN_SONG_STOP, EN_SONG_PLAY };
void en_song_cmd(int what, int piece);         /* REC: a take from the top (piece: kept in it); PLAY; STOP */
enum { SONG_IDLE, SONG_RECORDING, SONG_PLAYING };
extern volatile uint8_t en_song_state;         /* (UI) */
extern volatile uint32_t en_song_tick;         /* (UI) ticks into the take */
extern volatile uint8_t en_song_full;          /* (UI) the last take stopped full */

const int16_t *en_params(void);                /* (UI) every value as the render has it (a take playing sets them) */

/* state for the screen (UI) */
extern volatile uint8_t en_playing, en_ext;
extern volatile uint8_t en_cyc_n, en_cyc_pos, en_cyc_rep, en_cyc_reps;   /* the cycle: chords, where, time round, of */
extern volatile uint16_t en_cyc_mask[EN_CYC_MAX];  /* the cycle's chords as this section plays them (absolute) */
extern volatile uint8_t en_cyc_bass[EN_CYC_MAX];
extern volatile uint16_t en_chord_mask;        /* the chord sounding */
extern volatile uint8_t en_chord_bass;
extern volatile uint8_t en_yours;              /* the chord sounding is the player's */
extern volatile float en_chord_t;              /* 0..1 through the chord */
extern volatile int8_t en_shift;               /* the cycle moved this section, semitones (0: home) */
extern volatile uint8_t en_part_on[EN_NPART];  /* playing in this section */
extern volatile float en_part_glow[EN_NPART];  /* 0..1: a note just played */
extern volatile uint8_t en_part_div[EN_NPART]; /* notes a beat (the arpeggios) */
extern volatile uint8_t en_fig_len[EN_NPART], en_fig_full[EN_NPART], en_fig_pos[EN_NPART];   /* the figures */
extern volatile uint8_t en_fig_proc[EN_NPART]; /* 0 still, 1 adding, 2 taking away */
extern volatile uint8_t en_fig_user[EN_NPART]; /* a process by hand holds it */
extern volatile float en_phase;                /* PHASE: the lower's lead, in steps */
extern volatile uint8_t en_pat_at[EN_NPART], en_pat_n;   /* PATTERNS: each player's pattern, of */
extern volatile uint32_t en_beats;             /* beats since the start */
extern volatile uint32_t en_section;           /* sections since the start */
extern volatile float en_arc;                  /* the build's arc, -1 sparse .. +1 full */
extern volatile uint32_t en_notes[EN_NPART];   /* notes played, per part (tests) */
extern volatile uint8_t en_voices;             /* voices sounding (tests) */

/* the pure parts, for the tests and the screen */
/* chord k of a cycle (the library's, Mine, or Rolled from the seed), relative to the root; returns its length */
int en_cycle_get(int cycle, int seed, int k, uint16_t *mask, uint8_t *bass);
const char *en_cycle_name(int cycle);          /* a name for it ("Cadence", "Mine", "Rolled") */
const char *en_cycle_numerals(int cycle);      /* its numerals ("i VI iv V"); "" for Mine and Rolled */
int en_cycle_minor(int cycle, int seed);       /* Rolled: the seed's last bit */
int en_cycle_roll(uint32_t seed, int minor, uint16_t *mask, uint8_t *bass);   /* a rolled cycle: 2..6 chords */
uint16_t en_triad(int pc, int root, int minor);    /* the triad of a key on one note (absolute) */
/* EASY keys: key k (0 = F3 .. 26 = G5) of the FM-1 as a chord of the key: white keys the scale's seven
 * triads from the leftmost (the tonic) up, black keys the same as the white key left of them, with its
 * seventh; returns the chord (absolute) and its root in *bass */
uint16_t en_pad(int key, int root, int minor, int *bass);
const char *en_chord_name(uint32_t mask, int bass, char *buf);   /* "Am", "F", "G7", "Am/G" ... (16 bytes) */
/* note names as the key spells them: its own notes with its sharps or flats, the others flat, a minor
 * key's leading tone sharp. en_spell sets the key (the main loop's; the render never names notes) */
void en_spell(int root, int minor);
const char *en_note_name(int pc);
int en_figure(int fig, int seed, int8_t *steps); /* a figure's steps (Rolled: from the seed); its length */
const char *en_figure_name(int fig);
/* voice leading: new notes for the tones, each voice moving as little as possible, all in lo..hi */
void en_voice_lead(const int *from, const int *pcs, int ntones, int *to, int lo, int hi);

/* the pieces: recipes for every parameter (Keys, Hold and MIDI out are the player's) */
const char *en_piece_name(int i);
void en_piece(int i, int16_t *par);
