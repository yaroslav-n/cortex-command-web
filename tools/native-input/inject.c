// Input for the untouched original on a machine whose screen is locked, where the
// window server delivers no mouse or keyboard events. Loaded with
// DYLD_INSERT_LIBRARIES; nothing in the original is changed. It interposes
// SDL_PollEvent and hands the game the events of a script, at the script's times,
// before whatever SDL itself has.
//
// Script (CORTEX_INPUT_SCRIPT), one command per line, '#' comments:
//   wait <ms>          pause before the next command
//   move <x> <y>       mouse motion to window coordinates (points)
//   click <x> <y>      motion, then left button down and, 80 ms later, up
//   drag <x> <y> <x2> <y2>  press at the first point, move to the second in steps, release
//   key <name>         key down and, 80 ms later, up (names as in SDL_GetScancodeFromName);
//                      chords such as RAlt+W or LAlt+W hold LAlt/RAlt/LCtrl/LShift around it
//   text <string>      text input
//   focus              the window gains focus (a locked screen never gives it)
//   quit               ask the game to quit
// The same script drives the browser port through run-browser.sh.
//
// CORTEX_INPUT_FPS=<n> also paces SDL_GL_SwapWindow to n frames a second, as VSync
// would: a locked screen never sends the refresh a VSync'd swap waits for, so the
// game otherwise runs uncapped, and some of its behaviour depends on frame rate
// (the camera's resting point, for one).
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	Uint64 due; // milliseconds after the first poll
	SDL_Event event;
	SDL_Scancode stateKey; // key whose SDL keyboard state follows this event, or SDL_SCANCODE_UNKNOWN
	SDL_Keymod modState; // SDL's modifier state from this event on (the game asks SDL_GetModState)
} Pending;

static SDL_Keymod s_ScriptMods = SDL_KMOD_NONE;

static Pending *s_Queue = NULL;
static size_t s_Count = 0, s_Next = 0, s_Capacity = 0;
static Uint64 s_Start = 0;
static bool s_Loaded = false;
static float s_MouseX = 0, s_MouseY = 0;

static void Push(Uint64 due, const SDL_Event *event, SDL_Scancode stateKey) {
	if (s_Count == s_Capacity) {
		s_Capacity = s_Capacity ? s_Capacity * 2 : 64;
		s_Queue = realloc(s_Queue, s_Capacity * sizeof(Pending));
	}
	s_Queue[s_Count].due = due;
	s_Queue[s_Count].event = *event;
	s_Queue[s_Count].stateKey = stateKey;
	s_Queue[s_Count].modState = s_ScriptMods;
	++s_Count;
}

static void Motion(Uint64 at, float x, float y) {
	SDL_Event e;
	SDL_zero(e);
	e.type = SDL_EVENT_MOUSE_MOTION;
	e.motion.which = 1;
	e.motion.x = x;
	e.motion.y = y;
	e.motion.xrel = x - s_MouseX;
	e.motion.yrel = y - s_MouseY;
	s_MouseX = x;
	s_MouseY = y;
	Push(at, &e, SDL_SCANCODE_UNKNOWN);
}

static void Button(Uint64 at, bool down) {
	SDL_Event e;
	SDL_zero(e);
	e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
	e.button.which = 1;
	e.button.button = SDL_BUTTON_LEFT;
	e.button.down = down;
	e.button.clicks = 1;
	e.button.x = s_MouseX;
	e.button.y = s_MouseY;
	Push(at, &e, SDL_SCANCODE_UNKNOWN);
}

static void Key(Uint64 at, SDL_Scancode scancode, bool down) {
	SDL_Event e;
	SDL_zero(e);
	e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
	e.key.scancode = scancode;
	e.key.key = SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, false);
	e.key.down = down;
	e.key.mod = s_ScriptMods;
	Push(at, &e, scancode);
}

