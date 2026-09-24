# Running and observing the browser game

Updated 2026-09-24. Entry points: `tools/run-checks.mjs`, `tests/check-report.js`,
`tests/golden/`, `.github/workflows/ci.yml`, `tools/browser_driver.mjs`,
`tools/cc.sh`, `serve.py`, `site/index.html`.

## The checks: one command

```sh
./build.sh --target checks          # the game and every test program
node tools/run-checks.mjs           # --list, --only a,b, --angle metal, --chrome PATH, --log
```

`run-checks.mjs` serves `dist/` itself with the cross-origin isolation headers,
starts its own headless Chrome with a fresh temporary profile at a fixed
1280×720 window, and runs nineteen checks in about fifty seconds:

- **test pages** — each Emscripten test program's page loads
  `tests/check-report.js`, which shows the output and turns the program's exit
  status (0 passes; non-zero, an abort or an uncaught error fails) into
  `window.checkResult`. `audio-check` needs a click, which the runner makes;
  `worklet-probe.html?auto` starts the raw device and `ma_engine` itself and checks
  the sine's peak (0.25) and that the graph advances;
- **golden outputs** — `random_contract` and `float_contract` run under Node and
  must print exactly `tests/golden/`. The random contract's golden is also what the
  original's own generator (libstdc++, built with g++-13) prints;
- **the game** — its start screen fetches none of it until asked and offers
  "Download game" with a size and shows the strip under the game, two links and
  Fullscreen, with the game's area 50 px shorter than the window (`start-screen`),
  a browser without JSPI is told so
  and fetches nothing (`start-screen-no-jspi`), it boots to the main menu, every
  sound file it fetches after the start arrives (`sound-files`), and the serial
  simulation harness reproduces its recorded result lines, state hash, object
  count and random stream (the random-draw count depends on the window size, hence
  the fixed window).

It exits non-zero if anything fails, printing the failing check's last log lines.
`--log` prints every check's output, each line stamped with the seconds since the
page (for the game, since Play) started; test pages write their output to the
console for this. The game checks press the start screen's buttons as a player
would ("Download game" in the fresh profile, then "Play") and report how long after
opening the page the game started and how long it then took to reach the result. `--dist DIR` runs against another build, for
instance `--dist dist-worker` after `./build.sh --worker --target checks`.
The pages that need a person (`input-check`, `pointer-lock-check`, and
`editor-storage-check`, which inspects what a manual editor save left behind) are
not run.

The same runs in CI on every push (`.github/workflows/ci.yml`: Ubuntu, the
toolchain cached, Chrome headless on SwiftShader, a PulseAudio null sink as the
audio device). It has not run yet: it needs the repository on GitHub.

## Release and debug builds

`./build.sh` builds the release configuration: no Emscripten runtime assertions,
but C++ function names kept (`--profiling-funcs`) so crash reports and profiles
stay readable. `./build.sh --debug` adds `-sASSERTIONS=1` and builds into
`build/wasm-debug` and `dist-debug/`. Measured on the Metal instance: the title
screen is reached in 1.9 s against 2.6 s with assertions, `cortex.js` is 1.25 MB
against 1.81 MB, and the serial simulation benchmark below is about 2% faster.

**The simulation benchmark.** The harness prints `Simulation time: N steps in X
ms` for its serial run, which does identical work every time, so it measures
compiler and code changes to within about ±2%, where in-game frame time varies by
±0.5 ms between runs of the same build (each battle plays out differently). Use
Dummy Assault, 1200 steps, `parallel=0`, and compare builds from the same origin:
the state depends on saved settings (AI sight range follows the screen width).

`./build.sh --worker` builds the engine to run on a worker thread instead of the
page's main thread, into `build/wasm-worker` and `dist-worker/` (see
[threads](threads.md) for what it changes and what it costs). Each variant is a
full build of its own.

`-DCORTEX_SIMD=ON` compiles with `-msimd128`. It changed nothing measurable
(2.56 against 2.58 ms per step) and left the state hash identical, so it is off;
the software sprite drawing it might speed up is not in that benchmark.

## Chrome only

