/* A2 Racer II for PS5 - the screen: one EGL OpenGL Compatibility context for the whole run (decision log D3).
 *
 * The game opens its windows and GL contexts through SDL (the menu once, the race once per race). SDL's video in a
 * title has no GL (the offscreen driver), so the GL half of SDL is replaced here at link time (--wrap, see
 * CMakeLists.txt): SDL_GL_CreateContext hands out this context, SDL_GL_SwapWindow swaps it, SDL_GL_GetDrawableSize
 * reports the surface. Windows themselves stay SDL's, without SDL_WINDOW_OPENGL. */
#include "a2_ps5.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <SDL2/SDL.h>
#include <stdio.h>

static EGLDisplay g_display = EGL_NO_DISPLAY;
static EGLSurface g_surface = EGL_NO_SURFACE;
static EGLContext g_context = EGL_NO_CONTEXT;
static int g_width, g_height;

int a2ps5_gl_open(void)
{
	if (g_context != EGL_NO_CONTEXT)
		return 0;
	g_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	EGLint major = 0, minor = 0;
	if (g_display == EGL_NO_DISPLAY || !eglInitialize(g_display, &major, &minor) || !eglBindAPI(EGL_OPENGL_API)) {
		fprintf(stderr, "a2: egl: no display (0x%x)\n", eglGetError());
		return -1;
	}
	/* The game asks for depth 24 and double buffering (src/main.c:1035-1036). */
	const EGLint attributes[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24,
		EGL_NONE,
	};
	EGLConfig config = NULL;
	EGLint count = 0;
	if (!eglChooseConfig(g_display, attributes, &config, 1, &count) || count < 1) {
		fprintf(stderr, "a2: egl: no config (0x%x)\n", eglGetError());
		return -1;
	}
	g_surface = eglCreateWindowSurface(g_display, config, 0, NULL);
	/* No version: the highest Compatibility profile the driver has (4.6 in the Phase 0 check, D2). */
	const EGLint context_attributes[] = {
		EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT_KHR, EGL_NONE,
	};
	g_context = eglCreateContext(g_display, config, EGL_NO_CONTEXT, context_attributes);
	if (g_surface == EGL_NO_SURFACE || g_context == EGL_NO_CONTEXT ||
	    !eglMakeCurrent(g_display, g_surface, g_surface, g_context)) {
		fprintf(stderr, "a2: egl: no compatibility context (0x%x)\n", eglGetError());
		g_context = EGL_NO_CONTEXT;
		return -1;
	}
	eglQuerySurface(g_display, g_surface, EGL_WIDTH, &g_width);
	eglQuerySurface(g_display, g_surface, EGL_HEIGHT, &g_height);
	eglSwapInterval(g_display, 1);
	fprintf(stderr, "a2: gl: %s on %s, %dx%d\n", (const char *)glGetString(GL_VERSION),
	        (const char *)glGetString(GL_RENDERER), g_width, g_height);
	return 0;
}

void a2ps5_gl_size(int *w, int *h)
{
	*w = g_width;
	*h = g_height;
}

void a2ps5_gl_swap(void)
{
	eglSwapBuffers(g_display, g_surface);
}

/* The upstream name for "a new context starts from the defaults": the web build's one canvas and this one surface
 * both carry the menu's and each race's state into the next (src/main.c, src/menu/a2menu.c call it). */
void a2web_gl_reset(void)
{
	glUseProgram(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glDisableClientState(GL_NORMAL_ARRAY);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_FOG);
	glDisable(GL_LIGHTING);
	glDisable(GL_POLYGON_OFFSET_FILL);
	glDepthMask(GL_TRUE);
	glFrontFace(GL_CCW);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	glMatrixMode(GL_TEXTURE);
	glLoadIdentity();
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glViewport(0, 0, g_width, g_height);
	while (glGetError() != GL_NO_ERROR) {
	}
}

/* ---- SDL's GL functions, replaced at link time (-Wl,--wrap=...) ---- */

SDL_Window *__real_SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags);
SDL_Window *__wrap_SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags)
{
	/* The offscreen driver has no GL; the picture goes through the EGL surface instead. */
	return __real_SDL_CreateWindow(title, x, y, w, h, flags & ~(Uint32)SDL_WINDOW_OPENGL);
}

SDL_GLContext __wrap_SDL_GL_CreateContext(SDL_Window *window)
{
	(void)window;
	if (a2ps5_gl_open() != 0) {
		SDL_SetError("no EGL compatibility context");
		return NULL;
	}
	return (SDL_GLContext)g_context;
}

void __wrap_SDL_GL_DeleteContext(SDL_GLContext context)
{
	(void)context; /* one context for the whole run */
}

int __wrap_SDL_GL_MakeCurrent(SDL_Window *window, SDL_GLContext context)
{
	(void)window;
	(void)context;
	return 0;
}

void __wrap_SDL_GL_SwapWindow(SDL_Window *window)
{
	(void)window;
	a2ps5_gl_swap();
}

void __wrap_SDL_GL_GetDrawableSize(SDL_Window *window, int *w, int *h)
{
	(void)window;
	if (w)
		*w = g_width;
	if (h)
		*h = g_height;
}

int __wrap_SDL_GL_SetAttribute(SDL_GLattr attr, int value)
{
	(void)attr;
	(void)value;
	return 0;
}

int __wrap_SDL_GL_SetSwapInterval(int interval)
{
	return eglSwapInterval(g_display, interval) ? 0 : -1;
}
