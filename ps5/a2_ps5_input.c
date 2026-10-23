/* A2 Racer II for PS5 - the DualSense and the PS5's keyboard as the game's mouse, keyboard and wheel (decision
 * log D5).
 *
 * The game reads SDL events (the menu: mouse and keys; the race: keys) and, in the race, a wheel's analog steering,
 * throttle and brake (Game.ini DEVICE=3, src/main.c read_input). SDL's PS5 pad driver gives neither the touchpad
 * nor a mapping, so the pad is read here with scePadRead and turned into what the game already understands:
 *   SDL_PollEvent         returns the pad's key and mouse events before SDL's own (--wrap)
 *   SDL_GetKeyboardState  includes the keys the pad holds down (--wrap)
 *   a2ps5_drive           the race's analog input (patch in src/main.c read_input)
 *   SDL_StartTextInput    opens the PS5's keyboard (sceImeDialog); its text comes back as SDL_TEXTINPUT (--wrap)
 * The layout is in README.md and in a2ps5_layout below. */
#include "a2_ps5.h"

#include <SDL2/SDL.h>
#include <dlfcn.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---- the pad (layout of scePadRead's sample: ps5-homebrew-ui's pad.cpp, the touch part as on the PS4) ---- */

typedef struct {
	uint32_t buttons;
	uint8_t left_x, left_y, right_x, right_y, l2, r2;
	uint8_t reserved0[42];
	uint8_t touch_count; /* 0x34 */
	uint8_t reserved1[7];
	struct {
		uint16_t x, y;
		uint8_t id, reserved[3];
	} touch[2];          /* 0x3c */
	int32_t connected;   /* 0x4c */
	uint64_t timestamp;  /* 0x50 */
	uint8_t extension[16];
	uint8_t connected_count;
	uint8_t reserved2[15];
} pad_sample;
_Static_assert(sizeof(pad_sample) == 120, "scePadRead sample layout");
_Static_assert(offsetof(pad_sample, touch_count) == 0x34, "touch count");
_Static_assert(offsetof(pad_sample, touch) == 0x3c, "touch points");
_Static_assert(offsetof(pad_sample, connected) == 0x4c, "connected flag");

enum {
	PAD_L3 = 0x2, PAD_R3 = 0x4, PAD_OPTIONS = 0x8, PAD_UP = 0x10, PAD_RIGHT = 0x20, PAD_DOWN = 0x40, PAD_LEFT = 0x80,
	PAD_L2 = 0x100, PAD_R2 = 0x200, PAD_L1 = 0x400, PAD_R1 = 0x800, PAD_TRIANGLE = 0x1000, PAD_CIRCLE = 0x2000,
	PAD_CROSS = 0x4000, PAD_SQUARE = 0x8000, PAD_TOUCHPAD = 0x100000, PAD_INTERCEPTED = 0x80000000u,
};

int sceUserServiceInitialize(void *);
int sceUserServiceGetInitialUser(int32_t *);
int sceUserServiceGetForegroundUser(int32_t *);
int scePadInit(void);
int scePadOpen(int32_t user, int type, int index, const void *parameters);
int scePadGetHandle(int32_t user, int type, int index);
int scePadRead(int handle, pad_sample *samples, int count);

static int g_pad = -1;
static pad_sample g_now;     /* the latest sample */
static uint32_t g_held;      /* buttons down after the latest sample, without the system's */
static int g_in_race;        /* inside a2view_main: the race's layout */

static void pad_open(void)
{
	static int tried;
	if (g_pad >= 0 || tried)
		return;
	tried = 1;
	int32_t user = -1;
	sceUserServiceInitialize(NULL); /* already initialised is fine */
	if (sceUserServiceGetInitialUser(&user) != 0 && sceUserServiceGetForegroundUser(&user) != 0) {
		fprintf(stderr, "a2: pad: no user\n");
		return;
	}
	scePadInit();
	g_pad = scePadOpen(user, 0, 0, NULL);
	if (g_pad < 0)
		g_pad = scePadGetHandle(user, 0, 0); /* opened already (SDL's driver) */
	fprintf(stderr, "a2: pad: user 0x%x handle %d\n", (unsigned)user, g_pad);
}

/* ---- the events the pad makes ---- */

#define QUEUE 64
static SDL_Event g_queue[QUEUE];
static int g_head, g_count;

static void push(const SDL_Event *ev)
{
	if (g_count == QUEUE)
		return;
	g_queue[(g_head + g_count++) % QUEUE] = *ev;
}

