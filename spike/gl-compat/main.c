/* A2 Racer II for PS5 - Phase 0: does the PS5 OpenGL SDK's Compatibility profile run the fixed-function OpenGL
 * the game uses? Each check draws for a few seconds and logs PASS or FAIL, measured with glReadPixels and
 * glGetError, to a2racer2.log in the title folder (decision log D1).
 *
 * The checks follow what the race renderer calls (src/render.c, main.c, sprites.c, sky.c of a2racer2-re):
 * immediate mode, the matrix stack, client-side arrays with 32-bit indices, a non-power-of-two texture with
 * GL_REPEAT and GL_MODULATE, the alpha test, blending, glPushAttrib/glPopAttrib and glPolygonOffset. */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static EGLDisplay g_display;
static EGLSurface g_surface;
static int g_width, g_height;
static int g_failures;

static double now_seconds(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + ts.tv_nsec / 1e9;
}

static void result(const char *check, int pass, const char *detail)
{
	fprintf(stderr, "a2: check %-34s %s %s\n", check, pass ? "PASS" : "FAIL", detail ? detail : "");
	fflush(stderr);
	if (!pass)
		g_failures++;
}

/* Colour of the pixel at (x, y) of the back buffer, as fractions of the window. */
static void read_pixel(float fx, float fy, unsigned char rgba[4])
{
	glReadPixels((int)(fx * g_width), (int)(fy * g_height), 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

static int near(const unsigned char rgba[4], int r, int g, int b)
{
	return abs((int)rgba[0] - r) < 40 && abs((int)rgba[1] - g) < 40 && abs((int)rgba[2] - b) < 40;
}

static void describe(char *out, size_t cap, const unsigned char rgba[4], GLenum error)
{
	snprintf(out, cap, "(pixel %d,%d,%d,%d; glGetError 0x%x)", rgba[0], rgba[1], rgba[2], rgba[3], error);
}

static int create_context(void)
{
	g_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	EGLint major = 0, minor = 0;
	if (g_display == EGL_NO_DISPLAY || !eglInitialize(g_display, &major, &minor)) {
		fprintf(stderr, "a2: egl: no display (0x%x)\n", eglGetError());
		return 0;
	}
	fprintf(stderr, "a2: egl: EGL %d.%d\n", major, minor);
	if (!eglBindAPI(EGL_OPENGL_API)) {
		fprintf(stderr, "a2: egl: eglBindAPI failed (0x%x)\n", eglGetError());
		return 0;
	}
	/* The game asks for depth 24 and double buffering (a2racer2-re src/main.c:1035-1036). */
	const EGLint attributes[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24,
		EGL_NONE,
	};
	EGLConfig config = NULL;
	EGLint count = 0;
	if (!eglChooseConfig(g_display, attributes, &config, 1, &count) || count < 1) {
		fprintf(stderr, "a2: egl: no config (0x%x)\n", eglGetError());
		return 0;
	}
	g_surface = eglCreateWindowSurface(g_display, config, 0, NULL);
	if (g_surface == EGL_NO_SURFACE) {
		fprintf(stderr, "a2: egl: no surface (0x%x)\n", eglGetError());
		return 0;
	}
	/* The Compatibility profile: first without a version (the highest the driver offers), then 3.0 and 2.1. */
	const EGLint versions[][2] = { { 0, 0 }, { 3, 0 }, { 2, 1 } };
	EGLContext context = EGL_NO_CONTEXT;
	for (size_t i = 0; i < sizeof(versions) / sizeof(versions[0]) && context == EGL_NO_CONTEXT; i++) {
		EGLint list[8];
		int n = 0;
		if (versions[i][0]) {
			list[n++] = EGL_CONTEXT_MAJOR_VERSION_KHR;
			list[n++] = versions[i][0];
			list[n++] = EGL_CONTEXT_MINOR_VERSION_KHR;
			list[n++] = versions[i][1];
		}
		list[n++] = EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR;
		list[n++] = EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT_KHR;
		list[n] = EGL_NONE;
		context = eglCreateContext(g_display, config, EGL_NO_CONTEXT, list);
		fprintf(stderr, "a2: egl: compatibility context %d.%d: %s (0x%x)\n", versions[i][0], versions[i][1],
		        context == EGL_NO_CONTEXT ? "refused" : "created", eglGetError());
	}
	if (context == EGL_NO_CONTEXT || !eglMakeCurrent(g_display, g_surface, g_surface, context)) {
		fprintf(stderr, "a2: egl: no compatibility context could be made current\n");
		return 0;
	}
	eglQuerySurface(g_display, g_surface, EGL_WIDTH, &g_width);
	eglQuerySurface(g_display, g_surface, EGL_HEIGHT, &g_height);
	fprintf(stderr, "a2: egl: surface %dx%d\n", g_width, g_height);
	fprintf(stderr, "a2: gl: version %s\n", (const char *)glGetString(GL_VERSION));
	fprintf(stderr, "a2: gl: renderer %s\n", (const char *)glGetString(GL_RENDERER));
	GLint mask = 0;
	glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &mask);
	fprintf(stderr, "a2: gl: profile mask 0x%x (2 = compatibility)\n", mask);
	return 1;
}

/* Clears to a known colour and sets a 2D projection over the unit square. */
static void begin_2d(float r, float g, float b)
{
	glViewport(0, 0, g_width, g_height);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
	glClearColor(r, g, b, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0.0, 1.0, 0.0, 1.0, -1.0, 1.0);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
}

typedef void (*draw_fn)(double t);
typedef int (*check_fn)(char *detail, size_t cap);

/* Draws for a few seconds; the check reads the back buffer of the last frame before it is shown. */
static void run(const char *name, draw_fn draw, check_fn check, double seconds)
{
	int pass = 1;
	char detail[160] = "";
	double start = now_seconds();
	for (;;) {
		double t = now_seconds() - start;
		draw(t);
		int last = t >= seconds;
		if (last)
			pass = check(detail, sizeof(detail));
		eglSwapBuffers(g_display, g_surface);
		if (last)
			break;
	}
	result(name, pass, detail);
}

/* 1. Immediate mode: a red triangle over the centre on dark blue. */
static void draw_immediate(double t)
{
	(void)t;
	begin_2d(0.0f, 0.0f, 0.3f);
	glBegin(GL_TRIANGLES);
	glColor3f(1.0f, 0.0f, 0.0f);
	glVertex2f(0.2f, 0.2f);
	glVertex2f(0.8f, 0.2f);
	glVertex2f(0.5f, 0.8f);
	glEnd();
}
static int check_immediate(char *detail, size_t cap)
{
	unsigned char p[4];
	read_pixel(0.5f, 0.45f, p);
	GLenum e = glGetError();
	describe(detail, cap, p, e);
	return e == GL_NO_ERROR && near(p, 255, 0, 0);
}

/* 2. The matrix stack: a green quad turning in perspective (glFrustum, push/pop, translate, rotate). */
static void draw_matrices(double t)
{
	begin_2d(0.0f, 0.0f, 0.0f);
	glEnable(GL_DEPTH_TEST);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	double aspect = (double)g_width / g_height;
	glFrustum(-0.1 * aspect, 0.1 * aspect, -0.1, 0.1, 0.1, 100.0);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glPushMatrix();
	glTranslatef(0.0f, 0.0f, -3.0f);
	glRotatef((float)(t * 40.0), 0.0f, 1.0f, 0.0f);
	glColor3f(0.0f, 1.0f, 0.0f);
	glBegin(GL_QUADS);
	glVertex3f(-1.0f, -1.0f, 0.0f);
	glVertex3f(1.0f, -1.0f, 0.0f);
	glVertex3f(1.0f, 1.0f, 0.0f);
	glVertex3f(-1.0f, 1.0f, 0.0f);
	glEnd();
	glPopMatrix();
}
static int check_matrices(char *detail, size_t cap)
{
	unsigned char p[4];
	read_pixel(0.5f, 0.5f, p);
	GLint depth = 0;
	glGetIntegerv(GL_MODELVIEW_STACK_DEPTH, &depth);
	GLenum e = glGetError();
	describe(detail, cap, p, e);
	return e == GL_NO_ERROR && near(p, 0, 255, 0) && depth == 1;
}

/* 3. Client-side arrays with 32-bit indices: a blue quad from glDrawElements (GL_UNSIGNED_INT). */
static void draw_arrays(double t)
{
	(void)t;
	static const GLfloat vertices[] = { 0.25f, 0.25f, 0.75f, 0.25f, 0.75f, 0.75f, 0.25f, 0.75f };
	static const GLubyte colours[] = { 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255 };
	static const GLuint indices[] = { 0, 1, 2, 0, 2, 3 };
	begin_2d(0.2f, 0.2f, 0.2f);
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, vertices);
	glColorPointer(4, GL_UNSIGNED_BYTE, 0, colours);
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, indices);
	glDisableClientState(GL_COLOR_ARRAY);
	glDisableClientState(GL_VERTEX_ARRAY);
}
static int check_arrays(char *detail, size_t cap)
{
	unsigned char p[4];
	read_pixel(0.5f, 0.5f, p);
	GLenum e = glGetError();
	describe(detail, cap, p, e);
	return e == GL_NO_ERROR && near(p, 0, 0, 255);
}

