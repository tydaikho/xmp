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
#ifdef __native_client__
#include <sys/syslimits.h>
#else
#include <limits.h>
#endif
#include "load.h"
#include "mod.h"
#include "period.h"
#include "prowizard/prowiz.h"

extern struct list_head *checked_format;

static int pw_test(FILE *, char *, const int);
static int pw_load(struct xmp_context *, FILE *, const int);

struct xmp_loader_info pw_loader = {
	"pw",
	"prowizard",
	pw_test,
	pw_load
};

#define BUF_SIZE 0x10000

static int pw_test(FILE *f, char *t, const int start)
{
	unsigned char *b;
	int extra;
	int s = BUF_SIZE;

	b = calloc(1, BUF_SIZE);
	fread(b, s, 1, f);

	while ((extra = pw_check(b, s)) > 0) {
		unsigned char *buf = realloc(b, s + extra);
		if (buf == NULL) {
			free(b);
			return -1;
		}
		b = buf;
		fread(b + s, extra, 1, f);
		s += extra;
	}

	free(b);

	if (extra == 0) {
		struct pw_format *format;
		format = list_entry(checked_format, struct pw_format, list);
		if (format->enable)
			return 0;
	}

	return -1;
}

static int pw_load(struct xmp_context *ctx, FILE *f, const int start)
{
	struct xmp_mod_context *m = &ctx->m;
	struct xmp_options *o = &ctx->o;
	struct xxm_event *event;
	struct mod_header mh;
	uint8 mod_event[4];
	struct pw_format *fmt;
	char tmp[PATH_MAX];
	int i, j;
	int fd;

	/* Prowizard depacking */

	if (get_temp_dir(tmp, PATH_MAX) < 0)
		return -1;

	strncat(tmp, "xmp_XXXXXX", PATH_MAX);

	if ((fd = mkstemp(tmp)) < 0)
		return -1;

	if (pw_wizardry(fileno(f), fd, &fmt) < 0) {
		close(fd);
		unlink(tmp);
		return -1;
	}

	if ((f = fdopen(fd, "w+b")) == NULL) {
		close(fd);
		unlink(tmp);
		return -1;
	}


	/* Module loading */

	LOAD_INIT();

	fread(&mh.name, 20, 1, f);
	for (i = 0; i < 31; i++) {
		fread(&mh.ins[i].name, 22, 1, f);
		mh.ins[i].size = read16b(f);
		mh.ins[i].finetune = read8(f);
		mh.ins[i].volume = read8(f);
		mh.ins[i].loop_start = read16b(f);
		mh.ins[i].loop_size = read16b(f);
	}
	mh.len = read8(f);
	mh.restart = read8(f);
	fread(&mh.order, 128, 1, f);
	fread(&mh.magic, 4, 1, f);

	if (memcmp(mh.magic, "M.K.", 4))
		goto err;
		
	m->mod.xxh->ins = 31;
	m->mod.xxh->smp = m->mod.xxh->ins;
	m->mod.xxh->chn = 4;
	m->mod.xxh->len = mh.len;
	m->mod.xxh->rst = mh.restart;
	memcpy(m->mod.xxo, mh.order, 128);

	for (i = 0; i < 128; i++) {
		if (m->mod.xxh->chn > 4)
			m->mod.xxo[i] >>= 1;
		if (m->mod.xxo[i] > m->mod.xxh->pat)
			m->mod.xxh->pat = m->mod.xxo[i];
	}

	m->mod.xxh->pat++;

	m->mod.xxh->trk = m->mod.xxh->chn * m->mod.xxh->pat;

	snprintf(m->mod.name, XMP_NAMESIZE, "%s", (char *)mh.name);
	snprintf(m->mod.type, XMP_NAMESIZE, "%s (%s)", fmt->id, fmt->name);
	MODULE_INFO();

	INSTRUMENT_INIT();

	for (i = 0; i < m->mod.xxh->ins; i++) {
		m->mod.xxi[i].sub = calloc(sizeof (struct xxm_subinstrument), 1);
		m->mod.xxs[i].len = 2 * mh.ins[i].size;
		m->mod.xxs[i].lps = 2 * mh.ins[i].loop_start;
		m->mod.xxs[i].lpe = m->mod.xxs[i].lps + 2 * mh.ins[i].loop_size;
		m->mod.xxs[i].flg = mh.ins[i].loop_size > 1 ? XMP_SAMPLE_LOOP : 0;
		m->mod.xxi[i].sub[0].fin = (int8) (mh.ins[i].finetune << 4);
		m->mod.xxi[i].sub[0].vol = mh.ins[i].volume;
		m->mod.xxi[i].sub[0].pan = 0x80;
		m->mod.xxi[i].sub[0].sid = i;
		m->mod.xxi[i].nsm = !!(m->mod.xxs[i].len);
		m->mod.xxi[i].rls = 0xfff;

		if (m->mod.xxs[i].flg & XMP_SAMPLE_LOOP) {
			if (m->mod.xxs[i].lps == 0 && m->mod.xxs[i].len > m->mod.xxs[i].lpe)
				m->mod.xxs[i].flg |= XMP_SAMPLE_LOOP_FULL;
		}

		copy_adjust(m->mod.xxi[i].name, mh.ins[i].name, 22);

		_D(_D_INFO "[%2X] %-22.22s %04x %04x %04x %c V%02x %+d %c",
			     i, m->mod.xxi[i].name, m->mod.xxs[i].len,
			     m->mod.xxs[i].lps, m->mod.xxs[i].lpe,
			     mh.ins[i].loop_size > 1 ? 'L' : ' ',
			     m->mod.xxi[i].sub[0].vol, m->mod.xxi[i].sub[0].fin >> 4,
			     m->mod.xxs[i].flg & XMP_SAMPLE_LOOP_FULL ? '!' : ' ');
	}

	PATTERN_INIT();

	/* Load and convert patterns */
	_D(_D_INFO "Stored patterns: %d", m->mod.xxh->pat);

	for (i = 0; i < m->mod.xxh->pat; i++) {
		PATTERN_ALLOC(i);
		m->mod.xxp[i]->rows = 64;
		TRACK_ALLOC(i);
		for (j = 0; j < (64 * 4); j++) {
			event = &EVENT(i, j % 4, j / 4);
			fread(mod_event, 1, 4, f);
			cvt_pt_event(event, mod_event);
		}
	}

	m->mod.xxh->flg |= XXM_FLG_MODRNG;

	if (o->skipsmp)
		goto end;

	/* Load samples */

	_D(_D_INFO "Stored samples: %d", m->mod.xxh->smp);
	for (i = 0; i < m->mod.xxh->smp; i++) {
		xmp_drv_loadpatch(ctx, f, m->mod.xxi[i].sub[0].sid, 0,
				  &m->mod.xxs[m->mod.xxi[i].sub[0].sid], NULL);
	}

end:
	fclose(f);
	unlink(tmp);
	return 0;

err:
	fclose(f);
	unlink(tmp);
	return -1;
}
