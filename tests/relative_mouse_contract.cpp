// SDL's browser backend with a window in relative mode, as in play, after the pointer lock
// was lost to another tab or app. When the page gets the focus back before the pointer is
// over the canvas again, SDL must stay in relative mode: the movement must still arrive,
// and a click must ask for the lock again. Before, the game's aim froze while its clicks
// still worked, until a menu turned relative mode off and on.
//
// The page's events are made here (EM_ASM), so the browser grants no real lock: the page
// stands in for it, counting the requests SDL makes and granting the user activation that
// Emscripten asks of a request, and says when the lock is taken and lost.
//
// The browser also keeps from the page the Esc that lets the lock go; SDL must give it to
// the game all the same, so that one Esc in play opens the pause menu.
#include <SDL3/SDL.h>
#include <emscripten.h>
#include <cstdio>
#include <cstdlib>

static void check(bool value, const char* reason) {
	if (!value) {
		std::fprintf(stderr, "FAIL: %s\n", reason);
		std::exit(1);
	}
	std::printf("ok: %s\n", reason);
}

// The motion events waiting, and their movement added up.
static int TakeMotion(float& xrel, float& yrel) {
	int motions = 0;
	xrel = yrel = 0;
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		if (event.type == SDL_EVENT_MOUSE_MOTION) {
			++motions;
			xrel += event.motion.xrel;
			yrel += event.motion.yrel;
		}
	}
	return motions;
}

// The Esc presses waiting (not their repeats); each must be let go too.
static int TakeEscapes() {
	int presses = 0;
	int releases = 0;
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE && !event.key.repeat) {
			++presses;
		} else if (event.type == SDL_EVENT_KEY_UP && event.key.scancode == SDL_SCANCODE_ESCAPE) {
			++releases;
		}
	}
	if (presses != releases) {
		check(false, "every Esc pressed is let go");
	}
	return presses;
}

// Longer than SDL waits for a blur after a lost lock.
static void PassEscapeDelay() {
	const Uint64 start = SDL_GetTicksNS();
	while (SDL_GetTicksNS() - start < SDL_MS_TO_NS(150)) {
	}
}

static int LockRequests() {
	return EM_ASM_INT({ return window.lockRequests; });
}

