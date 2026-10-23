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

void __real_SDL_Quit(void);
void __wrap_SDL_Quit(void)
{
	trace("SDL_Quit");
	__real_SDL_Quit();
	trace("SDL_Quit done");
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
