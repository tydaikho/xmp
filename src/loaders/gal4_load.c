/* Extended Module Player
 * Copyright (C) 1996-2012 Claudio Matsuoka and Hipolito Carraro Jr
 *
 * This file is part of the Extended Module Player and is distributed
 * under the terms of the GNU General Public License. See doc/COPYING
 * for more information.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <limits.h>
#include "load.h"
#include "iff.h"
#include "period.h"

/* Galaxy Music System 4.0 module file loader
 *
 * Based on modules converted using mod2j2b.exe
 */

static int gal4_test(FILE *, char *, const int);
static int gal4_load(struct xmp_context *, FILE *, const int);

struct xmp_loader_info gal4_loader = {
	"GAL4",
	"Galaxy Music System 4.0",
	gal4_test,
	gal4_load
};

static int gal4_test(FILE *f, char *t, const int start)
{
        if (read32b(f) != MAGIC4('R', 'I', 'F', 'F'))
		return -1;

	read32b(f);

	if (read32b(f) != MAGIC4('A', 'M', 'F', 'F'))
		return -1;

	if (read32b(f) != MAGIC4('M', 'A', 'I', 'N'))
		return -1;

	read_title(f, t, 0);

	return 0;
}

static int snum;

static void get_main(struct xmp_context *ctx, int size, FILE *f)
{
	struct xmp_player_context *p = &ctx->p;
	struct xmp_mod_context *m = &ctx->m;
	char buf[64];
	int flags;
	
	fread(buf, 1, 64, f);
	strncpy(m->mod.name, buf, 64);
	strcpy(m->mod.type, "Galaxy Music System 4.0");

	flags = read8(f);
	if (~flags & 0x01)
		m->mod.xxh->flg = XXM_FLG_LINEAR;
	m->mod.xxh->chn = read8(f);
	m->mod.xxh->tpo = read8(f);
	m->mod.xxh->bpm = read8(f);
	read16l(f);		/* unknown - 0x01c5 */
	read16l(f);		/* unknown - 0xff00 */
	read8(f);		/* unknown - 0x80 */
}

static void get_ordr(struct xmp_context *ctx, int size, FILE *f)
{
	struct xmp_player_context *p = &ctx->p;
	struct xmp_mod_context *m = &ctx->m;
	int i;

	m->mod.xxh->len = read8(f);

	for (i = 0; i < m->mod.xxh->len; i++)
		m->mod.xxo[i] = read8(f);
}

static void get_patt_cnt(struct xmp_context *ctx, int size, FILE *f)
{
	struct xmp_player_context *p = &ctx->p;
	struct xmp_mod_context *m = &ctx->m;
	int i;

	i = read8(f) + 1;		/* pattern number */

	if (i > m->mod.xxh->pat)
		m->mod.xxh->pat = i;
}

static void get_inst_cnt(struct xmp_context *ctx, int size, FILE *f)
{
	struct xmp_player_context *p = &ctx->p;
	struct xmp_mod_context *m = &ctx->m;
	int i;

	read8(f);			/* 00 */
	i = read8(f) + 1;		/* instrument number */
	
	if (i > m->mod.xxh->ins)
		m->mod.xxh->ins = i;

	fseek(f, 28, SEEK_CUR);		/* skip name */

	m->mod.xxh->smp += read8(f);
}

static void get_patt(struct xmp_context *ctx, int size, FILE *f)
{
	struct xmp_player_context *p = &ctx->p;
	struct xmp_mod_context *m = &ctx->m;
	struct xxm_event *event, dummy;
	int i, len, chan;
	int rows, r;
	uint8 flag;
	
	i = read8(f);	/* pattern number */
	len = read32l(f);
	
	rows = read8(f) + 1;

	PATTERN_ALLOC(i);
	m->mod.xxp[i]->rows = rows;
	TRACK_ALLOC(i);

	for (r = 0; r < rows; ) {
		if ((flag = read8(f)) == 0) {
			r++;
			continue;
		}

		chan = flag & 0x1f;

		event = chan < m->mod.xxh->chn ? &EVENT(i, chan, r) : &dummy;

		if (flag & 0x80) {
			uint8 fxp = read8(f);
			uint8 fxt = read8(f);

			switch (fxt) {
			case 0x14:		/* speed */
				fxt = FX_S3M_TEMPO;
				break;
			default:
				if (fxt > 0x0f) {
					printf("unknown effect %02x %02x\n", fxt, fxp);
					fxt = fxp = 0;
				}
			}

			event->fxt = fxt;
			event->fxp = fxp;
		}

		if (flag & 0x40) {
			event->ins = read8(f);
			event->note = read8(f);

			if (event->note == 128) {
				event->note = XMP_KEY_OFF;
			} else if (event->note > 12) {
				event->note -= 12;
			} else {
				event->note = 0;
			}
		}

		if (flag & 0x20) {
			event->vol = 1 + read8(f) / 2;
		}
	}
}