/* A 100x60 (non-power-of-two) checker: white and orange squares, with a transparent left half when alpha_hole. */
static GLuint make_texture(int alpha_hole)
{
	static unsigned char pixels[60][100][4];
	for (int y = 0; y < 60; y++)
		for (int x = 0; x < 100; x++) {
			int white = ((x / 10) + (y / 10)) % 2 == 0;
			unsigned char *p = pixels[y][x];
			p[0] = 255;
			p[1] = white ? 255 : 128;
			p[2] = white ? 255 : 0;
			p[3] = alpha_hole && x < 50 ? 0 : 255;
		}
	GLuint id = 0;
	glGenTextures(1, &id);
	glBindTexture(GL_TEXTURE_2D, id);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 100, 60, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	return id;
}

static GLuint g_checker, g_holed;

static void textured_quad(float u)
{
	glBegin(GL_QUADS);
	glTexCoord2f(0.0f, 0.0f);
	glVertex2f(0.1f, 0.1f);
	glTexCoord2f(u, 0.0f);
	glVertex2f(0.9f, 0.1f);
	glTexCoord2f(u, u);
	glVertex2f(0.9f, 0.9f);
	glTexCoord2f(0.0f, u);
	glVertex2f(0.1f, 0.9f);
	glEnd();
}

