/* Extended Module Player
 * Copyright (C) 1996-2012 Claudio Matsuoka and Hipolito Carraro Jr
 *
 * This file is part of the Extended Module Player and is distributed
 * under the terms of the GNU General Public License. See doc/COPYING
 * for more information.
 */

/* Note: envelope switching (effect 9) and sample status change (effect 8)
 * are not supported. Frequency envelopes will be added in xmp 1.2.0. In
 * MDL envelopes are defined for each instrument in a patch -- xmp accepts
 * only one envelope for each patch, and the enveope of the first instrument
 * will be taken.
 */

#include "load.h"
#include "iff.h"
#include "period.h"

#define MAGIC_DMDL	MAGIC4('D','M','D','L')


static int mdl_test (FILE *, char *, const int);
static int mdl_load (struct module_data *, FILE *, const int);

const struct format_loader mdl_loader = {
    "Digitrakker (MDL)",
    mdl_test,
    mdl_load
};

static int mdl_test(FILE *f, char *t, const int start)
{
    uint16 id;

    if (read32b(f) != MAGIC_DMDL)
	return -1;

    read8(f);			/* version */
    id = read16b(f);

    if (id == 0x494e) {		/* IN */
	read32b(f);
	read_title(f, t, 32);
    } else {
	read_title(f, t, 0);
    }

    return 0;
}


#define MDL_NOTE_FOLLOWS	0x04
#define MDL_INSTRUMENT_FOLLOWS	0x08
#define MDL_VOLUME_FOLLOWS	0x10
#define MDL_EFFECT_FOLLOWS	0x20
#define MDL_PARAMETER1_FOLLOWS	0x40
#define MDL_PARAMETER2_FOLLOWS	0x80


struct mdl_envelope {
    uint8 num;
    uint8 data[30];
    uint8 sus;
    uint8 loop;
};

struct local_data {
    int *i_index;
    int *s_index;
    int *v_index;	/* volume envelope */
    int *p_index;	/* pan envelope */
    int *f_index;	/* pitch envelope */
    int *c2spd;
    int *packinfo;
    int v_envnum;
    int p_envnum;
    int f_envnum;
    struct mdl_envelope *v_env;
    struct mdl_envelope *p_env;
    struct mdl_envelope *f_env;
};


/* Effects 1-6 (note effects) can only be entered in the first effect
 * column, G-L (volume-effects) only in the second column.
 */

static void xlat_fx_common(uint8 *t, uint8 *p)
{
    switch (*t) {
    case 0x00:			/* - - No effect */
	*p = 0;
	break;
    case 0x07:			/* 7 - Set BPM */
	*t = FX_S3M_BPM;
	break;
    case 0x08:			/* 8 - Set pan */
    case 0x09:			/* 9 - Set envelope -- not supported */
    case 0x0a:			/* A - Not used */
	*t = *p = 0x00;
	break;
    case 0x0b:			/* B - Position jump */
    case 0x0c:			/* C - Set volume */
    case 0x0d:			/* D - Pattern break */
	/* Like protracker */
	break;
    case 0x0e:			/* E - Extended */
	switch (MSN (*p)) {
	case 0x0:		/* E0 - not used */
	case 0x3:		/* E3 - not used */
	case 0x8:		/* Set sample status -- unsupported */
	    *t = *p = 0x00;
	    break;
	case 0x1:		/* Pan slide left */
	    *t = FX_PANSLIDE;
	    *p <<= 4;
	    break;
	case 0x2:		/* Pan slide right */
	    *t = FX_PANSLIDE;
	    *p &= 0x0f;
	    break;
	}
	break;
    case 0x0f:
	*t = FX_S3M_SPEED;
	break;
    }
}

static void xlat_fx1(uint8 *t, uint8 *p)
{
    switch (*t) {
    case 0x05:			/* 5 - Arpeggio */
	*t = FX_ARPEGGIO;
	break;
    case 0x06:			/* 6 - Not used */
	*t = *p = 0x00;
	break;
    }

    xlat_fx_common(t, p);
}


