#include <stdlib.h>
#include "sound.h"

static int init(struct options *options)
{
	return 0;
}

static void play(void *b, int i)
{
}

static void deinit()
{
}

static void flush()
{
}

static void onpause()
{
}

static void onresume()
{
}


struct sound_driver sound_null = {
	"null",
	"null output",
	NULL,
	init,
	deinit,
	play,
	flush,
	onpause,
	onresume	
};