static void push_key(SDL_Keycode sym, SDL_Scancode scancode, int down)
{
	SDL_Event ev;
	memset(&ev, 0, sizeof ev);
	ev.type = down ? SDL_KEYDOWN : SDL_KEYUP;
	ev.key.timestamp = SDL_GetTicks();
	ev.key.state = down ? SDL_PRESSED : SDL_RELEASED;
	ev.key.keysym.sym = sym;
	ev.key.keysym.scancode = scancode;
	push(&ev);
}

static void tap_key(SDL_Keycode sym, SDL_Scancode scancode)
{
	push_key(sym, scancode, 1);
	push_key(sym, scancode, 0);
}

/* The menu's mouse: the 640x480 picture's coordinates (src/menu/a2menu.c takes them as they come on the PS5). */
static float g_mouse_x = 320.0f, g_mouse_y = 240.0f;

static void mouse_moved(float dx, float dy)
{
	float x = g_mouse_x + dx, y = g_mouse_y + dy;
	x = x < 0.0f ? 0.0f : x > 639.0f ? 639.0f : x;
	y = y < 0.0f ? 0.0f : y > 479.0f ? 479.0f : y;
	if ((int)x == (int)g_mouse_x && (int)y == (int)g_mouse_y) {
		g_mouse_x = x;
		g_mouse_y = y;
		return;
	}
	SDL_Event ev;
	memset(&ev, 0, sizeof ev);
	ev.type = SDL_MOUSEMOTION;
	ev.motion.timestamp = SDL_GetTicks();
	ev.motion.x = (int)x;
	ev.motion.y = (int)y;
	ev.motion.xrel = (int)x - (int)g_mouse_x;
	ev.motion.yrel = (int)y - (int)g_mouse_y;
	g_mouse_x = x;
	g_mouse_y = y;
	push(&ev);
}

static void mouse_button(int down)
{
	SDL_Event ev;
	memset(&ev, 0, sizeof ev);
	ev.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
	ev.button.timestamp = SDL_GetTicks();
	ev.button.button = SDL_BUTTON_LEFT;
	ev.button.state = down ? SDL_PRESSED : SDL_RELEASED;
	ev.button.clicks = 1;
	ev.button.x = (int)g_mouse_x;
	ev.button.y = (int)g_mouse_y;
	push(&ev);
}

/* ---- the PS5's keyboard (sceImeDialog), as ProsperoTV opens it in a title ---- */

typedef struct {
	int32_t user_id, type;
	uint64_t supported_languages;
	int32_t enter_label, input_method;
	void *filter;
	uint32_t option, max_text_length;
	uint16_t *input_text_buffer;
	float pos_x, pos_y;
	int32_t horizontal_alignment, vertical_alignment;
	const uint16_t *placeholder, *title;
	int8_t reserved[16];
} ime_param;
_Static_assert(sizeof(ime_param) == 96, "PS5 IME parameter layout");

typedef struct {
	int32_t outcome; /* 0 OK, 1 cancelled, 2 aborted */
	int8_t reserved[12];
} ime_result;

int sceImeDialogInit(const ime_param *param, const void *extended);
int sceImeDialogGetStatus(void);
int sceImeDialogGetResult(ime_result *result);
int sceImeDialogTerm(void);
int sceSysmoduleLoadModule(uint16_t id);

#define IME_MODULE 0x0096
#define NAME_CHARACTERS 8 /* src/menu/m_newplayer.c takes up to eight, printable ASCII */

static int g_ime_wanted, g_ime_open;
static uint32_t g_ime_since;
static uint16_t g_ime_text[NAME_CHARACTERS + 1];

static void ime_start(void)
{
	static int ready;
	if (!ready) {
		/* libSceCommonDialog has no stub in the payload SDK: looked up at run time. */
		void *common = dlopen("libSceCommonDialog.sprx", RTLD_LAZY);
		int (*initialize)(void) = common ? (int (*)(void))dlsym(common, "sceCommonDialogInitialize") : NULL;
		int r = initialize ? initialize() : -1;
		int m = sceSysmoduleLoadModule(IME_MODULE);
		fprintf(stderr, "a2: ime: common dialog 0x%x, module 0x%x\n", (unsigned)r, (unsigned)m);
		ready = 1;
	}
	int32_t user = -1;
	sceUserServiceGetForegroundUser(&user);
	static const uint16_t title[] = {'P', 'l', 'a', 'y', 'e', 'r', ' ', 'n', 'a', 'm', 'e', 0};
	memset(g_ime_text, 0, sizeof g_ime_text);
	ime_param p;
	memset(&p, 0, sizeof p);
	p.user_id = user;
	p.type = 1; /* basic Latin: the game's font has ASCII only */
	p.max_text_length = NAME_CHARACTERS;
	p.input_text_buffer = g_ime_text;
	p.horizontal_alignment = 1;
	p.vertical_alignment = 1;
	p.title = title;
	int r = sceImeDialogInit(&p, NULL);
	fprintf(stderr, "a2: ime: open 0x%x\n", (unsigned)r);
	g_ime_open = r == 0;
	g_ime_since = SDL_GetTicks();
	if (!g_ime_open) /* no keyboard: the name stays as the game made it */
		tap_key(SDLK_RETURN, SDL_SCANCODE_RETURN);
}