static void xlat_fx2(uint8 *t, uint8 *p)
{
    switch (*t) {
    case 0x01:			/* G - Volume slide up */
	*t = FX_VOLSLIDE_UP;
	break;
    case 0x02:			/* H - Volume slide down */
	*t = FX_VOLSLIDE_DN;
	break;
    case 0x03:			/* I - Multi-retrig */
	*t = FX_MULTI_RETRIG;
	break;
    case 0x04:			/* J - Tremolo */
	*t = FX_TREMOLO;
	break;
    case 0x05:			/* K - Tremor */
	*t = FX_TREMOR;
	break;
    case 0x06:			/* L - Not used */
	*t = *p = 0x00;
	break;
    }

    xlat_fx_common(t, p);
}

struct bits {
	uint32 b, n;
};

static unsigned int get_bits(char i, uint8 **buf, int *len, struct bits *bits)
{
    unsigned int x;

    if (i == 0) {
	bits->b = readmem32l(*buf);
	*buf += 4; *len -= 4;
	bits->n = 32;
	return 0;
    }

    x = bits->b & ((1 << i) - 1);	/* get i bits */
    bits->b >>= i;
    if ((bits->n -= i) <= 24) {
	if (*len == 0)		/* FIXME: last few bits can't be consumed */
		return x;
	bits->b |= readmem32l((*buf)++) << bits->n;
	bits->n += 8; (*len)--;
    }

    return x;
}

/* From the Digitrakker docs:
 *
 * The description of the sample-packmethode (1) [8bit packing]:...
 * ----------------------------------------------------------------
 *
 * The method is based on the Huffman algorithm. It's easy and very fast
 * and effective on samples. The packed sample is a bit stream:
 *
 *	     Byte 0    Byte 1    Byte 2    Byte 3
 *	Bit 76543210  fedcba98  nmlkjihg  ....rqpo
 *
 * A packed byte is stored in the following form:
 *
 *	xxxx10..0s => byte = <xxxx> + (number of <0> bits between
 *		s and 1) * 16 - 8;
 *	if s==1 then byte = byte xor 255
 *
 * If there are no <0> bits between the first bit (sign) and the <1> bit,
 * you have the following form:
 *
 *	xxx1s => byte = <xxx>; if s=1 then byte = byte xor 255
 */

static void unpack_sample8(uint8 *t, uint8 *f, int len, int l)
{
    int i, s;
    uint8 b, d;
    struct bits bits;

    get_bits(0, &f, &len, &bits);

    for (i = b = d = 0; i < l; i++) {
	s = get_bits(1, &f, &len, &bits);
	if (get_bits(1, &f, &len, &bits)) {
	    b = get_bits(3, &f, &len, &bits);
	} else {
            b = 8;
	    while (len >= 0 && !get_bits(1, &f, &len, &bits))
		b += 16;
	    b += get_bits(4, &f, &len, &bits);
	}

	if (s)
	    b ^= 0xff;

	d += b;
	*t++ = d;
    }
}

/*
 * The description of the sample-packmethode (2) [16bit packing]:...
 * ----------------------------------------------------------------
 *
 * It works as methode (1) but it only crunches every 2nd byte (the high-
 * bytes of 16 bit samples). So when you depack 16 bit samples, you have to
 * read 8 bits from the data-stream first. They present the lowbyte of the
 * sample-word. Then depack the highbyte in the descripted way (methode [1]).
 * Only the highbytes are delta-values. So take the lowbytes as they are.
 * Go on this way for the whole sample!
 */

static void unpack_sample16(uint8 *t, uint8 *f, int len, int l)
{
    int i, lo, s;
    uint8 b, d;
    struct bits bits;

    get_bits(0, &f, &len, &bits);

    for (i = lo = b = d = 0; i < l; i++) {
	lo = get_bits(8, &f, &len, &bits);
	s = get_bits(1, &f, &len, &bits);
	if (get_bits(1, &f, &len, &bits)) {
	    b = get_bits(3, &f, &len, &bits);
	} else {
            b = 8;
	    while (len >= 0 && !get_bits (1, &f, &len, &bits))
		b += 16;
	    b += get_bits(4, &f, &len, &bits);
	}

	if (s)
	    b ^= 0xff;
	d += b;

	*t++ = lo;
	*t++ = d;
    }
}


/*
 * IFF chunk handlers
 */