Chrome is the only officially supported browser, and the only one this build has
ever run in. Firefox and Safari are untried; cross-origin isolation,
`SharedArrayBuffer`, AudioWorklet with pthreads and IDBFS quota behaviour are all
places they could differ. Do not assume anything here transfers.

Everything below also runs under **software WebGL** (SwiftShader) in headless
Chrome, so every draw timing is an upper bound of unknown looseness. No run on a
real GPU has been done.

## Why a separate browser is needed

The engine keeps its synchronous main loop (suspended through JSPI) and yields once per frame inside `WindowMan::UploadFrame` through `BrowserWaitForFrame`, which awaits `requestAnimationFrame` when VSync is enabled. A document that is not being composited receives no animation frames, so the loop stops between yields. Any host that only paints the page while a screenshot is being taken therefore advances the engine a few frames per minute, and every observation made through it — frame rate, input latency, animation progress — measures the host rather than the game.

Measured example: in such a host the engine reported 3.04 ms of work per frame while only four frames completed in 77 seconds. The same build in a continuously composited Chrome ran 33 frames per second. Do not diagnose engine performance or input handling from a host that composites on demand.

## The driver

`tools/browser_driver.mjs` (wrapper `tools/cc.sh`) launches a headless Chrome with remote debugging and drives the page over the DevTools protocol. Chrome stays running between invocations; each command attaches to the existing page, so a loaded game survives across commands. Commands: `launch`, `kill`, `open <url>`, `url`, `eval <js>`, `play [seconds]`, `log [count] [regex]`, `shot <path> [x y w h scale]`, `move`, `click`, `drag`, `key`, `type`, `wait`, `batch "<cmd> …" …`. `play` presses the start screen's button as a player would until the game runs: "Download game" first in a browser that does not keep the game yet, then "Play".

Required Chrome flags are in `launch()`. Cross-origin isolation comes from `serve.py`, and `crossOriginIsolated`, `SharedArrayBuffer` and WebGL 2 are all available in this configuration. WebGL runs on SwiftShader by default, so draw cost is far higher than on a real GPU; treat draw timings from this environment as an upper bound, not as the browser port's true cost.

Three environment variables select another browser instance:
`CORTEX_CDP_PORT` (default 9222), `CORTEX_CDP_PROFILE` (default
`/tmp/cortex-chrome-profile`; a second instance needs its own) and `CORTEX_ANGLE`
(default `swiftshader`). **`CORTEX_ANGLE=metal` renders on the Mac's GPU** —
headless Chrome reports "ANGLE Metal Renderer: Apple M2 Pro" and works with the
screen locked — which is what a real player's Chrome does. Use it for any pixel
comparison with the native game: SwiftShader rounds nearest-texel ties at scaled
layer seams the other way from the GPU, so its screenshots of Bunker Breach show
thin grid lines across the sky (803 differing pixels) that vanish under Metal
(120, all real-time blink and physics drift). A fresh profile has no game
settings until the game has started once in it.

```sh
export CORTEX_CDP_PORT=9223 CORTEX_CDP_PROFILE=/tmp/cortex-chrome-metal CORTEX_ANGLE=metal
./tools/ccw.sh 60 batch "open http://127.0.0.1:8084/index.html" "wait 3000"
```

Three details matter and were each the cause of a wasted investigation:

- Only the front tab is visible. Chrome sometimes opens its own tabs, which pushes the game tab into the background and stops its animation frames. The driver closes other page targets and brings the game tab to the front on every connect.
- Pending protocol requests must not hold the Node event loop open, or every invocation waits out its full timeout before exiting.
- Do not replace `window.requestAnimationFrame` to pace frames. Emscripten's canvas setup depends on the real one; a `setTimeout`-based wrapper leaves the canvas at 0x0 and the engine stalls inside its first `UploadFrame`.

Do not read unexported Emscripten runtime properties (`Module.PThread` and similar) from injected script. The generated stub calls `abort()`, which kills the running game.

## When the page stops answering

`tools/ccw.sh <seconds> <cc.sh args…>` is `cc.sh` with a deadline. Use it for
anything that might hit a blocked page: a page whose main thread is blocked
outside JavaScript never answers DevTools, and plain `cc.sh` then waits forever.
Two driver commands help diagnose it: `stack` pauses the page and prints the call
stack (wasm frames are named, since the build keeps `-g2`), and `resize W H` /
`resize reset` changes the viewport the way resizing the window would.

