// Linked with --pre-js when the engine runs in a worker (CORTEX_WORKER).
//
// SDL registers its DOM event handlers on the page's thread, but for an engine in
// a worker those handlers only forward each event there, and the worker handles it
// after the event has been dispatched. Nothing then keeps the browser from acting
// on the event itself: Tab would move the focus, F5 reload, Ctrl+S save the page,
// Ctrl+wheel zoom it (and so change the game's resolution). On the page's thread
// SDL makes that decision in its handlers (SDL_emscriptenevents.c); this makes the
// same decisions here:
//   - a key is kept from the browser, except that while SDL is taking text, a
//     keydown that types stays with the browser so that its keypress, which SDL
//     turns into text, happens. Backspace, Tab, the arrows, the function keys and
//     anything with Ctrl are kept even then;
//   - a keypress while SDL is taking text;
//   - the wheel over the canvas, a mouse button released over it, and touches on it.
// Module.textInputActive mirrors SDL_TextInputActive; WindowMan::Present reports it.
if (typeof window !== 'undefined') {
  Module.engineInWorker = true;
  Module.textInputActive = false;

  const keptWhileTyping = /^(Backspace|Tab|Arrow(Left|Up|Right|Down)|F([1-9]|1[0-9]|2[0-4]))$/;
  window.addEventListener('keydown', (event) => {
    if (!event.code) return;
    if (Module.textInputActive && !event.ctrlKey && !keptWhileTyping.test(event.code)) return;
    event.preventDefault();
  }, true);
  window.addEventListener('keyup', (event) => {
    if (event.code) event.preventDefault();
  }, true);
  window.addEventListener('keypress', (event) => {
    if (Module.textInputActive) event.preventDefault();
  }, true);

  // The page's script defines Module, with its canvas, before this file loads.
  const canvas = Module.canvas;
  for (const type of ['wheel', 'touchstart', 'touchmove', 'touchend', 'touchcancel']) {
    canvas.addEventListener(type, (event) => event.preventDefault(), { passive: false });
  }
  document.addEventListener('mouseup', (event) => {
    const bounds = canvas.getBoundingClientRect();
    const inside = event.clientX >= bounds.left && event.clientX < bounds.right &&
      event.clientY >= bounds.top && event.clientY < bounds.bottom;
    if (event.button <= 2 && inside) event.preventDefault();
  });
}