static void get_chunk_in(struct module_data *m, int size, FILE *f, void *parm)
{
    struct xmp_module *mod = &m->mod;
    int i;

    fread(mod->name, 1, 32, f);
    fseek(f, 20, SEEK_CUR);

    mod->len = read16l(f);
    mod->rst = read16l(f);
    read8(f);			/* gvol */
    mod->spd = read8(f);
    mod->bpm = read8(f);

    for (i = 0; i < 32; i++) {
	uint8 chinfo = read8(f);
	if (chinfo & 0x80)
	    break;
	mod->xxc[i].pan = chinfo << 1;
    }
    mod->chn = i;
    fseek(f, 32 - i - 1, SEEK_CUR);

    fread(mod->xxo, 1, mod->len, f);

    MODULE_INFO();
}

static void get_chunk_pa(struct module_data *m, int size, FILE *f, void *parm)
{
    struct xmp_module *mod = &m->mod;
    int i, j, chn;
    int x;

    mod->pat = read8(f);
    mod->trk = mod->pat * mod->chn + 1;	/* Max */

    PATTERN_INIT();
    _D(_D_INFO "Stored patterns: %d", mod->pat);

    for (i = 0; i < mod->pat; i++) {
	PATTERN_ALLOC (i);
	chn = read8(f);
	mod->xxp[i]->rows = (int)read8(f) + 1;

	fseek(f, 16, SEEK_CUR);		/* Skip pattern name */
	for (j = 0; j < chn; j++) {
	    x = read16l(f);
	    if (j < mod->chn)
		mod->xxp[i]->index[j] = x;
	}
    }
}

static void get_chunk_p0(struct module_data *m, int size, FILE *f, void *parm)
{
    struct xmp_module *mod = &m->mod;
    int i, j;
    uint16 x16;

    mod->pat = read8(f);
    mod->trk = mod->pat * mod->chn + 1;	/* Max */

    PATTERN_INIT();
    _D(_D_INFO "Stored patterns: %d", mod->pat);

    for (i = 0; i < mod->pat; i++) {
	PATTERN_ALLOC (i);
	mod->xxp[i]->rows = 64;

	for (j = 0; j < 32; j++) {
	    x16 = read16l(f);
	    if (j < mod->chn)
		mod->xxp[i]->index[j] = x16;
	}
    }
}

static void get_chunk_tr(struct module_data *m, int size, FILE *f, void *parm)
{
    struct xmp_module *mod = &m->mod;
    int i, j, k, row, len;
    struct xmp_track *track;

    mod->trk = read16l(f) + 1;
    mod->xxt = realloc(mod->xxt, sizeof (struct xmp_track *) * mod->trk);

    _D(_D_INFO "Stored tracks: %d", mod->trk);

    track = calloc (1, sizeof (struct xmp_track) +
	sizeof (struct xmp_event) * 256);

    /* Empty track 0 is not stored in the file */
    mod->xxt[0] = calloc (1, sizeof (struct xmp_track) +
	256 * sizeof (struct xmp_event));
    mod->xxt[0]->rows = 256;

    for (i = 1; i < mod->trk; i++) {
	/* Length of the track in bytes */
	len = read16l(f);

	memset (track, 0, sizeof (struct xmp_track) +
            sizeof (struct xmp_event) * 256);

	for (row = 0; len;) {
	    j = read8(f);
	    len--;
	    switch (j & 0x03) {
	    case 0:
		row += j >> 2;
		break;
	    case 1:
		for (k = 0; k <= (j >> 2); k++)
		    memcpy (&track->event[row + k], &track->event[row - 1],
			sizeof (struct xmp_event));
		row += k - 1;
		break;
	    case 2:
		memcpy (&track->event[row], &track->event[j >> 2],
		    sizeof (struct xmp_event));
		break;
	    case 3:
		if (j & MDL_NOTE_FOLLOWS) {
		    uint8 b = read8(f);
		    len--;
		    track->event[row].note = b == 0xff ? XMP_KEY_OFF : b + 12;
		}
		if (j & MDL_INSTRUMENT_FOLLOWS)
		    len--, track->event[row].ins = read8(f);
		if (j & MDL_VOLUME_FOLLOWS)
		    len--, track->event[row].vol = read8(f);
		if (j & MDL_EFFECT_FOLLOWS) {
		    len--, k = read8(f);
		    track->event[row].fxt = LSN (k);
		    track->event[row].f2t = MSN (k);
		}
		if (j & MDL_PARAMETER1_FOLLOWS)
		    len--, track->event[row].fxp = read8(f);
		if (j & MDL_PARAMETER2_FOLLOWS)
		    len--, track->event[row].f2p = read8(f);
		break;
	    }

	    xlat_fx1(&track->event[row].fxt, &track->event[row].fxp);
	    xlat_fx2(&track->event[row].f2t, &track->event[row].f2p);

	    row++;
	}

	if (row <= 64)
	    row = 64;
	else if (row <= 128)
	    row = 128;
	else row = 256;

	mod->xxt[i] = calloc (1, sizeof (struct xmp_track) +
	    sizeof (struct xmp_event) * row);
	memcpy(mod->xxt[i], track, sizeof (struct xmp_track) +
	    sizeof (struct xmp_event) * row);
	mod->xxt[i]->rows = row;
    }

    free(track);
}