A page that answers nothing, cannot be paused and uses no CPU is usually showing a
dialog: the game opens `alert` for SDL message boxes (F5, Quick Save, says it is not
allowed on the main menu) and `confirm` for engine assertions (OK aborts, Cancel
ignores). A DevTools session that attached after the dialog opened cannot see it
("No dialog is showing"), so a script should enable the Page domain before the game
starts and answer `Page.javascriptDialogOpening`; `run-checks.mjs` logs each dialog as
a `DIALOG` line and dismisses it.

If even `stack` cannot pause the page, it is not spinning in wasm. Check for a
JavaScript dialog first — SDL's message box is an `alert()`, and headless Chrome
blocks on it — with `Page.handleJavaScriptDialog` from a browser-side session,
which does not need the blocked renderer. Then sample the renderer at the OS level
(`sample <pid>`); a leaf in `kevent64` at 0% CPU is a wait, not a loop. If a stray
driver process is still attached, kill it before starting another: two drivers on
one Chrome produce nonsense.

## Driving the original's UI

With the screen locked the native game receives no input, so `tools/native-input`
injects it: `run-original.sh <script> <out> [Key=Value …]` runs the untouched
original with `libinject.dylib` (built by `build.sh` against Homebrew's SDL3, which
the original links) and collects its F12 ScreenDumps; `run-browser.sh` plays the
same script into the port on the driver's Chrome (use the Metal instance) and
collects the port's. Scripts live in `tools/native-input/scripts/`; the format
is at the top of `inject.c`. Coordinates are window points at 864×558 ×1.5, i.e.
game pixels ×1.5. Waits are wall-clock from the first event poll (native) or the
Play click (browser); the first wait must outlast both loads. The original runs
with VSync off (a locked screen never delivers the refresh a VSync'd swap waits
for), so the injector paces its swaps to 60 a second (`CORTEX_INPUT_FPS`, default
60, 0 for uncapped); uncapped, frame-rate-dependent behaviour such as the camera's
resting point differs. Typed text goes one character per event, as real typing
does; the original keeps only 32 bytes of any one text event. The in-game Lua
console (`key \``, `text …`, `key Return`) prints engine state in both builds,
which is how a difference is traced to its cause. See
[parity](parity.md) for what has been compared this way.

## In-engine diagnostics

`?perf-debug` sets `Module.perfDiagnostics`. This enables the on-screen performance overlay from startup (the hotkey is otherwise unreachable before a scene is running) and prints a line every two seconds separating wall-clock frame rate from engine time per frame, with the number of mid-frame returns to the event loop and the time they took, the worst frame's engine time and interval, how many intervals exceeded 25 ms (visible hitches), and simulation updates per second with the frames that ran none or several (60.0 with none is real-time speed). Separating the two is what distinguishes a slow engine from a throttled presentation surface. The line ends with how long printing the previous one took, and that time is left out of the next frame: with DevTools attached, printing it took 19–24 ms, which had shown as one "worst frame" of about 24 ms in every report. `?thread-debug` and `?graphics-debug` remain as before.

"Engine time" is everything between two frame waits, including any time the engine
spent suspended mid-frame. When it is high, profile before optimising: `cc.sh
profile <ms> [out.cpuprofile] [rows]` samples the page's main thread with V8's
profiler and lists the functions with the most time of their own (wasm frames are
named); `(idle)` is the event loop. A mission once reported 15 ms per frame while
the profile showed the main thread idle three quarters of the time — it was
waiting on clamped timers, not computing (see [threads](threads.md)). The saved
profile opens in Chrome's DevTools. `?gl-check` checks for a GL error after every
GL call, as the native build does, to locate one (see [rendering](rendering.md)).

`?perf-debug` also prints an audio report every two seconds: the engine's clock,
the device state, how many frames the mixer produced and their peak, the active
channels with each one's playback cursor, and the peaks either side of the
per-channel fader. Those three levels are what distinguish a mixer that never
runs from one that runs and produces zero from one whose output never reaches the
speakers — see [audio](audio.md), where that distinction was the whole diagnosis.