static void ime_poll(void)
{
	/* Opened once Cross (which chose "new player") is up, or the keyboard takes that press as Enter. */
	if (g_ime_wanted && !(g_held & PAD_CROSS)) {
		g_ime_wanted = 0;
		ime_start();
	}
	if (!g_ime_open)
		return;
	int status = sceImeDialogGetStatus();
	if (status == 1 || (status == 0 && SDL_GetTicks() - g_ime_since < 1000))
		return;
	ime_result result;
	memset(&result, 0, sizeof result);
	sceImeDialogGetResult(&result);
	sceImeDialogTerm();
	g_ime_open = 0;
	if (result.outcome != 0) {
		tap_key(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
		return;
	}
	SDL_Event ev;
	memset(&ev, 0, sizeof ev);
	ev.type = SDL_TEXTINPUT;
	ev.text.timestamp = SDL_GetTicks();
	size_t n = 0;
	for (size_t i = 0; g_ime_text[i] && n < NAME_CHARACTERS; i++)
		if (g_ime_text[i] >= 32 && g_ime_text[i] <= 126)
			ev.text.text[n++] = (char)g_ime_text[i];
	ev.text.text[n] = 0;
	fprintf(stderr, "a2: ime: \"%s\"\n", ev.text.text);
	if (n > 0)
		push(&ev);
	tap_key(SDLK_RETURN, SDL_SCANCODE_RETURN);
}

void __real_SDL_StartTextInput(void);
void __wrap_SDL_StartTextInput(void)
{
	__real_SDL_StartTextInput();
	if (!g_ime_open)
		g_ime_wanted = 1;
}

/* ---- the layout (README.md) ---- */

/* Edges of one button: pressed now, not before. */
#define PRESSED(b) ((now & (b)) && !(before & (b)))
#define RELEASED(b) (!(now & (b)) && (before & (b)))

static int g_camera; /* the race's camera views: keys 1, 2 and 4 (src/main.c, handle_game_cameras) */

static void layout_buttons(uint32_t now, uint32_t before)
{
	if (g_ime_open)
		return; /* the keyboard has the pad */
	if (g_in_race) {
		/* Cross: Enter (start, the end of a race) and J (yes, end the race); Circle: N (no) */
		if (PRESSED(PAD_CROSS)) {
			tap_key(SDLK_RETURN, SDL_SCANCODE_RETURN);
			tap_key(SDLK_j, SDL_SCANCODE_J);
		}
		if (PRESSED(PAD_CIRCLE))
			tap_key(SDLK_n, SDL_SCANCODE_N);
		if (PRESSED(PAD_OPTIONS))
			tap_key(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE); /* the "stop the race?" question; again: back */
		if (PRESSED(PAD_TOUCHPAD))
			tap_key(SDLK_p, SDL_SCANCODE_P); /* pause */
		if (PRESSED(PAD_TRIANGLE)) {
			static const struct {
				SDL_Keycode sym;
				SDL_Scancode code;
			} views[3] = {{SDLK_1, SDL_SCANCODE_1}, {SDLK_2, SDL_SCANCODE_2}, {SDLK_4, SDL_SCANCODE_4}};
			g_camera = (g_camera + 1) % 3;
			tap_key(views[g_camera].sym, views[g_camera].code);
		}
		return;
	}
	/* the menu: the D-pad moves the focus, Cross takes it, Circle goes back; the touchpad is the mouse */
	if (PRESSED(PAD_UP))
		tap_key(SDLK_UP, SDL_SCANCODE_UP);
	if (PRESSED(PAD_DOWN))
		tap_key(SDLK_DOWN, SDL_SCANCODE_DOWN);
	if (PRESSED(PAD_LEFT))
		tap_key(SDLK_LEFT, SDL_SCANCODE_LEFT);
	if (PRESSED(PAD_RIGHT))
		tap_key(SDLK_RIGHT, SDL_SCANCODE_RIGHT);
	if (PRESSED(PAD_CROSS) || PRESSED(PAD_OPTIONS))
		tap_key(SDLK_RETURN, SDL_SCANCODE_RETURN);
	if (PRESSED(PAD_CIRCLE))
		tap_key(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
	if (PRESSED(PAD_TOUCHPAD) || PRESSED(PAD_R2))
		mouse_button(1);
	if (RELEASED(PAD_TOUCHPAD) || RELEASED(PAD_R2))
		mouse_button(0);
}

/* The touchpad as a laptop's: a finger moving moves the pointer; the left stick moves it too. */
static void layout_pointer(const pad_sample *s, float dt)
{
	static int tracking = -1;
	static uint16_t last_x, last_y;
	if (s->touch_count > 0) {
		if (tracking == s->touch[0].id) {
			/* the touchpad reports about 1920 x 1080; half a pixel of the 640 x 480 picture per unit */
			mouse_moved((s->touch[0].x - last_x) * 0.5f, (s->touch[0].y - last_y) * 0.5f);
		}
		tracking = s->touch[0].id;
		last_x = s->touch[0].x;
		last_y = s->touch[0].y;
	} else {
		tracking = -1;
	}
	float sx = (s->left_x - 128) / 127.0f, sy = (s->left_y - 128) / 127.0f;
	if (sx * sx + sy * sy > 0.04f) /* past a fifth of the way */
		mouse_moved(sx * 420.0f * dt, sy * 420.0f * dt);
}

static void input_tick(void)
{
	pad_open();
	if (g_pad < 0)
		return;
	static uint32_t last_tick;
	uint32_t ticks = SDL_GetTicks();
	float dt = last_tick ? (ticks - last_tick) / 1000.0f : 0.0f;
	last_tick = ticks;
	if (dt > 0.1f)
		dt = 0.1f;
	pad_sample samples[64];
	int n = scePadRead(g_pad, samples, 64);
	for (int i = 0; i < n; i++) { /* every sample, so a short tap between two frames still counts */
		const pad_sample *s = &samples[i];
		uint32_t now = s->connected && !(s->buttons & PAD_INTERCEPTED) ? s->buttons : 0;
		layout_buttons(now, g_held);
		g_held = now;
		g_now = *s;
	}
	if (n > 0 && !g_in_race && !g_ime_open)
		layout_pointer(&g_now, dt);
	ime_poll();
}

int __real_SDL_PollEvent(SDL_Event *event);
int __wrap_SDL_PollEvent(SDL_Event *event)
{
	if (g_count == 0)
		input_tick();
	if (g_count > 0) {
		if (event) {
			*event = g_queue[g_head];
			g_head = (g_head + 1) % QUEUE;
			g_count--;
		}
		return 1;
	}
	return __real_SDL_PollEvent(event);
}

/* The keys the race polls while held: L1 the view behind (key 3), R3 the horn (T). */
const Uint8 *__real_SDL_GetKeyboardState(int *numkeys);
const Uint8 *__wrap_SDL_GetKeyboardState(int *numkeys)
{
	static Uint8 keys[SDL_NUM_SCANCODES];
	int n = 0;
	const Uint8 *real = __real_SDL_GetKeyboardState(&n);
	memcpy(keys, real, n < SDL_NUM_SCANCODES ? (size_t)n : sizeof keys);
	if (g_in_race && !g_ime_open) {
		keys[SDL_SCANCODE_3] |= (g_held & PAD_L1) != 0;
		keys[SDL_SCANCODE_T] |= (g_held & PAD_R3) != 0;
	}
	if (numkeys)
		*numkeys = SDL_NUM_SCANCODES;
	return keys;
}

/* The race's analog input, as Game.ini DEVICE=3 (a wheel) gives it: steering -100..100 on the left stick, throttle
   0..100 on R2, brake 0..100 on L2; turbo on Cross or R1, handbrake on Square. Returns 0 without a pad. */
int a2ps5_drive(float *steer, float *throttle, float *brake, int *turbo, int *handbrake)
{
	if (g_pad < 0 || !g_now.connected)
		return 0;
	float x = (g_now.left_x - 128) / 127.0f;
	x = x > 1.0f ? 1.0f : x < -1.0f ? -1.0f : x;
	const float dead = 0.08f; /* the stick's rest wobble */
	x = x > dead ? (x - dead) / (1.0f - dead) : x < -dead ? (x + dead) / (1.0f - dead) : 0.0f;
	*steer = x * 100.0f;
	*throttle = g_now.r2 * 100.0f / 255.0f;
	*brake = g_now.l2 * 100.0f / 255.0f;
	*turbo = (g_held & (PAD_CROSS | PAD_R1)) != 0;
	*handbrake = (g_held & PAD_SQUARE) != 0;
	return 1;
}

/* The race runs inside the menu (src/menu/a2menu.c run_race): its layout while it does. */
int __real_a2view_main(int argc, char **argv);
int __wrap_a2view_main(int argc, char **argv)
{
	g_in_race = 1;
	g_camera = 0;
	int r = __real_a2view_main(argc, argv);
	g_in_race = 0;
	return r;
}