static void get_chunk_ii(struct module_data *m, int size, FILE *f, void *parm)
{
    struct xmp_module *mod = &m->mod;
    struct local_data *data = (struct local_data *)parm;
    int i, j, k;
    int map, last_map;
    char buf[40];

    mod->ins = read8(f);
    _D(_D_INFO "Instruments: %d", mod->ins);

    INSTRUMENT_INIT();

    for (i = 0; i < mod->ins; i++) {
	data->i_index[i] = read8(f);
	mod->xxi[i].nsm = read8(f);
	fread(buf, 1, 32, f);
	buf[32] = 0;
	str_adj(buf);
	strncpy((char *)mod->xxi[i].name, buf, 32);

	_D(_D_INFO "[%2X] %-32.32s %2d", i_index[i],
				mod->xxi[i].name, mod->xxi[i].nsm);

	mod->xxi[i].sub = calloc (sizeof (struct xmp_subinstrument), mod->xxi[i].nsm);

	for (j = 0; j < XMP_MAX_KEYS; j++)
	    mod->xxi[i].map[j].ins = -1;

	for (last_map = j = 0; j < mod->xxi[i].nsm; j++) {
	    int x;

	    mod->xxi[i].sub[j].sid = read8(f);
	    map = read8(f) + 12;
	    mod->xxi[i].sub[j].vol = read8(f);
	    for (k = last_map; k <= map; k++) {
		if (k < XMP_MAX_KEYS)
		    mod->xxi[i].map[k].ins = j;
	    }
	    last_map = map + 1;

	    x = read8(f);		/* Volume envelope */
	    if (j == 0)
		data->v_index[i] = x & 0x80 ? x & 0x3f : -1;
	    if (~x & 0x40)
		mod->xxi[i].sub[j].vol = 0xff;

	    mod->xxi[i].sub[j].pan = read8(f) << 1;

	    x = read8(f);		/* Pan envelope */
	    if (j == 0)
		data->p_index[i] = x & 0x80 ? x & 0x3f : -1;
	    if (~x & 0x40)
		mod->xxi[i].sub[j].pan = 0x80;

	    x = read16l(f);
	    if (j == 0)
		mod->xxi[i].rls = x;

	    mod->xxi[i].sub[j].vra = read8(f);	/* vibrato rate */
	    mod->xxi[i].sub[j].vde = read8(f);	/* vibrato delay */
	    mod->xxi[i].sub[j].vsw = read8(f);	/* vibrato sweep */
	    mod->xxi[i].sub[j].vwf = read8(f);	/* vibrato waveform */
	    read8(f);			/* Reserved */

	    x = read8(f);		/* Pitch envelope */
	    if (j == 0)
		data->f_index[i] = x & 0x80 ? x & 0x3f : -1;

	    _D(_D_INFO "  %2x: V%02x S%02x v%02x p%02x f%02x",
				j, mod->xxi[i].sub[j].vol, mod->xxi[i].sub[j].sid,
				v_index[i], p_index[i], f_index[i]);
	}
    }
}