`cc.sh audio` reads the page side: peak, non-silent block ratio, and (only on a
ScriptProcessorNode build) callback count and worst lateness. Audio mixes on an
AudioWorklet thread now, so there is no main-thread callback to time; judge
continuity from the non-silent ratio and the engine clock instead.

Memory: the `?perf-debug` line ends with the WebAssembly heap's size and how much
of it is allocated (flat at about 800 MB through six minutes of Decision Day).
`tools/reload-leak.mjs <port> <url> [loads] [play]` reloads the page in one
tab and prints V8's heap after a forced full collection; the ArrayBuffer total
must stay flat. `performance.memory` is no use for this: it counts garbage not
yet collected. If a long-lived driver tab suddenly runs slowly with the profile
full of `(garbage collector)`, measure with this before suspecting the game, and
restart Chrome (`cc.sh kill`, `cc.sh launch`) to get a clean baseline.

The overlay's own numbers come from `PerformanceMan`. `RAlt+P` toggles it and `RAlt+Alt+P` toggles the counter graphs; the driver sends right Alt as `key AltRight+p`.

## Input

Mouse position reaches the engine only through motion events, on browser and native alike, so a synthetic click without a preceding move is applied at the previous position. The driver's `click` always moves first. This is not a port defect.

Short clicks and key taps survive frames that run no simulation update; see [input](input.md) for the queue that preserves those edges.

**Synthetic keys must look like a real keyboard, and must not carry a native code.**
`Input.dispatchKeyEvent`'s `nativeVirtualKeyCode` is the *platform's* key code.
The driver once passed the Windows virtual-key number there; on macOS 16 (Shift
on Windows) is `kVK_ANSI_Y`, so after any Shift press Chrome believed a Y key was
held and auto-repeated it — ~3,000 trusted `keydown` events a second (`key:
"Unidentified"`, `keyCode 89`) that swamped the page until it stopped answering
the debugger. It looked exactly like a game hang triggered by Shift in the
console, on SwiftShader and Metal alike. The driver now omits the native code,
and `type` sends each character with its physical `code`, virtual key, text and a
held Shift where a US layout needs one. Checked on Metal: `print("Typed 12345 OK")`
typed into the console arrives whole and runs. The earlier "save name loses its
first characters" observation predates this fix and most likely had the same
cause.

To find a page flood like that: a page-level `keydown` listener counting
`e.code` (installed with `eval`) shows it immediately; a browser-level trace with
`disabled-by-default-v8.cpu_profiler` works even on a page that no longer answers,
provided it is started *before* the page freezes.

## Assertion dialogs

The original stops on a modal "RTE Assert!" box with Abort / Ignore / Ignore All.
SDL has no multi-button message box in the browser — `SDL_ShowMessageBox`
returns without showing anything — so the port used to ignore every assertion
silently. It now asks with `confirm()` (OK aborts, Cancel ignores), which blocks
the page as the native box blocks the game. A page stuck on one answers no
`eval`; attach with `Page.enable` and read `Page.javascriptDialogOpening`, or
dismiss it with `Page.handleJavaScriptDialog`. Headless runs of the *original*
that hang are usually the same thing: `sample` the process and look for
`RTEError::ShowAssertMessageBox` under `SDL_ShowMessageBox`. The scene-dump and
module-validation modes never prompt.

## Quitting reloads the page

Escape on the main menu screen is Quit when no activity is running
(`MainMenuGUI::HandleBackNavigation` to `ShowQuitScreenOrQuit`), the same as
native, and so are Exit and Conquest's Quit Program. In the browser all of them
return to the page's start screen: the loop stops, saves are flushed to
IndexedDB, and `index.html` reloads the page, whose start screen offers Play. A driver
that presses Escape once too often therefore finds itself back at Play, not in a
dead page.

`tools/boot.sh` opens the page, starts the game with `cc.sh play` and waits for the main menu. It never presses Escape: on the main menu that quits.

## Comparing against the untouched original

`tools/parity-probe/sweep.sh [out-dir]` is the real comparison: it runs the
unmodified `original-code` game and the browser port into each scripted mission
and compares what they wrote. `run-original.sh` and `run-port.sh` do one side for
one mission and can be used alone.