static void get_inst(struct xmp_context *ctx, int size, FILE *f)
{
	struct xmp_player_context *p = &ctx->p;
	struct xmp_mod_context *m = &ctx->m;
	int i, j;
	int srate, finetune, flags;
	int val, vwf, vra, vde, vsw, fade;
	uint8 buf[30];

	read8(f);		/* 00 */
	i = read8(f);		/* instrument number */

	fread(&m->mod.xxi[i].name, 1, 28, f);
	str_adj((char *)m->mod.xxi[i].name);

	m->mod.xxi[i].nsm = read8(f);
	fseek(f, 12, SEEK_CUR);		/* Sample map - 1st octave */

	for (j = 0; j < 96; j++) {
		m->mod.xxi[i].map[j].ins = read8(f);
	}

	fseek(f, 11, SEEK_CUR);		/* unknown */
	vwf = read8(f);			/* vibrato waveform */
	vsw = read8(f);			/* vibrato sweep */
	read8(f);			/* unknown */
	read8(f);			/* unknown */
	vde = read8(f) / 4;		/* vibrato depth */
	vra = read16l(f) / 16;		/* vibrato speed */
	read8(f);			/* unknown */

	val = read8(f);			/* PV envelopes flags */
	if (LSN(val) & 0x01)
		m->mod.xxi[i].aei.flg |= XXM_ENV_ON;
	if (LSN(val) & 0x02)
		m->mod.xxi[i].aei.flg |= XXM_ENV_SUS;
	if (LSN(val) & 0x04)
		m->mod.xxi[i].aei.flg |= XXM_ENV_LOOP;
	if (MSN(val) & 0x01)
		m->mod.xxi[i].pei.flg |= XXM_ENV_ON;
	if (MSN(val) & 0x02)
		m->mod.xxi[i].pei.flg |= XXM_ENV_SUS;
	if (MSN(val) & 0x04)
		m->mod.xxi[i].pei.flg |= XXM_ENV_LOOP;

	val = read8(f);			/* PV envelopes points */
	m->mod.xxi[i].aei.npt = LSN(val) + 1;
	m->mod.xxi[i].pei.npt = MSN(val) + 1;

	val = read8(f);			/* PV envelopes sustain point */
	m->mod.xxi[i].aei.sus = LSN(val);
	m->mod.xxi[i].pei.sus = MSN(val);

	val = read8(f);			/* PV envelopes loop start */
	m->mod.xxi[i].aei.lps = LSN(val);
	m->mod.xxi[i].pei.lps = MSN(val);

	read8(f);			/* PV envelopes loop end */
	m->mod.xxi[i].aei.lpe = LSN(val);
	m->mod.xxi[i].pei.lpe = MSN(val);

	if (m->mod.xxi[i].aei.npt <= 0 || m->mod.xxi[i].aei.npt >= XMP_MAXENV)
		m->mod.xxi[i].aei.flg &= ~XXM_ENV_ON;

	if (m->mod.xxi[i].pei.npt <= 0 || m->mod.xxi[i].pei.npt >= XMP_MAXENV)
		m->mod.xxi[i].pei.flg &= ~XXM_ENV_ON;

	fread(buf, 1, 30, f);		/* volume envelope points */;
	for (j = 0; j < m->mod.xxi[i].aei.npt; j++) {
		m->mod.xxi[i].aei.data[j * 2] = readmem16l(buf + j * 3) / 16;
		m->mod.xxi[i].aei.data[j * 2 + 1] = buf[j * 3 + 2];
	}

	fread(buf, 1, 30, f);		/* pan envelope points */;
	for (j = 0; j < m->mod.xxi[i].pei.npt; j++) {
		m->mod.xxi[i].pei.data[j * 2] = readmem16l(buf + j * 3) / 16;
		m->mod.xxi[i].pei.data[j * 2 + 1] = buf[j * 3 + 2];
	}

	fade = read8(f);		/* fadeout - 0x80->0x02 0x310->0x0c */
	read8(f);			/* unknown */

	_D(_D_INFO "[%2X] %-28.28s  %2d ", i, m->mod.xxi[i].name, m->mod.xxi[i].nsm);

	if (m->mod.xxi[i].nsm == 0)
		return;

	m->mod.xxi[i].sub = calloc(sizeof(struct xxm_subinstrument), m->mod.xxi[i].nsm);

	for (j = 0; j < m->mod.xxi[i].nsm; j++, snum++) {
		read32b(f);	/* SAMP */
		read32b(f);	/* size */
	
		fread(&m->mod.xxs[snum].name, 1, 28, f);
		str_adj((char *)m->mod.xxs[snum].name);
	
		m->mod.xxi[i].sub[j].pan = read8(f) * 4;
		if (m->mod.xxi[i].sub[j].pan == 0)	/* not sure about this */
			m->mod.xxi[i].sub[j].pan = 0x80;
		
		m->mod.xxi[i].sub[j].vol = read8(f);
		flags = read8(f);
		read8(f);	/* unknown - 0x80 */

		m->mod.xxi[i].sub[j].vwf = vwf;
		m->mod.xxi[i].sub[j].vde = vde;
		m->mod.xxi[i].sub[j].vra = vra;
		m->mod.xxi[i].sub[j].vsw = vsw;
		m->mod.xxi[i].sub[j].sid = snum;
	
		m->mod.xxs[snum].len = read32l(f);
		m->mod.xxs[snum].lps = read32l(f);
		m->mod.xxs[snum].lpe = read32l(f);
	
		m->mod.xxs[snum].flg = 0;
		if (flags & 0x04)
			m->mod.xxs[snum].flg |= XMP_SAMPLE_16BIT;
		if (flags & 0x08)
			m->mod.xxs[snum].flg |= XMP_SAMPLE_LOOP;
		if (flags & 0x10)
			m->mod.xxs[snum].flg |= XMP_SAMPLE_LOOP_BIDIR;
		/* if (flags & 0x80)
			m->mod.xxs[snum].flg |= ? */
	
		srate = read32l(f);
		finetune = 0;
		c2spd_to_note(srate, &m->mod.xxi[i].sub[j].xpo, &m->mod.xxi[i].sub[j].fin);
		m->mod.xxi[i].sub[j].fin += finetune;
	
		read32l(f);			/* 0x00000000 */
		read32l(f);			/* unknown */
	
		_D(_D_INFO, "  %X: %05x%c%05x %05x %c V%02x P%02x %5d",
			j, m->mod.xxs[snum].len,
			m->mod.xxs[snum].flg & XMP_SAMPLE_16BIT ? '+' : ' ',
			m->mod.xxs[snum].lps,
			m->mod.xxs[snum].lpe,
			m->mod.xxs[snum].flg & XMP_SAMPLE_LOOP_BIDIR ? 'B' : 
			m->mod.xxs[snum].flg & XMP_SAMPLE_LOOP ? 'L' : ' ',
			m->mod.xxi[i].sub[j].vol,
			m->mod.xxi[i].sub[j].pan,
			srate);
	
		if (m->mod.xxs[snum].len > 1) {
			xmp_drv_loadpatch(ctx, f, snum, 0, &m->mod.xxs[snum], NULL);
		}
	}
}