static void get_chunk_is(struct module_data *m, int size, FILE *f, void *parm)
{
    struct xmp_module *mod = &m->mod;
    struct local_data *data = (struct local_data *)parm;
    int i;
    char buf[64];
    uint8 x;

    mod->smp = read8(f);
    mod->xxs = calloc(sizeof (struct xmp_sample), mod->smp);
    data->packinfo = calloc(sizeof (int), mod->smp);

    _D(_D_INFO "Sample infos: %d", mod->smp);

    for (i = 0; i < mod->smp; i++) {
	data->s_index[i] = read8(f);		/* Sample number */
	fread(buf, 1, 32, f);
	buf[32] = 0;
	str_adj(buf);

	fseek(f, 8, SEEK_CUR);		/* Sample filename */

	data->c2spd[i] = read32l(f);

	mod->xxs[i].len = read32l(f);
	mod->xxs[i].lps = read32l(f);
	mod->xxs[i].lpe = read32l(f);

	mod->xxs[i].flg = mod->xxs[i].lpe > 0 ? XMP_SAMPLE_LOOP : 0;
	mod->xxs[i].lpe = mod->xxs[i].lps + mod->xxs[i].lpe;
	if (mod->xxs[i].lpe > 0)
	    mod->xxs[i].lpe--;

	read8(f);			/* Volume in DMDL 0.0 */
	x = read8(f);
	if (x & 0x01) {
	    mod->xxs[i].flg |= XMP_SAMPLE_16BIT;
	    mod->xxs[i].len >>= 1;
	    mod->xxs[i].lps >>= 1;
	    mod->xxs[i].lpe >>= 1;
        }
	mod->xxs[i].flg |= (x & 0x02) ? XMP_SAMPLE_LOOP_BIDIR : 0;
	data->packinfo[i] = (x & 0x0c) >> 2;

	_D(_D_INFO "[%2X] %-32.32s %05x%c %05x %05x %c %6d %d",
			s_index[i], buf,
			mod->xxs[i].len,
			mod->xxs[i].flg & XMP_SAMPLE_16BIT ? '+' : ' ',
			mod->xxs[i].lps,
			mod->xxs[i].lpe,
			mod->xxs[i].flg & XMP_SAMPLE_LOOP ? 'L' : ' ',
			data->c2spd[i], data->packinfo[i]);
    }
}

static void get_chunk_i0(struct module_data *m, int size, FILE *f, void *parm)
{
    struct xmp_module *mod = &m->mod;
    struct local_data *data = (struct local_data *)parm;
    int i;
    char buf[64];
    uint8 x;

    mod->ins = mod->smp = read8(f);

    _D(_D_INFO "Instruments: %d", mod->ins);

    INSTRUMENT_INIT();

    data->packinfo = calloc (sizeof (int), mod->smp);

    for (i = 0; i < mod->ins; i++) {
	mod->xxi[i].nsm = 1;
	mod->xxi[i].sub = calloc(sizeof (struct xmp_subinstrument), 1);
	mod->xxi[i].sub[0].sid = data->i_index[i] = data->s_index[i] = read8(f);

	fread(buf, 1, 32, f);
	buf[32] = 0;
	str_adj(buf);			/* Sample name */
	fseek(f, 8, SEEK_CUR);		/* Sample filename */

	data->c2spd[i] = read16l(f);

	mod->xxs[i].len = read32l(f);
	mod->xxs[i].lps = read32l(f);
	mod->xxs[i].lpe = read32l(f);

	mod->xxs[i].flg = mod->xxs[i].lpe > 0 ? XMP_SAMPLE_LOOP : 0;
	mod->xxs[i].lpe = mod->xxs[i].lps + mod->xxs[i].lpe;

	mod->xxi[i].sub[0].vol = read8(f);	/* Volume */
	mod->xxi[i].sub[0].pan = 0x80;

	x = read8(f);
	if (x & 0x01) {
	    mod->xxs[i].flg |= XMP_SAMPLE_16BIT;
	    mod->xxs[i].len >>= 1;
	    mod->xxs[i].lps >>= 1;
	    mod->xxs[i].lpe >>= 1;
	}
	mod->xxs[i].flg |= (x & 0x02) ? XMP_SAMPLE_LOOP_BIDIR : 0;
	data->packinfo[i] = (x & 0x0c) >> 2;

	_D(_D_INFO "[%2X] %-32.32s %5d V%02x %05x%c %05x %05x %d",
		i_index[i], buf, c2spd[i],  mod->xxi[i].sub[0].vol,
		mod->xxs[i].len,mod->xxs[i].flg & XMP_SAMPLE_16BIT ? '+' : ' ',
		mod->xxs[i].lps, mod->xxs[i].lpe, packinfo[i]);
    }
}

