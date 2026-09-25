// SDL's browser backend with a window in relative mode, as in play, after the pointer lock
// was lost to another tab or app. When the page gets the focus back before the pointer is
// over the canvas again, SDL must stay in relative mode: the movement must still arrive,
// and a click must ask for the lock again. Before, the game's aim froze while its clicks
// still worked, until a menu turned relative mode off and on.
//
// The page's events are made here (EM_ASM), so the browser grants no real lock: the page
// stands in for it, counting the requests SDL makes and granting the user activation that
// Emscripten asks of a request.
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

	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}