/* 4. A non-power-of-two texture repeated three times with GL_MODULATE: white vertex colour keeps the texels. */
static void draw_texture(double t)
{
	(void)t;
	begin_2d(0.0f, 0.0f, 0.0f);
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, g_checker);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glColor4ub(255, 255, 255, 255);
	textured_quad(3.0f);
	glDisable(GL_TEXTURE_2D);
}
static int check_texture(char *detail, size_t cap)
{
	/* The quad spans 0.1-0.9; three repeats of a 10-column checker: near the left edge lies a white square. */
	unsigned char p[4];
	read_pixel(0.105f, 0.105f, p);
	GLenum e = glGetError();
	describe(detail, cap, p, e);
	return e == GL_NO_ERROR && (near(p, 255, 255, 255) || near(p, 255, 128, 0));
}

/* 5. The alpha test and blending: the transparent left half of the texture shows the green clear colour. */
static void draw_alpha(double t)
{
	(void)t;
	begin_2d(0.0f, 0.6f, 0.0f);
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, g_holed);
	glEnable(GL_ALPHA_TEST);
	glAlphaFunc(GL_GREATER, 0.5f);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glColor4ub(255, 255, 255, 255);
	textured_quad(1.0f);
	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_TEXTURE_2D);
}
static int check_alpha(char *detail, size_t cap)
{
	unsigned char left[4], right[4];
	read_pixel(0.3f, 0.5f, left);
	read_pixel(0.7f, 0.5f, right);
	GLenum e = glGetError();
	snprintf(detail, cap, "(left %d,%d,%d right %d,%d,%d; glGetError 0x%x)", left[0], left[1], left[2], right[0],
	         right[1], right[2], e);
	return e == GL_NO_ERROR && near(left, 0, 153, 0) && !near(right, 0, 153, 0);
}

