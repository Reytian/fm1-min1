/* SPDX-License-Identifier: GPL-3.0-only */
/* MIN-1 project: one flash object (OBJ_PROJ), and the songs: a take each in up to SONG_OBJS objects.
 * A project of another format, or none, gives the defaults. Part of the unity build, after app.h. (AMB-1's.)
 *
 * The pieces (their recipes are the engine's, en_piece): each keeps its own settings as you leave it,
 * and its own Mine (the cycle learned with LEARN); Keys, Hold and MIDI out are global. */
project_t proj;

const char *piece_name(int i) { return en_piece_name(i); }

static int global_par(int p) { return p == P_MIDI || p == P_KEYS || p == P_HOLD; }

static void recipe(int i, slot_t *s) { en_piece(i, s->par); }

static void take(const slot_t *s)                /* a piece's settings into play, the globals kept */
{
    int p;
    for (p = 0; p < P_NPARAMS; p++)
        if (!global_par(p))
            proj.par[p] = s->par[p];
}

static void keep(void)                           /* the piece playing, into its slot */
{
    int p;
    for (p = 0; p < P_NPARAMS; p++)
        proj.slot[proj.piece].par[p] = proj.par[p];
}

void project_defaults(void)
{
    int i;
    memset(&proj, 0, sizeof proj);
    proj.magic = PROJ_MAGIC;
    proj.format = PROJ_FORMAT;
    for (i = 0; i < NPIECES; i++)
        recipe(i, &proj.slot[i]);
    for (i = 0; i < P_NPARAMS; i++)
        proj.par[i] = en_param_info(i)->def;
    take(&proj.slot[0]);
    proj.leds = 1;
}

static int pars_ok(const int16_t *par)
{
    int i;
    for (i = 0; i < P_NPARAMS; i++) {
        const en_param_t *p = en_param_info(i);
        if (par[i] < p->lo || par[i] > p->hi)
            return 0;
    }
    return 1;
}

static int mine_ok(const slot_t *s)
{
    int k;
    if (s->mine_n > EN_CYC_MAX)
        return 0;
    for (k = 0; k < s->mine_n; k++)
        if ((s->mine_mask[k] >> 12) || s->mine_bass[k] > 11u)
            return 0;
    return 1;
}

static int project_valid(void)
{
    int i;
    if (proj.magic != PROJ_MAGIC || proj.format != PROJ_FORMAT || proj.piece >= NPIECES || proj.leds > 2u ||
        proj.song >= NSONGS || !pars_ok(proj.par))
        return 0;
    for (i = 0; i < NPIECES; i++)
        if (!pars_ok(proj.slot[i].par) || !mine_ok(&proj.slot[i]))
            return 0;
    return 1;
}

int project_load(void)
{
    int n;
    memset(&proj, 0, sizeof proj);
    n = plat_store_load(OBJ_PROJ, &proj, sizeof proj);
    if (n == (int)sizeof proj && project_valid())
        return 0;
    project_defaults();
    return -1;
}

int project_save(void)
{
    keep();
    return plat_store_save(OBJ_PROJ, &proj, sizeof proj);
}

void project_piece(int i)
{
    keep();
    proj.piece = (uint8_t)((i % NPIECES + NPIECES) % NPIECES);
    take(&proj.slot[proj.piece]);
}

void project_piece_reset(void)
{
    slot_t s;
    recipe(proj.piece, &s);
    take(&s);
}

void project_mine(int n, const uint16_t *mask, const uint8_t *bass)
{
    slot_t *s = &proj.slot[proj.piece];
    int k;
    s->mine_n = (uint8_t)(n < 0 ? 0 : n > EN_CYC_MAX ? EN_CYC_MAX : n);
    for (k = 0; k < EN_CYC_MAX; k++) {
        s->mine_mask[k] = k < s->mine_n ? (uint16_t)(mask[k] & 0xFFFu) : 0;
        s->mine_bass[k] = k < s->mine_n ? (uint8_t)(bass[k] % 12u) : 0;
    }
}

void project_apply(void)
{
    const slot_t *s = &proj.slot[proj.piece];
    int i;
    en_mine(s->mine_n, s->mine_mask, s->mine_bass);  /* Mine before the Cycle, which may ask for it */
    for (i = 0; i < P_NPARAMS; i++)
        en_set(i, proj.par[i]);
}

/* a take: the header and its commands, cut into objects of PLAT_STORE_MAX bytes */
int song_save(int k)
{
    const uint8_t *b = (const uint8_t *)en_song();
    uint32_t bytes = EN_SONG_BYTES(en_song()->n), off;
    int c, rc;
    if (k < 0 || k >= NSONGS || en_song()->magic != EN_SONG_MAGIC || bytes > SONG_OBJS * PLAT_STORE_MAX)
        return -1;
    for (c = 0, off = 0; off < bytes; c++, off += PLAT_STORE_MAX)
        if ((rc = plat_store_save(OBJ_SONG0 + (uint32_t)(k * SONG_OBJS + c), b + off,
                                  bytes - off > PLAT_STORE_MAX ? PLAT_STORE_MAX : bytes - off)) != 0)
            return rc;
    return 0;
}

int song_load(int k)
{
    en_song_t *s = en_song();
    uint8_t *b = (uint8_t *)s;
    uint32_t bytes, off;
    int c, n;
    if (k < 0 || k >= NSONGS)
        return -1;
    n = plat_store_load(OBJ_SONG0 + (uint32_t)(k * SONG_OBJS), b, PLAT_STORE_MAX);
    if (n < (int)EN_SONG_BYTES(0) || s->magic != EN_SONG_MAGIC || s->n > EN_SONG_MAX) {
        s->magic = 0;
        return -1;
    }
    bytes = EN_SONG_BYTES(s->n);
    for (c = 1, off = PLAT_STORE_MAX; off < bytes; c++, off += PLAT_STORE_MAX) {
        uint32_t want = bytes - off > PLAT_STORE_MAX ? PLAT_STORE_MAX : bytes - off;
        if (plat_store_load(OBJ_SONG0 + (uint32_t)(k * SONG_OBJS + c), b + off, want) != (int)want) {
            s->magic = 0;
            return -1;
        }
    }
    return 0;
}
