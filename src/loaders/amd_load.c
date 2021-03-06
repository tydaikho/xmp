/* Extended Module Player
 * Copyright (C) 1996-2012 Claudio Matsuoka and Hipolito Carraro Jr
 *
 * This file is part of the Extended Module Player and is distributed
 * under the terms of the GNU Lesser General Public License. See COPYING.LIB
 * for more information.
 */

#include "loader.h"
#include "synth.h"


static int amd_test (FILE *, char *, const int);
static int amd_load (struct module_data *, FILE *, const int);

const struct format_loader amd_loader = {
    "Amusic Adlib Tracker (AMD)",
    amd_test,
    amd_load
};

static int amd_test(FILE *f, char *t, const int start)
{
    char buf[9];

    fseek(f, start + 1062, SEEK_SET);
    if (fread(buf, 1, 9, f) < 9)
	return -1;

    if (memcmp(buf, "<o", 2) || memcmp(buf + 6, "RoR", 3))
	return -1;

    fseek(f, start + 0, SEEK_SET);
    read_title(f, t, 24);

    return 0;
}



struct amd_instrument {
    uint8 name[23];		/* Instrument name */
    uint8 reg[11];		/* Adlib registers */
};


struct amd_file_header {
    uint8 name[24];		/* ASCIIZ song name */
    uint8 author[24];		/* ASCIIZ author name */
    struct amd_instrument ins[26];	/* Instruments */
    uint8 len;			/* Song length */
    uint8 pat;			/* Index of last pattern */
    uint8 order[128];		/* Orders */
    uint8 magic[9];		/* 3c 6f ef 51 55 ee 52 6f 52 */
    uint8 version;		/* 0x10=normal module, 0x11=packed */
};



static int amd_load(struct module_data *m, FILE *f, const int start)
{
    struct xmp_module *mod = &m->mod;
    int r, i, j, tmode = 1;
    struct amd_file_header afh;
    struct xmp_event *event;
    char regs[11];
    const int reg_xlat[] = { 0, 5, 1, 6, 2, 7, 3, 8, 4, 9, 10 };
    uint16 w;
    uint8 b;

    LOAD_INIT();

    fread(&afh.name, 24, 1, f);
    fread(&afh.author, 24, 1, f);
    for (i = 0; i < 26; i++) {
	fread(&afh.ins[i].name, 23, 1, f);
	fread(&afh.ins[i].reg, 11, 1, f);
    }
    afh.len = read8(f);
    afh.pat = read8(f);
    fread(&afh.order, 128, 1, f);
    fread(&afh.magic, 9, 1, f);
    afh.version = read8(f);

    mod->chn = 9;
    mod->bpm = 125;
    mod->spd = 6;
    mod->len = afh.len;
    mod->pat = afh.pat + 1;
    mod->ins = 26;
    mod->smp = 0;
    memcpy (mod->xxo, afh.order, mod->len);

    set_type(m, "Amusic");
    strncpy(mod->name, (char *)afh.name, 24);

    MODULE_INFO();
    D_(D_INFO "Instruments: %d", mod->ins);

    INSTRUMENT_INIT();

    /* Load instruments */
    for (i = 0; i < mod->ins; i++) {
	mod->xxi[i].sub = calloc(sizeof (struct xmp_subinstrument), 1);

	copy_adjust(mod->xxi[i].name, afh.ins[i].name, 23);

	mod->xxi[i].nsm = 1;
	mod->xxi[i].sub[0].vol = 0x40;
	mod->xxi[i].sub[0].pan = 0x80;
	mod->xxi[i].sub[0].sid = i;
	mod->xxi[i].sub[0].xpo = -1;

	for (j = 0; j < 11; j++)
	    regs[j] = afh.ins[i].reg[reg_xlat[j]];

	D_(D_INFO "\n[%2X] %-23.23s", i, mod->xxi[i].name);

	load_sample(f, SAMPLE_FLAG_ADLIB, NULL, regs);
    }

    if (!afh.version) {
	D_(D_CRIT "error: Unpacked module not supported");
	return -1;
    }

    D_(D_INFO "Stored patterns: %d", mod->pat);

    mod->xxp = calloc (sizeof (struct xmp_pattern *), mod->pat + 1);

    for (i = 0; i < mod->pat; i++) {
	PATTERN_ALLOC (i);
	for (j = 0; j < 9; j++) {
	    w = read16l(f);
	    mod->xxp[i]->index[j] = w;
	    if (w > mod->trk)
		mod->trk = w;
	}
	mod->xxp[i]->rows = 64;
    }
    mod->trk++;

    w = read16l(f);

    D_(D_INFO "Stored tracks: %d", w);

    mod->xxt = calloc (sizeof (struct xmp_track *), mod->trk);
    mod->trk = w;

    for (i = 0; i < mod->trk; i++) {
	w = read16l(f);
	mod->xxt[w] = calloc (sizeof (struct xmp_track) +
	    sizeof (struct xmp_event) * 64, 1);
	mod->xxt[w]->rows = 64;
	for (r = 0; r < 64; r++) {
	    event = &mod->xxt[w]->event[r];
	    b = read8(f);		/* Effect parameter */
	    if (b & 0x80) {
		r += (b & 0x7f) - 1;
		continue;
	    }
	    event->fxp = b;
	    b = read8(f);		/* Instrument + effect type */
	    event->ins = MSN (b);
	    switch (b = LSN (b)) {
	    case 0:		/* Arpeggio */
		break;
	    case 4:		/* Set volume */
		b = FX_VOLSET;
		break;
	    case 1:		/* Slide up */
	    case 2:		/* Slide down */
	    case 3:		/* Modulator/carrier intensity */
	    case 8:		/* Tone portamento */
	    case 9:		/* Tremolo/vibrato */
		event->fxp = b = 0;
		break;
	    case 5:		/* Pattern jump */
		b = FX_JUMP;
		break;
	    case 6:		/* Pattern break */
		b = FX_BREAK;
		break;
	    case 7:		/* Speed */
		if (!event->fxp)
		    tmode = 3;
		if (event->fxp > 31) {
		    event->fxp = b = 0;
		    break;
		}
		event->fxp *= tmode;
		b = FX_SPEED;
		break;
	    }
	    event->fxt = b;
	    b = read8(f);	/* Note + octave + instrument */
	    event->ins |= (b & 1) << 4;
	    if ((event->note = MSN (b)))
		event->note += (2 + ((b & 0xe) >> 1)) * 12;
	}
    }

    for (i = 0; i < mod->chn; i++) {
	mod->xxc[i].pan = 0x80;
	mod->xxc[i].flg = XMP_CHANNEL_SYNTH;
    }

    m->synth = &synth_adlib;

    return 0;
}