/* 6. glPushAttrib / glPopAttrib and glPolygonOffset: state comes back, no errors. */
static void draw_attrib(double t)
{
	(void)t;
	begin_2d(0.3f, 0.0f, 0.3f);
}
static int check_attrib(char *detail, size_t cap)
{
	glEnable(GL_BLEND);
	glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT);
	glDisable(GL_BLEND);
	glPopAttrib();
	GLboolean blend = glIsEnabled(GL_BLEND);
	glDisable(GL_BLEND);
	glEnable(GL_POLYGON_OFFSET_FILL);
	glPolygonOffset(-1.0f, -1.0f);
	glDisable(GL_POLYGON_OFFSET_FILL);
	GLenum e = glGetError();
	snprintf(detail, cap, "(blend after pop %d; glGetError 0x%x)", blend, e);
	return e == GL_NO_ERROR && blend == GL_TRUE;
}

/* 7. Load: 1,000 immediate-mode quads per frame, as the race's sprites and HUD draw; frame time without vsync. */
static int g_frames;
static double g_draw_seconds;
static void draw_load(double t)
{
	(void)t;
	double start = now_seconds();
	begin_2d(0.0f, 0.0f, 0.0f);
	glBegin(GL_QUADS);
	for (int i = 0; i < 1000; i++) {
		float x = (float)(i % 40) / 40.0f, y = (float)(i / 40) / 25.0f;
		glColor3ub((GLubyte)(i * 7), (GLubyte)(i * 13), (GLubyte)(i * 29));
		glVertex2f(x, y);
		glVertex2f(x + 0.02f, y);
		glVertex2f(x + 0.02f, y + 0.03f);
		glVertex2f(x, y + 0.03f);
	}
	glEnd();
	g_draw_seconds += now_seconds() - start;
	g_frames++;
}
static int check_load(char *detail, size_t cap)
{
	GLenum e = glGetError();
	double ms = g_frames ? g_draw_seconds * 1000.0 / g_frames : 0.0;
	snprintf(detail, cap, "(%d frames, %.2f ms per frame to submit; glGetError 0x%x)", g_frames, ms, e);
	return e == GL_NO_ERROR && g_frames > 0;
}

int main(void)
{
	fprintf(stderr, "a2: phase 0, the Compatibility-profile check\n");
	if (!create_context()) {
		result("context", 0, "no compatibility context");
		return 1;
	}
	eglSwapInterval(g_display, 1);
	g_checker = make_texture(0);
	g_holed = make_texture(1);
	result("textures created", glGetError() == GL_NO_ERROR, NULL);

	run("1 immediate mode", draw_immediate, check_immediate, 4.0);
	run("2 matrix stack", draw_matrices, check_matrices, 4.0);
	run("3 client arrays, 32-bit indices", draw_arrays, check_arrays, 4.0);
	run("4 NPOT texture, repeat, modulate", draw_texture, check_texture, 4.0);
	run("5 alpha test and blending", draw_alpha, check_alpha, 4.0);
	run("6 push/pop attrib, polygon offset", draw_attrib, check_attrib, 2.0);

	double start = now_seconds();
	eglSwapInterval(g_display, 0);
	run("7 1,000 quads per frame", draw_load, check_load, 5.0);
	double total = now_seconds() - start;
	fprintf(stderr, "a2: load: %d frames in %.2f s, %.1f fps without vsync\n", g_frames, total, g_frames / total);

	fprintf(stderr, "a2: phase 0 done: %d check(s) failed\n", g_failures);
	fflush(stderr);
	return g_failures ? 2 : 0;
}
