# Input

Updated 2026-09-24.

Entry points: `engine/Source/Main.cpp` (`PollSDLEvents`, `EndInputFrame`),
`engine/Source/System/BrowserInputEventQueue.h`,
`engine/Source/Managers/UInputMan.cpp`,
`engine/Source/GUI/GUIInputWrapper.cpp`; tests:
`tests/browser_input_queue_contract.cpp`, `gui_input_contract.cpp`.

## The path an input takes

```
browser event
  → SDL3 Emscripten backend
    → SDL_PollEvent                     (Main::PollSDLEvents)
      → BrowserInputEventQueue::Push    (browser only)
        → BrowserInputEventQueue::Pop   (browser only, rate-limited)
          → UInputMan::HandleInputEvent
            → GUIInputWrapper           (menus)
            → Controller / Activity     (gameplay)
              → EndInputFrame           (acknowledges consumption)
```

`UInputMan` turns raw events into held state, press/release transitions, mouse
and wheel movement, and analog controller values. Gameplay reads **transitions**,
not just held state, so an edge that is never observed is an input that never
happened.

## Why the browser needs an extra queue

This is the central browser-specific problem, and the fix is subtle enough to be
worth understanding before touching anything here.

`Main` polls SDL once per **outer rendering iteration**. But in activity mode,
`UInputMan` and `ActivityMan` update only inside `TimerMan`'s **fixed-step
simulation loop**, and an outer frame can execute **zero** simulation steps — the
accumulator simply had not reached a full step yet.

So: press and release can both arrive during render-only frames, and the
simulation never sees either. A long press survives because it spans a step; a
short tap vanishes. Natively this is rare because frames are short and steps
frequent; in the browser, where a frame can be long, it is common. The
user-visible symptom was short clicks in the Scene Editor highlighting a button
without activating it.

`BrowserInputEventQueue` (browser only; the native path is unchanged) fixes it:

- `Push` takes everything SDL has, unbounded and ordered.
- `Pop` delivers at most **one edge per (event class, device, button)** until
  consumption is acknowledged. `m_Delivered` is a `std::set` of
  `(downEventType, which, button)` tuples; a second edge for the same button
  returns `false` and stays queued.
- Non-button events (motion, wheel, text) are unaffected and delivered in order.
- `EndInputFrame` calls `Consumed()`, which clears `m_Delivered`.

The key detail: `Consumed()` is called from **`EndInputFrame`**, which `Main`
invokes at its three real consumption sites — menu transition, normal menu
update, and simulation update — not on every poll. Polling ten times without a
simulation step therefore cannot advance past a pending press. That ordering is
the whole mechanism; moving `Consumed()` back to the poll loop reintroduces the
bug exactly.

### Text payload lifetime

SDL3's `SDL_TextInputEvent` and `SDL_TextEditingEvent` carry `const char*`, not
inline arrays. In the bundled SDL3, `SDL_events.c` ties those payloads to
temporary event memory and `SDL_PumpEventsInternal` calls
`SDL_FreeTemporaryMemory` before the next platform pump.

Since the queue deliberately retains events across many pumps, copying the
`SDL_Event` struct alone leaves text pointing at freed storage. So `Push` copies
the bytes into an owned `std::string` per pending event, and `Pop` moves that
string into `m_DeliveredText` and repoints the outgoing event at it. It stays
valid until the **next successful `Pop`**, which is sufficient because
`PollSDLEvents` handles each event synchronously before asking for another.

No general lifetime extension is promised for other pointer-bearing SDL events
the engine does not consume. If a new event type with a pointer payload starts
being used, it needs the same treatment.

### Text input: the original's 32 bytes

`UInputMan::HandleInputEvent` keeps at most 32 bytes of any one `TEXT_INPUT`
event, exactly as the original does. Its `input <= 127` test passes every byte,
because `char` is signed on Apple arm64 and in WebAssembly alike, so UTF-8 comes
through as it does in the Mac original. Typing sends one character per event, so
only a long IME commit or a text event from a tool is cut. The port once kept whole
strings — the earlier test driver sent a whole string per event — and went back to
the original's loop to match it.

Field-level filtering still lives in `GUITextPanel::OnTextInput` — ordinary
fields accept printable ASCII; numeric fields and maximum lengths add more
restrictions. Preserving UTF-8 in `UInputMan` did **not** add Unicode font
rendering or IME support to the legacy GUI controls.

## Keyboard specifics

**Return and keypad Enter are one logical key.**
`GUIInputWrapper::UpdateKeyboardInput` used to call `ConvertKeyEvent` twice for
`GUIInput::Key_Enter` — once for Return, once for keypad Enter. Both calls wrote
the same repeat timer and output byte, so with only Return held the second call
saw keypad Enter up and overwrote the press with a release. `ConvertKeyEvent` now
takes an optional alternate physical key and evaluates both held states together:
one conversion, one logical key. Releasing one while the other is held does not
release the logical key, and pressing the second does not produce a duplicate
press.

The browser path reads `UInputMan`'s consumed snapshot; native reads SDL's
keyboard state directly. Both go through the same conversion.

**There is no generic keyboard navigation.** `GUIButton::OnKeyDown` is empty and
`GUIManager` routes keyboard events only to the focused panel — no Tab traversal.
Buttons therefore cannot be activated by Enter, and a failure to do so is *not* a
browser regression. GUI **text panels** do emit Enter notifications; the current
consumers are the numeric gameplay settings fields.