static SDL_Keymod ModifierOf(const char *name, SDL_Scancode *scancode) {
	static const struct { const char *name; SDL_Scancode scancode; SDL_Keymod mod; } modifiers[] = {
		{"LAlt", SDL_SCANCODE_LALT, SDL_KMOD_LALT}, {"RAlt", SDL_SCANCODE_RALT, SDL_KMOD_RALT},
		{"LCtrl", SDL_SCANCODE_LCTRL, SDL_KMOD_LCTRL}, {"LShift", SDL_SCANCODE_LSHIFT, SDL_KMOD_LSHIFT},
	};
	for (size_t i = 0; i < sizeof(modifiers) / sizeof(modifiers[0]); ++i) {
		if (!strcmp(name, modifiers[i].name)) {
			*scancode = modifiers[i].scancode;
			return modifiers[i].mod;
		}
	}
	return SDL_KMOD_NONE;
}

static void Load(void) {
	s_Loaded = true;
	const char *path = getenv("CORTEX_INPUT_SCRIPT");
	FILE *file = path ? fopen(path, "r") : NULL;
	if (!file) {
		fprintf(stderr, "inject: no script (CORTEX_INPUT_SCRIPT)\n");
		return;
	}
	char line[512];
	Uint64 at = 0;
	while (fgets(line, sizeof(line), file)) {
		line[strcspn(line, "\r\n")] = 0;
		char command[32] = {0};
		if (line[0] == '#' || sscanf(line, "%31s", command) != 1) {
			continue;
		}
		const char *rest = line + strlen(command);
		while (*rest == ' ') {
			++rest;
		}
		float x, y;
		if (!strcmp(command, "wait")) {
			at += (Uint64)atoll(rest);
		} else if (!strcmp(command, "move") && sscanf(rest, "%f %f", &x, &y) == 2) {
			Motion(at, x, y);
		} else if (!strcmp(command, "click") && sscanf(rest, "%f %f", &x, &y) == 2) {
			Motion(at, x, y);
			Button(at + 30, true);
			Button(at + 110, false);
			at += 110;
		} else if (!strcmp(command, "drag")) {
			float x2, y2;
			if (sscanf(rest, "%f %f %f %f", &x, &y, &x2, &y2) != 4) {
				fprintf(stderr, "inject: cannot read '%s'\n", line);
				continue;
			}
			Motion(at, x, y);
			Button(at + 30, true);
			for (int step = 1; step <= 10; ++step) {
				Motion(at + 30 + step * 30, x + (x2 - x) * step / 10.0F, y + (y2 - y) * step / 10.0F);
			}
			Button(at + 400, false);
			at += 400;
		} else if (!strcmp(command, "key")) {
			char chord[128];
			snprintf(chord, sizeof(chord), "%s", rest);
			char *keyName = strrchr(chord, '+');
			SDL_Scancode modifierKey = SDL_SCANCODE_UNKNOWN;
			SDL_Keymod modifier = SDL_KMOD_NONE;
			if (keyName && keyName != chord) {
				*keyName++ = 0;
				modifier = ModifierOf(chord, &modifierKey);
				if (modifier == SDL_KMOD_NONE) {
					fprintf(stderr, "inject: unknown modifier '%s'\n", chord);
					continue;
				}
			} else {
				keyName = chord;
			}
			SDL_Scancode scancode = SDL_GetScancodeFromName(keyName);
			if (scancode == SDL_SCANCODE_UNKNOWN) {
				fprintf(stderr, "inject: unknown key '%s'\n", keyName);
				continue;
			}
			if (modifier != SDL_KMOD_NONE) {
				s_ScriptMods |= modifier;
				Key(at, modifierKey, true);
				at += 40;
			}
			Key(at, scancode, true);
			Key(at + 80, scancode, false);
			at += 80;
			if (modifier != SDL_KMOD_NONE) {
				s_ScriptMods &= ~modifier;
				Key(at + 40, modifierKey, false);
				at += 40;
			}
		} else if (!strcmp(command, "text")) {
			// One character per event, 10 ms apart, as typing produces; the original
			// keeps only the first 32 bytes of any one text event.
			for (const char *c = rest; *c; ++c) {
				char one[2] = {*c, 0};
				SDL_Event e;
				SDL_zero(e);
				e.type = SDL_EVENT_TEXT_INPUT;
				e.text.text = SDL_strdup(one);
				Push(at, &e, SDL_SCANCODE_UNKNOWN);
				at += 10;
			}
		} else if (!strcmp(command, "focus")) {
			SDL_Event e;
			SDL_zero(e);
			e.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
			Push(at, &e, SDL_SCANCODE_UNKNOWN);
		} else if (!strcmp(command, "quit")) {
			SDL_Event e;
			SDL_zero(e);
			e.type = SDL_EVENT_QUIT;
			Push(at, &e, SDL_SCANCODE_UNKNOWN);
		} else {
			fprintf(stderr, "inject: cannot read '%s'\n", line);
		}
	}
	fclose(file);
	fprintf(stderr, "inject: %zu events over %llu ms\n", s_Count, (unsigned long long)at);
}

