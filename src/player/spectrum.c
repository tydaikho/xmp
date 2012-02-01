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

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "xmp.h"
#include "common.h"
#include "effects.h"
#include "driver.h"
#include "synth.h"
#include "spectrum.h"
#include "ym2149.h"


struct spectrum_channel {
	int vol;
	int freq;
	int count;
	int orn;
	struct spectrum_sample patch;
};

struct spectrum {
	struct spectrum_channel sc[3];
	int noise_offset;
	int envtype;
	int env;
	struct ym2149 *ym;
};


static struct spectrum *spectrum_new(int freq)
{
	struct spectrum *sp;

	sp = calloc(1, sizeof (struct spectrum));
	if (sp == NULL)
		goto err1;

	sp->ym = ym2149_new(SPECTRUM_CLOCK, 1, freq);
	if (sp->ym == NULL)
		goto err2;

	return sp;

err2:
	free(sp);
err1:
	return NULL;
}

static void spectrum_destroy(struct spectrum *sp)
{
	ym2149_reset(sp->ym);
	ym2149_destroy(sp->ym);
	free(sp);
}

/*
 * Tone Generator Control
 * (Registers R0, R1, R2, R3, R4, R5)
 * 
 * The frequency of each square wave generated by the three Tone
 * Generators (one each for Channels A, B, and C) is obtained in the
 * PSG by first counting down the input clock by 16, then by further
 * counting down the result by the programmed 12-bit Tone Period
 * value. Each 12-bit value is obtained in the PSG by combining the
 * contents of the relative Coarse and Fine Tune registers, as illustrated
 * in the following:
 * 
 *    Coarse Tune Registers     Channel            Fine Tune Register
 *           R1                    A                      R0
 *           R3                    B                      R2
 *           R5                    C                      R4
 * 
 *    B7 B6 B5 B4 B3 B2 B1 B0                      B7 B6 B5 B4 B3 B2 B1 B0
 *    \_________/ |           \                   /                      /
 *        \/      |            \                 /                      /
 *     NOT USED   |             | ______________/                      /
 *               /              |/                              ______/
 *             TP11 TP10 TP9 TP8 TP7 TP6 TP5 TP4 TP3 TP2 TP1 TP0
 *                 12-bit Tone Period (TP) to Tone Generator
 *
 * Noise Generator Control
 * (Register R6)
 * 
 * The frequency of the noise source is obtained in the PSG by first
 * counting down the input clock by 16, then by further counting down
 * the result by the programmed 5-bit Noise Period value. This 5-bit
 * value consists of the lower 5-bits (B4-B0) of register R6, as
 * illustrated in the following:
 * 
 *                Noise Period Register R6
 * 
 *                B7 B6 B5 B4 B3 B2 B1 B0
 *                \______/ \___________/
 *                   \/         \/
 *                NOT USED   5-bit Noise Period (NP)
 *                           to Noise Generator
 * 
 * Mixer Control-I/O Enable
 * (Register R7)
 *                                   ______
 * Register R7 is a multi functional Enable register which controls the
 * three Noise/Tone Mixers and the general purpose I/O Port.
 * 
 * The Mixers, as previously described, combine the noise and tone
 * frequencies for each of the three channels. The determination of
 * combining neither/either/both noise and tone frequencies on each
 * channel is made by the state of bits B5-B0 or R7.
 * 
 * The direction (input or output) of the general purpose I/O Port
 * (IOA) is determined by the state of bit B6 or R7.
 * 
 * These functions are illustrated in the following:
 * 
 *                Mixer Control-U/O Enable Register R7
 * 
 *                B7 B6 B5 B4 B3 B2 B1 B0
 *    NOT USED____/  |  \______/ \______/
 *                   |   __\/       \/______
 *     _____________/   |___________        |__________
 *     Input Enable     Noise Enable        Tone Enable  <-- Function
 *     I/O Port A        C   B   A           C   B   A   <-- Channel
 *
 * Amplitude Control
 * (Registers R8, R9, R10)
 * 
 * The amplitudes of the signals generated by each of the three D/A
 * Converters (one each for Channels A, B, and C) is determined by the
 * contents of the lower 5 bits (B4--B0) of registers R8, R9, and R10 as
 * illustrated in the following:
 * 
 *        Amplitude Control Register    Channel
 *                  R8                    A
 *                  R9                    B
 *                  R10                   C
 * 
 *                B7 B6 B5 B4 B3 B2 B1 B0
 *                \______/  | \________/
 *                   \/     |     \/
 *                NOT USED  |     L3 L2 L1 L0
 *                          |     4-bit "fixed" amplitude Level.
 *                          |
 *                          |
 *                    amplitude "Mode"
 * 
 */

/*
 * From http://chipmusic.org/forums/topic/27/vortex-tracker-ii/
 *
 * 1F|tne +000_ +00(00)_ F_ ***************
 * 11 234 56667 899 AA B CD EEEEEEEEEEEEEEE
 *
 * 1 - Line number. Cannot be edited.
 * 2 - If set to "T", a tone plays. If set to "t", it doesn't.
 * 3 - If set to "N", white noise plays. If set to "n", it doesn't.
 * 4 - If set to "E", envelope sound plays. If set to "e", it doesn't.
 * 5 - Direction of pitch change.
 * 6 - Amount of pitch change.
 * 7 - If set to ^, pitch change adds up over time. If set to _, pitch
 *     change is absolute.
 * 8 - Direction of pitch change for noise and envelope sound.
 * 9 - Amount of pitch change for noise and envelope sound.
 * A - Displays the absolute value of pitch change for noise and envelope
 *     sound. Cannot be edited.
 * B - Like 7, but for noise and envelope sound.
 * C - Volume of sound for this line.
 * D - If set to _, nothing happens. If set to +, volume increases. If
 *     set to -, volume decreases.
 * E - Visual representation of the volume for that line. Cannot be edited.
 */ 

