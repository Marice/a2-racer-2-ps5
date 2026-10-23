/* A2 Racer II for PS5 - the step from a race back to the menu, traced (decision log D6).
 *
 * The first run ended with signal 11 at address 0x20 right after the race saved its result: the race ends with
 * SDL_Quit and the menu brings SDL up again, in one process (on the desktop the race is its own process). Each step
 * of that is logged and flushed here, and eglTerminate is kept from SDL: the whole run has one EGL context
 * (ps5/a2_ps5_gl.c), which a video driver's quit must not end. */
#include <EGL/egl.h>
#include <SDL2/SDL.h>
#include <stdio.h>

static void trace(const char *what)
{
	fprintf(stderr, "a2: trace: %s\n", what);
	fflush(stderr);
}

EGLBoolean __wrap_eglTerminate(EGLDisplay display)
{
	(void)display;
	trace("eglTerminate (from SDL) kept off: the run's one EGL context stays");
	return EGL_TRUE;
}

/* The race ends with SDL_Quit and the menu calls SDL_Init again (upstream's one-program path, made for the web).
   On the PS5 SDL_Init after SDL_Quit ended the title (signal 11 inside SDL_Init, D8), so the race's quit is left
   out: SDL stays up from the menu through every race. The race closes its own audio device before. */
int a2ps5_in_race(void);
void __real_SDL_Quit(void);
void __wrap_SDL_Quit(void)
{
	if (a2ps5_in_race()) {
		trace("SDL_Quit at the end of a race left out: SDL stays up for the menu");
		return;
	}
	/* The menu's own quit, just before the title ends (D10): nothing of SDL's needs ending for that. */
	trace("SDL_Quit left out: the title ends right after");
}

int __real_SDL_Init(Uint32 flags);
int __wrap_SDL_Init(Uint32 flags)
{
	char line[64];
	snprintf(line, sizeof line, "SDL_Init 0x%x", (unsigned)flags);
	trace(line);
	int r = __real_SDL_Init(flags);
	snprintf(line, sizeof line, "SDL_Init done %d", r);
	trace(line);
	return r;
}

int __real_SDL_InitSubSystem(Uint32 flags);
int __wrap_SDL_InitSubSystem(Uint32 flags)
{
	char line[64];
	snprintf(line, sizeof line, "SDL_InitSubSystem 0x%x", (unsigned)flags);
	trace(line);
	return __real_SDL_InitSubSystem(flags);
}

SDL_AudioDeviceID __real_SDL_OpenAudioDevice(const char *device, int iscapture, const SDL_AudioSpec *desired,
                                             SDL_AudioSpec *obtained, int allowed_changes);
SDL_AudioDeviceID __wrap_SDL_OpenAudioDevice(const char *device, int iscapture, const SDL_AudioSpec *desired,
                                             SDL_AudioSpec *obtained, int allowed_changes)
{
	trace("SDL_OpenAudioDevice");
	SDL_AudioDeviceID id = __real_SDL_OpenAudioDevice(device, iscapture, desired, obtained, allowed_changes);
	char line[64];
	snprintf(line, sizeof line, "SDL_OpenAudioDevice -> %u", (unsigned)id);
	trace(line);
	return id;
}

void __real_SDL_CloseAudioDevice(SDL_AudioDeviceID dev);
void __wrap_SDL_CloseAudioDevice(SDL_AudioDeviceID dev)
{
	trace("SDL_CloseAudioDevice");
	__real_SDL_CloseAudioDevice(dev);
	trace("SDL_CloseAudioDevice done");
}

void __real_SDL_DestroyWindow(SDL_Window *window);
void __wrap_SDL_DestroyWindow(SDL_Window *window)
{
	trace("SDL_DestroyWindow");
	__real_SDL_DestroyWindow(window);
}