static void get_chunk_sa(struct module_data *m, int size, FILE *f, void *parm)
{
    struct xmp_module *mod = &m->mod;
    struct local_data *data = (struct local_data *)parm;
    int i, len;
    uint8 *smpbuf, *buf;

    _D(_D_INFO "Stored samples: %d", mod->smp);

    for (i = 0; i < mod->smp; i++) {
	smpbuf = calloc (1, mod->xxs[i].flg & XMP_SAMPLE_16BIT ?
		mod->xxs[i].len << 1 : mod->xxs[i].len);

	switch (data->packinfo[i]) {
	case 0:
	    fread(smpbuf, 1, mod->xxs[i].len, f);
	    break;
	case 1: 
	    len = read32l(f);
	    buf = malloc(len + 4);
	    fread(buf, 1, len, f);
	    unpack_sample8(smpbuf, buf, len, mod->xxs[i].len);
	    free(buf);
	    break;
	case 2:
	    len = read32l(f);
	    buf = malloc(len + 4);
	    fread(buf, 1, len, f);
	    unpack_sample16(smpbuf, buf, len, mod->xxs[i].len);
	    free(buf);
	    break;
	}
	
	load_sample(NULL, SAMPLE_FLAG_NOLOAD, &mod->xxs[i], (char *)smpbuf);

	free (smpbuf);
    }

    free(data->packinfo);
}

static void get_chunk_ve(struct module_data *m, int size, FILE *f, void *parm)
{
    struct local_data *data = (struct local_data *)parm;
    int i;

    if ((data->v_envnum = read8(f)) == 0)
	return;

    _D(_D_INFO "Vol envelopes: %d", data->v_envnum);

    data->v_env = calloc(data->v_envnum, sizeof (struct mdl_envelope));

    for (i = 0; i < data->v_envnum; i++) {
	data->v_env[i].num = read8(f);
	fread(data->v_env[i].data, 1, 30, f);
	data->v_env[i].sus = read8(f);
	data->v_env[i].loop = read8(f);
    }
}

static void get_chunk_pe(struct module_data *m, int size, FILE *f, void *parm)
{
    struct local_data *data = (struct local_data *)parm;
    int i;

    if ((data->p_envnum = read8(f)) == 0)
	return;

    _D(_D_INFO "Pan envelopes: %d", data->p_envnum);

    data->p_env = calloc(data->p_envnum, sizeof (struct mdl_envelope));

    for (i = 0; i < data->p_envnum; i++) {
	data->p_env[i].num = read8(f);
	fread(data->p_env[i].data, 1, 30, f);
	data->p_env[i].sus = read8(f);
	data->p_env[i].loop = read8(f);
    }
}

static void get_chunk_fe(struct module_data *m, int size, FILE *f, void *parm)
{
    struct local_data *data = (struct local_data *)parm;
    int i;

    if ((data->f_envnum = read8(f)) == 0)
	return;

    _D(_D_INFO "Pitch envelopes: %d", data->f_envnum);

    data->f_env = calloc(data->f_envnum, sizeof (struct mdl_envelope));

    for (i = 0; i < data->f_envnum; i++) {
	data->f_env[i].num = read8(f);
	fread(data->f_env[i].data, 1, 30, f);
	data->f_env[i].sus = read8(f);
	data->f_env[i].loop = read8(f);
    }
}