How it avoids touching the original's code:

- **`ParityProbe.rte`** is a content mod with one global script. `StartScript`
  runs inside `GAScripted::Start`, after `Activity::Start` has reseeded and loaded
  the Scene and before any simulation step. It reads every pixel with
  `SceneMan:GetTerrMatter`, appends 32 `SelectRand` and 8 `PosRand` draws from the
  master Lua state, and writes `Userdata/ParityProbe.bin` with `io.open` — the
  original loads Lua's `io` and `os` libraries. `if jit then os.exit(0) end` quits
  only the native game (LuaJIT); the browser keeps running so IDBFS can persist.
- **Settings** do the rest (`settings.py`): `LaunchIntoActivity = 1`, the mission
  and Scene as `DefaultActivityName` / `DefaultSceneName`, `SkipIntro = 1`,
  `EnableGlobalScript = ParityProbe.rte/Parity Probe` — the key is **module and
  preset name**, not preset name alone — and for the native game `EnableVSync = 0`,
  which is what lets it run with no awake display.

Details that each cost a run: mods must declare `SupportedGameVersion = 7.0.0` or
the original asserts while loading; global scripts only run under **GAScripted**
activities, so the Tutorial (a plain `GameActivity`) cannot be probed this way;
and `LoadScene()` with nothing queued always ends up on `DefaultSceneName`, so the
setting chooses the Scene as well. Both scripts restore everything they change —
the reference's `Settings.ini` and `Mods/`, and the browser's stored settings and
`/Mods` — even when interrupted.

A macOS "reopen windows?" alert from crash history can still block the native game
on launch; `defaults write org.cortex-command-community.cccp ApplePersistenceIgnoreState -bool YES`
clears it once.

## Native reference

> **Where this runs has changed.** `original-code/` is now the untouched original
> game and has none of the port's harness flags — run with
> `-dump-scene-preview` it simply starts normally. The flags below exist in the
> port's engine, `engine/`, which can also be built natively with its own
> `meson.build`. A native build of `engine` versus the browser build proves the
> browser reproduces the port; comparing against the original needs the original
> driven through its own UI. See the parity section of [README](README.md).

The port's engine accepts `-dump-scene-preview "<Scene Name>"`, which loads that scene
after the data modules, writes its preview into the screenshot directory and
exits. Both builds run the same code for it, so the two files can be diffed with
`tools/compare_png.py`. This is how native/browser rendering parity is
settled:

```sh
'./Cortex Command.app/Contents/MacOS/CortexCommand' -dump-scene-preview 'Tutorial Bunker'
```

The file lands in `ScreenShots/` under the working directory of whichever build ran it. The browser runs the same thing
through `?dump-scene=<Scene Name>`, which passes the argument to `callMain`; pull
the result out of the persisted filesystem with the driver:

```sh
./tools/cc.sh getfile /ScreenShots /ScreenShots/<file>.png /tmp/browser.png
```

Rebuild **both** before comparing. They share the random stream, and a stale
binary on either side compares two different games rather than two builds of one.

### What used to block it, and what each turned out to be

The run appeared to fail "because the screen is locked". It was four separate
faults in a row, each found by `sample`-ing the stalled process:

1. **A modal macOS alert.** `NSPersistentUIRestorer promptToIgnorePersistentStateWithCrashHistory:`
   puts up a "reopen windows?" dialog after repeated crashes, and SDL's event
   pump sits inside it forever. Cleared with
   `defaults write org.cortex-command-community.cccp ApplePersistenceIgnoreState -bool YES`.
2. **The vsync wait in `Cocoa_GL_SwapWindow`.** Presenting a frame waits on the
   display's refresh callback, which never arrives while no display is awake.
   `WindowMan` now routes every swap through `PresentWindow`, which skips
   presentation in dump mode — a run that only writes a PNG has nothing to show.
3. **Arguments parsed too late.** `HandleMainArgs` runs *after* `InitializeManagers`,
   so dump mode was not set while the window was being created. `DetectSceneDumpMode`
   now scans `argv` for the flag before the managers start.
4. **An assertion dialog.** See the Activity section below.

