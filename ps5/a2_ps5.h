/* A2 Racer II for PS5 - the title's own parts: the screen, the disc import, the status screen. */
#ifndef A2_PS5_H
#define A2_PS5_H

#include <stddef.h>

/* The title folder, as the title sees it, and the folders the player and the game use there. */
#define A2PS5_TITLE_DIR "/app0"
#define A2PS5_DUMP_DIR A2PS5_TITLE_DIR "/dump"
#define A2PS5_DATA_DIR A2PS5_TITLE_DIR "/data"

/* a2_ps5_gl.c: the EGL Compatibility context (decision log D3). */
int a2ps5_gl_open(void);
void a2ps5_gl_size(int *w, int *h);
void a2ps5_gl_swap(void);
void a2web_gl_reset(void);

/* a2_ps5_disc.c: the data folder from a disc image in dump/ (decision log D4). Returns 0, or -1 with the reason in
   status. progress gets 0-100. */
typedef void (*a2ps5_progress)(int percent);
int a2ps5_import_disc(const char *dump_dir, const char *out_dir, a2ps5_progress progress, char *status, size_t cap);

/* a2_ps5_screen.c: a plain status screen with a built-in font, for before the game's own files are there. */
void a2ps5_screen_show(const char *title, const char *line1, const char *line2, int percent);

#endif