static int mdl_load(struct module_data *m, FILE *f, const int start)
{
    struct xmp_module *mod = &m->mod;
    iff_handle handle;
    int i, j, k, l;
    char buf[8];
    struct local_data data;

    LOAD_INIT();

    /* Check magic and get version */
    read32b(f);
    fread(buf, 1, 1, f);

    handle = iff_new();
    if (handle == NULL)
	return -1;

    /* IFFoid chunk IDs */
    iff_register(handle, "IN", get_chunk_in);	/* Module info */
    iff_register(handle, "TR", get_chunk_tr);	/* Tracks */
    iff_register(handle, "SA", get_chunk_sa);	/* Sampled data */
    iff_register(handle, "VE", get_chunk_ve);	/* Volume envelopes */
    iff_register(handle, "PE", get_chunk_pe);	/* Pan envelopes */
    iff_register(handle, "FE", get_chunk_fe);	/* Pitch envelopes */

    if (MSN(*buf)) {
	iff_register(handle, "II", get_chunk_ii);	/* Instruments */
	iff_register(handle, "PA", get_chunk_pa);	/* Patterns */
	iff_register(handle, "IS", get_chunk_is);	/* Sample info */
    } else {
	iff_register(handle, "PA", get_chunk_p0);	/* Old 0.0 patterns */
	iff_register(handle, "IS", get_chunk_i0);	/* Old 0.0 Sample info */
    }

    /* MDL uses a degenerated IFF-style file format with 16 bit IDs and
     * little endian 32 bit chunk size. There's only one chunk per data
     * type i.e. one huge chunk for all the sampled instruments.
     */
    iff_id_size(handle, 2);
    iff_set_quirk(handle, IFF_LITTLE_ENDIAN);

    set_type(m, "Digitrakker MDL %d.%d", MSN(*buf), LSN(*buf));

    m->volbase = 0xff;
    m->c4rate = C4_NTSC_RATE;

    data.v_envnum = data.p_envnum = data.f_envnum = 0;
    data.s_index = calloc(256, sizeof (int));
    data.i_index = calloc(256, sizeof (int));
    data.v_index = malloc(256 * sizeof (int));
    data.p_index = malloc(256 * sizeof (int));
    data.f_index = malloc(256 * sizeof (int));
    data.c2spd = calloc(256, sizeof (int));

    for (i = 0; i < 256; i++) {
	data.v_index[i] = data.p_index[i] = data.f_index[i] = -1;
    }

    /* Load IFFoid chunks */
    while (!feof(f)) {
	iff_chunk(handle, m, f, &data);
    }

    iff_release(handle);

    /* Re-index instruments & samples */

    for (i = 0; i < mod->pat; i++)
	for (j = 0; j < mod->xxp[i]->rows; j++)
	    for (k = 0; k < mod->chn; k++)
		for (l = 0; l < mod->ins; l++) {
		    if (j >= mod->xxt[mod->xxp[i]->index[k]]->rows)
			continue;
		    
		    if (EVENT(i, k, j).ins && EVENT(i, k, j).ins == data.i_index[l]) {
		    	EVENT(i, k, j).ins = l + 1;
			break;
		    }
		}

    for (i = 0; i < mod->ins; i++) {

	/* FIXME: envelope timing is wrong */

	/* volume envelopes */
	if (data.v_index[i] >= 0) {
	    mod->xxi[i].aei.flg = XMP_ENVELOPE_ON;
	    mod->xxi[i].aei.npt = 16;

	    for (j = 0; j < data.v_envnum; j++) {
		if (data.v_index[i] == j) {
		    mod->xxi[i].aei.flg |= data.v_env[j].sus & 0x10 ? XMP_ENVELOPE_SUS : 0;
		    mod->xxi[i].aei.flg |= data.v_env[j].sus & 0x20 ? XMP_ENVELOPE_LOOP : 0;
		    mod->xxi[i].aei.sus = data.v_env[j].sus & 0x0f;
		    mod->xxi[i].aei.lps = data.v_env[j].loop & 0x0f;
		    mod->xxi[i].aei.lpe = data.v_env[j].loop & 0xf0;
		    mod->xxi[i].aei.data[0] = 0;
		    for (k = 1; k < mod->xxi[i].aei.npt; k++) {
			mod->xxi[i].aei.data[k * 2] = mod->xxi[i].aei.data[(k - 1) * 2] +
						data.v_env[j].data[(k - 1) * 2];

			if (data.v_env[j].data[k * 2] == 0)
			    break;

			mod->xxi[i].aei.data[k * 2 + 1] = data.v_env[j].data[(k - 1) * 2 + 1];
		    }
		    mod->xxi[i].aei.npt = k;
		    break;
		}
	    }
	}

	/* pan envelopes */
	if (data.p_index[i] >= 0) {
	    mod->xxi[i].pei.flg = XMP_ENVELOPE_ON;
	    mod->xxi[i].pei.npt = 16;

	    for (j = 0; j < data.p_envnum; j++) {
		if (data.p_index[i] == j) {
		    mod->xxi[i].pei.flg |= data.p_env[j].sus & 0x10 ? XMP_ENVELOPE_SUS : 0;
		    mod->xxi[i].pei.flg |= data.p_env[j].sus & 0x20 ? XMP_ENVELOPE_LOOP : 0;
		    mod->xxi[i].pei.sus = data.p_env[j].sus & 0x0f;
		    mod->xxi[i].pei.lps = data.p_env[j].loop & 0x0f;
		    mod->xxi[i].pei.lpe = data.p_env[j].loop & 0xf0;
		    mod->xxi[i].pei.data[0] = 0;

		    for (k = 1; k < mod->xxi[i].pei.npt; k++) {
			mod->xxi[i].pei.data[k * 2] = mod->xxi[i].pei.data[(k - 1) * 2] +
						data.p_env[j].data[(k - 1) * 2];
			if (data.p_env[j].data[k * 2] == 0)
			    break;
			mod->xxi[i].pei.data[k * 2 + 1] = data.p_env[j].data[(k - 1) * 2 + 1];
		    }
		    mod->xxi[i].pei.npt = k;
		    break;
		}
	    }
	}

	/* pitch envelopes */
	if (data.f_index[i] >= 0) {
	    mod->xxi[i].fei.flg = XMP_ENVELOPE_ON;
	    mod->xxi[i].fei.npt = 16;

	    for (j = 0; j < data.f_envnum; j++) {
		if (data.f_index[i] == j) {
		    mod->xxi[i].fei.flg |= data.f_env[j].sus & 0x10 ? XMP_ENVELOPE_SUS : 0;
		    mod->xxi[i].fei.flg |= data.f_env[j].sus & 0x20 ? XMP_ENVELOPE_LOOP : 0;
		    mod->xxi[i].fei.sus = data.f_env[j].sus & 0x0f;
		    mod->xxi[i].fei.lps = data.f_env[j].loop & 0x0f;
		    mod->xxi[i].fei.lpe = data.f_env[j].loop & 0xf0;
		    mod->xxi[i].fei.data[0] = 0;
		    mod->xxi[i].fei.data[1] = 32;

		    for (k = 1; k < mod->xxi[i].fei.npt; k++) {
			mod->xxi[i].fei.data[k * 2] = mod->xxi[i].fei.data[(k - 1) * 2] +
						data.f_env[j].data[(k - 1) * 2];
			if (data.f_env[j].data[k * 2] == 0)
			    break;
			mod->xxi[i].fei.data[k * 2 + 1] = data.f_env[j].data[(k - 1) * 2 + 1] * 4;
		    }

		    mod->xxi[i].fei.npt = k;
		    break;
		}
	    }
	}

	for (j = 0; j < mod->xxi[i].nsm; j++)
	    for (k = 0; k < mod->smp; k++)
		if (mod->xxi[i].sub[j].sid == data.s_index[k]) {
		    mod->xxi[i].sub[j].sid = k;
		    c2spd_to_note (data.c2spd[k], &mod->xxi[i].sub[j].xpo, &mod->xxi[i].sub[j].fin);
		    break;
		}
    }

    free(data.c2spd);
    free(data.f_index);
    free(data.p_index);
    free(data.v_index);
    free(data.i_index);
    free(data.s_index);

    if (data.v_envnum)
	free(data.v_env);
    if (data.p_envnum)
	free(data.p_env);
    if (data.f_envnum)
	free(data.f_env);

    m->quirk |= QUIRK_FINEFX;

    m->quirk |= QUIRKS_FT2;
    m->read_event_type = READ_EVENT_FT2;

    return 0;
}
