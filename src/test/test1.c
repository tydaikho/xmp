/* A simple frontend for xmp */

#include <stdio.h>
#include <stdlib.h>
#include <pulse/simple.h>
#include <pulse/error.h>

#include "xmp.h"

static void display_data(struct xmp_module_info *mi)
{
	printf("%3d/%3d %3d/%3d\r",
		mi->order, mi->mod->len,
		mi->row, mi->mod->xxp[mi->pattern]->rows);

	fflush(stdout);
}

int main(int argc, char **argv)
{
	static xmp_context ctx;
	static struct xmp_module_info mi[2];
	int current, prev;
	int i;
	pa_simple *s;
	pa_sample_spec ss;
	int error;

	ss.format = PA_SAMPLE_S16NE;
	ss.channels = 2;
	ss.rate = 44100;

	s = pa_simple_new(NULL,	/* Use the default server */
		  "test",	/* Our application's name */
		  PA_STREAM_PLAYBACK, NULL,	/* Use the default device */
		  "Music",	/* Description of our stream */
		  &ss,		/* Our sample format */
		  NULL,		/* Use default channel map */
		  NULL,		/* Use default buffering attributes */
		  &error);	/* Ignore error code */

	if (s == 0) {
		fprintf(stderr, "pulseaudio error: %s\n", pa_strerror(error));
		exit(1);
	}

	ctx = xmp_create_context();
	xmp_init(ctx, argc, argv);

	for (i = 1; i < argc; i++) {
		if (xmp_load_module(ctx, argv[i]) < 0) {
			fprintf(stderr, "%s: error loading %s\n", argv[0],
				argv[i]);
			continue;
		}

		if (xmp_player_start(ctx) == 0) {

			/* Show module data */

			xmp_player_get_info(ctx, &mi[0]);
			printf("%s (%s)\n", mi[0].mod->name, mi[0].mod->type);
			mi[0].order = -1;
			mi[0].row = -1;
			current = 0;
	
			/* Play module */

			while (xmp_player_frame(ctx) == 0) {
				prev = current;
				current ^= 1;
	
				xmp_player_get_info(ctx, &mi[current]);
				pa_simple_write(s, mi[current].buffer,
						mi[current].size, &error);
	
				if (mi[current].row != mi[prev].row) {
					display_data(&mi[current]);
				}
			}
			xmp_player_end(ctx);
		}

		xmp_release_module(ctx);
		printf("\n");
	}

	xmp_free_context(ctx);

	return 0;
}