static void spectrum_update(struct spectrum *sp)
{
	int i;
	int mask = 0x7f;
	int noise = 0;

	for (i = 0; i < 3; i++) {
		struct spectrum_channel *sc = &sp->sc[i];
		struct spectrum_stick *st = &sc->patch.stick[sc->count];
		int freq = sc->freq + st->tone_inc;

		/* freq */
		ym2149_write_register(sp->ym, YM_PERL(i), freq & 0xff);
		ym2149_write_register(sp->ym, YM_PERH(i), freq >> 8);

		/* vol */
		ym2149_write_register(sp->ym, YM_VOL(i), st->vol);

		/* ? */
		/*if (st->flags & SPECTRUM_FLAG_ENVELOPE)
			sp->env += st->noise_env_inc;
		else
			noise += st->noise_env_inc;*/

		noise += st->noise_env_inc;

		/* mixer */
		if (st->flags & SPECTRUM_FLAG_MIXTONE)
			mask &= ~(0x01 << i);

		if (st->flags & SPECTRUM_FLAG_MIXNOISE)
			mask &= ~(0x08 << i);

		/* prepare next tick */
		sc->count++;
		if (sc->count >= sc->patch.length)
			sc->count = sc->patch.loop;
	}

	/* envelope */
	ym2149_write_register(sp->ym, YM_ENVL, sp->env & 0xff);
	ym2149_write_register(sp->ym, YM_ENVH, (sp->env & 0xff00) >> 8);
	ym2149_write_register(sp->ym, YM_ENVTYPE, sp->envtype);

	/* noise */
	ym2149_write_register(sp->ym, YM_NOISE, noise);

	/* mixer */
	ym2149_write_register(sp->ym, YM_MIXER, mask);
}


/*
 * Synth functions
 */

static void synth_setpatch(struct xmp_context *ctx, int c, uint8 *data)
{
	struct spectrum *sp = SYNTH_CHIP(ctx);
	struct spectrum_channel *sc = &sp->sc[c];

	memcpy(&sc->patch, data, sizeof (struct spectrum_sample));
	sc->count = 0;
	sc->vol = sc->patch.stick[0].vol;

}

static void synth_setnote(struct xmp_context *ctx, int c, int note, int bend)
{
	struct xmp_mod_context *m = &ctx->m;
	struct spectrum *sp = SYNTH_CHIP(ctx);
	struct spectrum_extra *se = m->extra;
	struct spectrum_channel *sc = &sp->sc[c];
	double d;

	note += se->ornament[sc->orn].val[sc->count];
	d = (double)note + (double)bend / 100;
	sp->sc[c].freq = (int)(0xfff / pow(2, d / 12));
}

static void synth_setvol(struct xmp_context *ctx, int c, int vol)
{
}

static void synth_seteffect(struct xmp_context *ctx, int c, int type, int val)
{
	struct spectrum *sp = SYNTH_CHIP(ctx);
	struct spectrum_channel *sc = &sp->sc[c];

	switch (type) {
	case FX_SYNTH_0:
		if (val < SPECTRUM_NUM_ORNAMENTS)
			sc->orn = val;
		sp->envtype = 15;
		sp->env = 0x0000;
		break;
	case FX_SYNTH_1:
		/* set R12 */
		sp->env = (sp->env & 0x00ff) | (val << 8);
		break;
	case FX_SYNTH_2:
		sp->envtype = 15;
		sp->env = 0x0000;
		sc->orn = 0;
		break;
	case FX_SYNTH_3:
	case FX_SYNTH_4:
	case FX_SYNTH_5:
	case FX_SYNTH_6:
	case FX_SYNTH_7:
	case FX_SYNTH_8:
	case FX_SYNTH_9:
	case FX_SYNTH_A:
	case FX_SYNTH_B:
	case FX_SYNTH_C:
	case FX_SYNTH_D:
	case FX_SYNTH_E:
		/* set R13 */
		sp->envtype = type - FX_SYNTH_0;
		/* set R11 */
		sp->env = (sp->env & 0xff00) | val;
		sc->orn = 0;
		break;
	}
}

static int synth_init(struct xmp_context *ctx, int freq)
{
	SYNTH_CHIP(ctx) = spectrum_new(freq);
	if (SYNTH_CHIP(ctx) == NULL)
		return -1;

	return 0;
}

static int synth_reset(struct xmp_context *ctx)
{
	struct spectrum *sp = SYNTH_CHIP(ctx);
	ym2149_reset(sp->ym);

	return 0;
}

static int synth_deinit(struct xmp_context *ctx)
{
	struct spectrum *sp = SYNTH_CHIP(ctx);
	spectrum_destroy(sp);

	return 0;
}

static void synth_mixer(struct xmp_context *ctx, int *tmp_bk, int count, int vl, int vr, int stereo)
{
	struct spectrum *sp = SYNTH_CHIP(ctx);

	if (!tmp_bk)
		return;

	spectrum_update(sp);
	ym2149_update(sp->ym, tmp_bk, count, vl, vr, stereo);
}


struct xmp_synth_info synth_spectrum = {
	synth_init,
	synth_deinit,
	synth_reset,
	synth_setpatch,
	synth_setnote,
	synth_setvol,
	synth_seteffect,
	synth_mixer
};