**Text shortcuts use Control, as in the original**, on macOS too: Command-A
does not select all in a text field there either. (The port briefly accepted
Command; that was undone to match.) One text-input event keeps at most 32 bytes,
as in the original; typing delivers one character per event.

## Keys and the browser's own actions

Every key the game receives is kept from the browser: Tab does not move the focus,
F5 does not reload, Ctrl+S does not save the page. SDL decides this in its DOM
handlers (`Emscripten_HandleKey` in `SDL_emscriptenevents.c`) by the handler's
return value: a key is kept unless SDL is taking text and the key types, in which
case its keydown stays with the browser so that the keypress happens, and SDL
turns that into text. Backspace, Tab, the arrows, the function keys and anything
with Ctrl are kept even then. The wheel over the canvas is kept too, so
Ctrl+wheel cannot zoom the page (which would change the game's resolution).

When the engine runs in a worker (`./build.sh --worker`, see
[threads](threads.md)), those handlers only forward each event to the worker,
after it has been dispatched, and Emscripten then keeps nothing.
`runtime/engine-in-worker.js`, included only in that build, makes the same
decisions on the page's thread: keydown, keyup and keypress on the window, the
wheel and touches on the canvas, and a mouse button released over it.
`Module.textInputActive` mirrors `SDL_TextInputActive`; `WindowMan::Present`
reports changes every frame, in both builds.

In fullscreen (the page's Fullscreen button, see [display](display.md)), Esc
belongs to Chrome, which leaves fullscreen before the page sees the key; the game
gets Esc again once the window is back.

Checked by sending keys to both builds and reading each event's
`defaultPrevented` after SDL's own window listener: F5, Tab, Backspace, a letter,
Ctrl+S, Space and Enter give the same result in both, at the main menu and with
the Lua console open, and the console receives the same text. F5 at the main menu
is the game's Quick Save, which says it is not allowed there in an `alert`, as the
original does in its message box.

## Mouse

Mouse position reaches the engine only through **motion events**, natively and in
the browser alike. A synthetic click with no preceding move is applied at the
previous position; the test driver's `click` always moves first. This is not a
port defect.

**Pointer lock is unverified.** SDL's `Emscripten_SetRelativeMouseMode` calls
`emscripten_request_pointerlock`, whose JS request is asynchronous. Automated
requests have returned Chromium `UnknownError` even in a minimal diagnostic.
Relative-mode aiming should be assumed unproven until someone confirms it by
hand with a real pointer.

## Focus loss

The bundled `SDL_emscriptenevents.c` calls `SDL_ResetKeyboard` on window blur
before clearing keyboard focus, and `WindowMan::DisplaySwitchOut` disables
keyboard and mouse motion. That is the intended release behaviour by source
inspection only — live focus-loss behaviour, mouse button release on blur,
pointer lock interaction and controller behaviour are all unchecked.

## Verification

`browser_input_queue_contract` passes, native and in Chromium: mouse down/up with
ten intervening render-only polls, a following click, keyboard down/up,
independent controller IDs, ordered non-button delivery, text-input ownership
across a blocked key release, and text-editing ownership and selection.

`gui_input_contract` (browser page `gui-input-check.html`) covers the GUI wrapper
(Return alone, keypad Enter alone, overlapping holds, final release) and text
(a long event cut at 32 bytes as in the original, concatenation of successive
events, `EndFrame` clearing, UTF-8 bytes, empty event) on the real `UInputMan`. These checks used to
live in a test that mostly exercised network input; they were kept when networking
was removed on 2026-09-24, along with `UInputMan`'s own network input path and the
original's dormant multiplayer mouse override in `GUIInput`.

Live: single clicks activate Game Editors → Scene Editor → Create New → Build
with no drag needed; Settings → Gameplay accepts Control-A, Backspace and Return
on the Crab Bomb threshold field; long `print(...)` commands typed character by
character reach the Lua console intact.

## Open issues

- **Pointer lock / relative mouse** — unverified, see above.
- **Physical gamepads** — `Base.rte/gamecontrollerdb.txt` is loaded at startup
  and the packet format carries analog axes, but no physical pad has been used.
- **Save-name prefix loss** — a saved name once appeared to be missing its first
  characters, and horizontal scrolling does not explain it. Four deliberate
  reproduction attempts all retained every character. Source trace: `GUIManager`
  processes mouse focus changes before dispatching text to the focused panel,
  `GUITextPanel` starts and stops SDL text input on focus change, and the
  Emscripten SDL backend checks `SDL_TextInputActive` before emitting text — so
  delayed focus activation is a plausible mechanism, but it is not established.
  **Do not record this as fixed.** A future attempt should capture
  text-input-active state and event order during a reproducible first-focus
  failure. Avoid enabling text input globally as a workaround without considering
  mobile keyboards and the other GUI managers.
  New evidence (2026-09-24): the observation was made with a driver that sent
  keys as text-only events and with a wrong macOS native key code (see
  [testing](testing.md), "Synthetic keys"). With realistic key events a full
  command including shifted characters reaches the console intact. That makes a
  harness cause likely, but the save-name case itself has not been re-run.
- Focus loss and prolonged simulation suspension deserve dedicated coverage.