int main(int, char**) {
	EM_ASM({
		window.lockRequests = 0;
		Module.canvas.requestPointerLock = () => { window.lockRequests++; return Promise.resolve(); };
		Object.defineProperty(navigator, 'userActivation', { configurable: true, get: () => ({ isActive: true, hasBeenActive: true }) });
		window.pointer = (type, x, y, extra) => {
			const box = Module.canvas.getBoundingClientRect();
			Module.canvas.dispatchEvent(new MouseEvent(type, Object.assign({ bubbles: true, cancelable: true, clientX: box.left + x, clientY: box.top + y }, extra)));
		};
		window.lockedElement = null;
		Object.defineProperty(document, 'pointerLockElement', { configurable: true, get: () => window.lockedElement });
		document.exitPointerLock = () => {};
		window.lock = (on) => { window.lockedElement = on ? Module.canvas : null; document.dispatchEvent(new Event('pointerlockchange')); };
		window.key = (type, repeat) => window.dispatchEvent(new KeyboardEvent(type, { key: 'Escape', code: 'Escape', keyCode: 27, repeat: !!repeat, bubbles: true, cancelable: true }));
		window.pageHidden = false;
		Object.defineProperty(document, 'hidden', { configurable: true, get: () => window.pageHidden });
		window.visibility = (hidden) => { window.pageHidden = hidden; document.dispatchEvent(new Event('visibilitychange')); };
	});
	check(SDL_Init(SDL_INIT_VIDEO), "SDL starts");
	SDL_Window* window = SDL_CreateWindow("Relative mouse", 320, 200, 0);
	check(window != nullptr, "the window is made");
	float xrel = 0;
	float yrel = 0;

	// The page has the focus and the pointer is over the canvas, as when a mission starts.
	EM_ASM({ window.dispatchEvent(new FocusEvent('focus')); pointer('mouseenter', 50, 50); pointer('mousemove', 60, 60, { movementX: 10, movementY: 10 }); });
	TakeMotion(xrel, yrel);
	check(SDL_SetWindowRelativeMouseMode(window, true), "the window goes into relative mode");
	check(LockRequests() > 0, "relative mode asks for the pointer lock");
	EM_ASM({ pointer('mousemove', 65, 62, { movementX: 5, movementY: 2 }); });
	check(TakeMotion(xrel, yrel) > 0 && xrel > 0 && yrel > 0, "movement arrives in relative mode");

	// Another tab or app: the pointer leaves the canvas and the page loses the focus...
	EM_ASM({ pointer('mouseleave', -5, -5); window.dispatchEvent(new FocusEvent('blur')); });
	TakeMotion(xrel, yrel);
	// ...and gets it back before the pointer is over the canvas again.
	const int requestsBeforeReturn = LockRequests();
	EM_ASM({ window.dispatchEvent(new FocusEvent('focus')); });
	check(LockRequests() > requestsBeforeReturn, "getting the focus back asks for the pointer lock again");
	EM_ASM({ pointer('mouseenter', 70, 70); pointer('mousemove', 80, 75, { movementX: 7, movementY: 3 }); });
	check(TakeMotion(xrel, yrel) > 0 && xrel > 0 && yrel > 0, "movement arrives after the page gets the focus back, before the lock does");

	const int requestsBeforeClick = LockRequests();
	EM_ASM({ pointer('mousedown', 80, 75, { button: 0, buttons: 1 }); pointer('mouseup', 80, 75, { button: 0, buttons: 0 }); });
	check(LockRequests() > requestsBeforeClick, "a click asks for the pointer lock again");
	TakeMotion(xrel, yrel);

	// The browser keeps from the page the Esc that lets the lock go. The game must get it all
	// the same, so that one Esc opens its pause menu, as natively, and not only frees the cursor.
	EM_ASM({ lock(true); lock(false); });
	check(TakeEscapes() == 0, "the Esc waits a moment for a blur after the lock goes");
	PassEscapeDelay();
	check(TakeEscapes() == 1, "a lock the browser takes while the page keeps the focus gives the game an Esc");
	PassEscapeDelay();
	check(TakeEscapes() == 0, "and only one");
	// Held on, the key repeats, and the browser passes the repeats and the release to the page.
	EM_ASM({ key('keydown', true); key('keydown', true); key('keyup'); });
	check(TakeEscapes() == 0, "the repeats of an Esc the browser kept are not another press");
	EM_ASM({ key('keydown'); key('keydown', true); key('keyup'); });
	check(TakeEscapes() == 1, "the next Esc is a press again");

	// The page loses the focus, and the lock with it, in either order: no Esc.
	EM_ASM({ lock(true); lock(false); window.dispatchEvent(new FocusEvent('blur')); });
	PassEscapeDelay();
	check(TakeEscapes() == 0, "a lock lost to another tab or app gives no Esc (the blur after the lock)");
	EM_ASM({ window.dispatchEvent(new FocusEvent('focus')); lock(true); window.dispatchEvent(new FocusEvent('blur')); lock(false); window.dispatchEvent(new FocusEvent('focus')); });
	PassEscapeDelay();
	check(TakeEscapes() == 0, "a lock lost to another tab or app gives no Esc (the blur before the lock)");
	EM_ASM({ lock(true); lock(false); visibility(true); });
	PassEscapeDelay();
	check(TakeEscapes() == 0, "a lock lost with the page hidden gives no Esc");
	EM_ASM({ visibility(false); });

	// An Esc the page did receive is the only one.
	EM_ASM({ lock(true); lock(false); key('keydown'); key('keyup'); });
	PassEscapeDelay();
	check(TakeEscapes() == 1, "an Esc that reaches the page is not doubled (the Esc after the lock)");
	EM_ASM({ lock(true); key('keydown'); lock(false); key('keyup'); });
	PassEscapeDelay();
	check(TakeEscapes() == 1, "an Esc that reaches the page is not doubled (the Esc before the lock)");

	// The game lets the lock go itself, as when its pause menu opens: no Esc, even when it takes
	// relative mode again before the lock's change arrives, as a picker closing does.
	EM_ASM({ lock(true); });
	check(SDL_SetWindowRelativeMouseMode(window, false), "the window leaves relative mode");
	EM_ASM({ lock(false); });
	PassEscapeDelay();
	check(TakeEscapes() == 0, "a lock the game lets go gives no Esc");
	check(SDL_SetWindowRelativeMouseMode(window, true), "the window goes into relative mode again");
	EM_ASM({ lock(true); });
	check(SDL_SetWindowRelativeMouseMode(window, false) && SDL_SetWindowRelativeMouseMode(window, true), "the window leaves relative mode and takes it again");
	EM_ASM({ lock(false); });
	PassEscapeDelay();
	check(TakeEscapes() == 0, "a lock the game lets go gives no Esc, though it wants the lock again");

	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}
