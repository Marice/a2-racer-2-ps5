/* A2 Racer II for PS5 - the title's start: the screen, the game's files from the player's disc image when they are
 * not there yet (decision log D4), then the menu, which starts each race as a call (decision log D1). */
#include "a2_ps5.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

int a2menu_main(int argc, char **argv); /* src/menu/a2menu.c, built with A2_PS5 */

static const char kTitle[] = "A2 RACER II";

static int exists(const char *path)
{
	struct stat st;
	return stat(path, &st) == 0;
}

static void on_progress(int percent)
{
	static int shown = -1;
	if (percent == shown)
		return;
	shown = percent;
	a2ps5_screen_show(kTitle, "Copying the game's files from your disc image", "This happens once", percent);
}

/* Shows a message that the player has to act on, until the title is closed (the PS button). */
static int stay_with(const char *line1, const char *line2)
{
	for (;;) {
		a2ps5_screen_show(kTitle, line1, line2, -1);
		struct timespec wait = {0, 250 * 1000 * 1000};
		nanosleep(&wait, NULL);
	}
	return 1;
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	fprintf(stderr, "a2: A2 Racer II for PS5\n");
	/* SDL's video in a title is the offscreen driver: windows and events; the picture goes through EGL. */
	SDL_SetHint(SDL_HINT_VIDEODRIVER, "offscreen");
	if (a2ps5_gl_open() != 0)
		return 1;

	if (!exists(A2PS5_DATA_DIR "/spel.dat")) {
		char status[160];
		a2ps5_screen_show(kTitle, "Looking for your disc image in the dump folder", NULL, -1);
		if (a2ps5_import_disc(A2PS5_DUMP_DIR, A2PS5_DATA_DIR, on_progress, status, sizeof status) != 0)
			return stay_with(status, "Put the .bin (and .cue) of A2 Racer II in the title folder's dump folder");
	}

	char *menu_argv[] = {"a2racer2", A2PS5_DATA_DIR, NULL};
	int status = a2menu_main(2, menu_argv);
	/* The player chose to stop. A normal exit ran the static destructors twice (atexit's _fini and the loader's
	   teardown) and ended in a crash report (D10); the process ends here instead, the log written first. */
	fprintf(stderr, "a2: the menu closed (%d): the title ends\n", status);
	fflush(NULL);
	_exit(status);
}