static int gal4_load(struct xmp_context *ctx, FILE *f, const int start)
{
	struct xmp_player_context *p = &ctx->p;
	struct xmp_mod_context *m = &ctx->m;
	int i, offset;

	LOAD_INIT();

	read32b(f);	/* Skip RIFF */
	read32b(f);	/* Skip size */
	read32b(f);	/* Skip AM   */

	offset = ftell(f);

	m->mod.xxh->smp = m->mod.xxh->ins = 0;

	iff_register("MAIN", get_main);
	iff_register("ORDR", get_ordr);
	iff_register("PATT", get_patt_cnt);
	iff_register("INST", get_inst_cnt);
	iff_setflag(IFF_LITTLE_ENDIAN);
	iff_setflag(IFF_CHUNK_TRUNC4);

	/* Load IFF chunks */
	while (!feof(f))
		iff_chunk(ctx, f);

	iff_release();

	m->mod.xxh->trk = m->mod.xxh->pat * m->mod.xxh->chn;

	MODULE_INFO();
	INSTRUMENT_INIT();
	PATTERN_INIT();

	_D(_D_INFO "Stored patterns: %d\n", m->mod.xxh->pat);
	_D(_D_INFO "Stored samples : %d ", m->mod.xxh->smp);

	fseek(f, start + offset, SEEK_SET);
	snum = 0;

	iff_register("PATT", get_patt);
	iff_register("INST", get_inst);
	iff_setflag(IFF_LITTLE_ENDIAN);
	iff_setflag(IFF_CHUNK_TRUNC4);

	/* Load IFF chunks */
	while (!feof (f))
		iff_chunk(ctx, f);

	iff_release();

	for (i = 0; i < m->mod.xxh->chn; i++)
		m->mod.xxc[i].pan = 0x80;

	return 0;
}