`System::IsInSceneDumpMode()` gates all of it, so an ordinary run is untouched.
`RTEError` also refuses to open any message box when nothing can dismiss one
(dump mode or external module validation): asserts abort with the message on
stderr instead of waiting on a dialog that no one will ever click.

### Loading a Scene with no Activity

`SpatialPartitionGrid::Reset` and `::Add` asserted that an Activity was running.
Loading a Scene outside one — which the preview dump does deliberately, and which
`SceneMan:LoadScene` from the console also does — tripped that assert, and the
resulting modal box is what made those console calls look like an unexplained
hang. Both now treat "no Activity" as "no team is playing": `Add` registers only
in the team-agnostic cells, which is where every lookup falls back to anyway, and
`Reset` clears every team's used cells rather than leaving a previous Activity's
stale MOIDs behind. `FrameMan::SaveBitmap` likewise no longer refuses a world or
scene dump just because no Activity is running.

### Two resolutions

`-dump-scene-preview` writes the 170x80 preview. `-dump-scene-world` writes the
whole terrain at scene resolution, which is the same content without the roughly
twelvefold downscale and is far more sensitive: 13.2 million pixels for a large
scene against 13,600. The browser takes `?dump-scene=<Name>` and
`?dump-world=<Name>` respectively.

Scenes load by **preset name**, not by file name. All but one of the mission
scenes match, but `Doainar.ini` defines a Scene whose `PresetName` is
`First Signs`, and passing the file name simply fails to load.

### Simulating, not just loading

`-simulate-tutorial <steps>` (browser: `?simulate=<steps>`) starts the real
Tutorial mission, runs that many simulation steps with a fixed time step and no
input or rendering, and prints the object count, a hash of every object's
identity, position, velocity, rotation and mass in MOID order, and the random
stream. It also writes the terrain afterwards, so weapon damage can be compared.

By default it simulates the way the game actually runs, on the thread pool. An
optional second argument is a bitmask of which parallel phases to keep parallel,
so a phase can be serialised one at a time; `0` serialises everything. That
bisection is what found the one phase that used to stop a run reproducing itself —
a data race on the global random generator, now fixed. See
[randomness](randomness.md).

`-simulate-activity "<Activity>|<Scene>" [steps] [mask]` (browser:
`?simulate-activity=<Activity>|<Scene>&simulate=<steps>&parallel=<mask>`, with
the `|` and spaces URL-encoded) runs the same harness on any mission. The Scene
must be named: an Activity preset's `SceneName` does not survive into the stored
preset, and some are not Scenes at all; `tools/parity-probe/missions.txt`
lists pairs that work. Use `parallel=0` for a reproducible run: Dummy Assault,
300 steps, gives state hash `1e215e18472c610a`, 191 objects and 47,133 draws on
every run (before path requests could be serialised, the random stream matched
but the state did not).

The harness drains both thread pools before hashing, because `GetMOIDCount` reads
an index that a pool thread rebuilds. Without that it reports 84, 38 and 4 objects
for identical simulations, which looks exactly like nondeterminism and is not.

### Result

Native and browser output is **byte identical**, including the PNG file sizes.

- Previews: all **47** mission scenes, 0 of 13,600 pixels differing on every one.
- Full terrain: 11 scenes up to 5040x2620, **81,396,616 pixels**, none differing.
- The reported random draw counts and stream hashes match exactly on every scene.
- Simulation: 600 and 3600 steps of the Tutorial mission — a minute of play at
  60 Hz with actors, AI, weapons fire, physics, collisions and Lua — gave the same
  object count, state hash and random stream on both builds, and the terrain left
  behind was byte identical at 2110x840.

That simulation result was measured while the port still gave script states
private generators, which made threaded runs reproducible. Those were removed
(they changed which values the simulation drew — see [randomness](randomness.md)),
so threaded runs now vary as the original's do, and a comparison must use the
serial mode: `?simulate=600&parallel=0` gives state hash `c2b76d23260db076`, 82
objects, 34,095 draws on every run.

After replacing the binary inside the bundle, follow `notes/original-game.md` exactly — the
fresh binary needs `install_name_tool -add_rpath @executable_path/../Frameworks`
before signing, or it cannot find `libfmod.dylib` and dies at launch.