static bool InjectedPollEvent(SDL_Event *event) {
	if (!s_Loaded) {
		Load();
		s_Start = SDL_GetTicks();
	}
	if (s_Next < s_Count && SDL_GetTicks() - s_Start >= s_Queue[s_Next].due) {
		Pending *pending = &s_Queue[s_Next++];
		SDL_Window *window = NULL;
		int windowCount = 0;
		SDL_Window **windows = SDL_GetWindows(&windowCount);
		if (windows && windowCount > 0) {
			window = windows[0];
		}
		SDL_free(windows);
		SDL_WindowID windowID = window ? SDL_GetWindowID(window) : 0;
		switch (pending->event.type) {
			case SDL_EVENT_MOUSE_MOTION: pending->event.motion.windowID = windowID; break;
			case SDL_EVENT_MOUSE_BUTTON_DOWN:
			case SDL_EVENT_MOUSE_BUTTON_UP: pending->event.button.windowID = windowID; break;
			case SDL_EVENT_KEY_DOWN:
			case SDL_EVENT_KEY_UP: pending->event.key.windowID = windowID; break;
			case SDL_EVENT_TEXT_INPUT: pending->event.text.windowID = windowID; break;
			case SDL_EVENT_WINDOW_FOCUS_GAINED: pending->event.window.windowID = windowID; break;
			default: break;
		}
		pending->event.common.timestamp = SDL_GetTicksNS();
		if (pending->stateKey != SDL_SCANCODE_UNKNOWN) {
			// The GUI library reads SDL's keyboard state array directly rather than
			// the events, so keep that array in step with the injected keys.
			int keyCount = 0;
			bool *state = (bool *)SDL_GetKeyboardState(&keyCount);
			if ((int)pending->stateKey < keyCount) {
				state[pending->stateKey] = pending->event.type == SDL_EVENT_KEY_DOWN;
			}
		}
		SDL_SetModState(pending->modState);
		*event = pending->event;
		return true;
	}
	return SDL_PollEvent(event);
}

static bool PacedSwapWindow(SDL_Window *window) {
	static double framePeriodNs = -1.0;
	static Uint64 nextFrame = 0;
	if (framePeriodNs < 0.0) {
		const char *fps = getenv("CORTEX_INPUT_FPS");
		framePeriodNs = fps && atof(fps) > 0.0 ? 1e9 / atof(fps) : 0.0;
	}
	bool result = SDL_GL_SwapWindow(window);
	if (framePeriodNs > 0.0) {
		Uint64 now = SDL_GetTicksNS();
		if (nextFrame == 0 || now > nextFrame + (Uint64)(4 * framePeriodNs)) {
			nextFrame = now;
		}
		nextFrame += (Uint64)framePeriodNs;
		if (nextFrame > now) {
			SDL_DelayPrecise(nextFrame - now);
		}
	}
	return result;
}

__attribute__((used)) static const struct {
	const void *replacement;
	const void *replacee;
} s_Interposers[] __attribute__((section("__DATA,__interpose"))) = {
	{(const void *)InjectedPollEvent, (const void *)SDL_PollEvent},
	{(const void *)PacedSwapWindow, (const void *)SDL_GL_SwapWindow},
};
