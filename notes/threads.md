# Threads and browser scheduling

Updated 2026-09-24.

Entry points: `engine/Source/Managers/ThreadMan.h` / `.cpp`,
`engine/Source/System/System.h` (`BrowserCooperativeYield`),
`engine/Source/System/ParallelAlgorithms.h`,
`engine/external/include/thread-pool-3.5.0/include/BS_thread_pool.hpp`
(`multi_future::wait`),
`engine/Source/Managers/WindowMan.cpp` (`BrowserWaitForFrame`),
`CMakeLists.txt` (`CORTEX_WORKER`), `runtime/engine-in-worker.js`, `serve.py`,
`tests/thread_wait.cpp`.

## The shape of the problem

The engine is a synchronous C++ program with a `while (!quit)` main loop that
blocks on vsync, plus thread pools for scripts, AI, pathfinding and rendering
work. A browser gives you neither: the page's main thread must return to the
event loop regularly or nothing paints, nothing responds, and — before audio
moved to a worklet — nothing plays.

Three separate mechanisms keep that working, and they solve different problems.
Confusing them is the main way to break this subsystem.

| mechanism | what it does | where |
| --- | --- | --- |
| **pthreads** | real worker threads via Web Workers + SharedArrayBuffer | `-pthread`, `ThreadMan` |
| **JSPI** | suspends the main thread's C++ stack and resumes it later, so a synchronous loop can yield to the event loop | `-sJSPI=1` (`CORTEX_JSPI`) |
| **cooperative yields** | explicit yield points inside long synchronous work | `BrowserCooperativeYield` |

## The engine's thread: the page's, or a worker

The engine runs on the page's main thread by default. `./build.sh --worker`
(CMake `CORTEX_WORKER`) builds it to run on a worker instead, into `dist-worker/`;
the rest of this note applies to both, with "the engine thread" meaning whichever
thread runs `main`. `main` marks it (`BrowserMarkEngineThread`), and
`BrowserIsEngineThread` is what decides who may yield and who may wait on the
pool, where the checks used to ask for the page's main thread.

How the worker build works:

- `-sPROXY_TO_PTHREAD` has `callMain` start `main` on a pthread (one of the
  prestarted workers), and `-sOFFSCREENCANVAS_SUPPORT` hands `#canvas` to that
  thread as an OffscreenCanvas, where SDL creates the WebGL context.
- JSPI works on a pthread: Emscripten calls a thread's entry point through a
  promising wrapper. `BrowserWaitForFrame` awaits the worker's own
  `requestAnimationFrame` (62 per second in a visible tab), and while the worker
  is suspended its event loop runs, which is when the canvas frame is committed
  and SDL's input events, forwarded from the page, reach it.
- What stays on the page's thread, and how the engine gets there:
  - **files.** Emscripten's JavaScript filesystem lives there, so every file call
    from any other thread is a synchronous round trip to it, about 13 µs each;
  - **Web Audio.** `window.AudioContext` exists only there. `FMOD::System::init`
    has the page's thread run `ma_engine_init` while the worker waits (see
    [audio](audio.md));
  - **page state and dialogs.** `Module` and `location` in a worker are the
    worker's own, so every read of a page flag (`?perf-debug` and the like) and
    every `alert`/`confirm` goes through `MAIN_THREAD_EM_ASM`. Three SDL calls did
    not (the system theme query, the message box's `alert`, `SDL_OpenURL`); the
    port's SDL now proxies them too;
  - **the browser's own action for a key or the wheel**, which SDL decides in
    handlers that, for a worker, only forward the event: see [input](input.md);
  - **quitting.** `BrowserReturnToStartScreen` only marks the return; the page
    writes the saves to IndexedDB in `onExit`, then reloads. Both builds do this.

Measured on the Metal instance, same origin and cleared storage:

| | page's thread | worker |
| --- | --- | --- |
| Tutorial, engine time per frame | 3.2–3.7 ms | 3.5–3.7 ms |
| worst frame in 2 s | 7–10 ms | 7–8 ms |
| renderer process CPU, 10 s of play | 6.0 s | 5.5 s |
| Play to main menu (runner, fresh profile) | 2.6 s | 3.5 s |
| serial simulation hashes | equal | equal |

Play is the same; the worker uses a little less CPU, because the page's thread
cannot block and spins where a worker sleeps. Startup is slower: loading the data
modules makes about 57,000 file calls, and each is a round trip. (It was 215,000
and 2.6 s slower until images were read whole: SDL_image asks ten decoders in
turn whether a file is theirs, each one seeks the stdio stream back and throws
away its buffer; see [files and saves](files-and-saves.md).) Checked in the worker
build: boot, both simulation hashes, typing into the console, the keys and the
wheel kept from the browser, window resizing, audio and the decoded-sound cache,
and quitting with saves persisted.

That startup cost is why the worker build is not the default. Removing it needs a
filesystem in WebAssembly memory, reachable from any thread without a round trip
(Emscripten's WasmFS), which would also move the 353 MB data package into the
WebAssembly heap and the saves from IDBFS to OPFS.

## Real threads

`-pthread` with `-sPTHREAD_POOL_SIZE=16` prestarts sixteen Web Workers at load so
the engine never has to create a worker mid-frame (worker creation needs the
event loop, which a blocked main thread cannot service — creating a thread from a
blocked main thread deadlocks).

Shared memory requires **cross-origin isolation**. `serve.py` sends
`Cross-Origin-Opener-Policy: same-origin` and
`Cross-Origin-Embedder-Policy: require-corp`. Without both, `SharedArrayBuffer`
is unavailable and the module will not start. Any host serving this build must
send those headers; that is a hard deployment requirement, not a nicety.

`-sALLOW_MEMORY_GROWTH=1` with `-sINITIAL_MEMORY=536870912` (512 MB) and
`-sMAXIMUM_MEMORY=4294967296` (4 GB, WebAssembly's limit). The cap was 2 GB until a
player's campaign battle ran out of memory uploading terrain (`std::bad_alloc` in
`BigTexture::Update`); the original, a 64-bit program, uses 1.5 GB at its title
screen and has no cap. Checked: with a mission running, Lua filled the heap to
2.6 GB in use (4 GB reserved) at 60 fps, and 1.9 GB of strings stored partly above
the 2 GB mark read back intact byte for byte. Nothing hand-written in JavaScript
here indexes the heap with signed arithmetic (miniaudio's ScriptProcessor path
does, but the build uses its AudioWorklet path). Growth with `-pthread` carries a
known performance warning from Emscripten; the initial size is large specifically
so growth is rare during play.

When the heap cannot grow, the game stops at once with "OOM" and the page says it
ran out of memory (`-sABORTING_MALLOC=1`). Before, malloc returned null, `operator
new` threw `std::bad_alloc`, and describing that exception for the report
(`EXCEPTION_STACK_TRACES` demangles its type name) needed memory too: each thread
that failed an allocation stopped inside the demangler (`RuntimeError: unreachable`
in `itanium_demangle::OutputBuffer::grow`), and the report named neither memory
nor what had used it. A player's report looked exactly like that, from pool
threads running `InitializeObjectScripts`. `ABORTING_MALLOC` acts only where the
heap fails to grow. Close to 4 GB, `sbrk` refuses a request that would pass the end
of the address space before trying to grow, so malloc still returns null; in a test
the engine thread then ended in "Terminate was called without an exception". So
`main` also installs a `new_handler` that stops the same way
(`BrowserStopOnFailedNew`), and so does the Lua allocator (see [Lua](lua.md)).
Whichever allocation fails last is often on another thread than the one that used
the memory up, so `runtime/heap-growth.js`, a pre-js, prints the stack the heap
grew from once it passes 2 GB and at every further 512 MB: the heap grows on the
thread whose allocation needs the room.

### When a pool task throws

Natively, an exception that leaves a thread's function calls `std::terminate`, and
the engine's terminate handler reports it. Under WebAssembly exceptions it did
not: it left the worker as an anonymous JavaScript exception ("Uncaught [object
WebAssembly.Exception]"), with nothing of what was thrown. The pool's worker loop
(`BS_thread_pool.hpp`) now catches it and calls `std::terminate` itself, so the
handler runs as it would natively. On a worker the handler writes no AbortSave or
AbortScreen and stops at a second failure (see [parity](parity.md)); the page
shows the first error lines it saw above the last ones. Checked with a task that
throws on the background pool: "Runtime Error due to unhandled exception on a
worker thread: …", the engine's message box, and a clean stop.

### The pools

`ThreadMan` owns two `BS::thread_pool`s:

| pool | native | browser | used for |
| --- | --- | --- | --- |
| priority | `hardware_concurrency()` | **4** | work that must finish this frame: script states, actor AI, sight rays, MOID rebuild, pathfinding |
| background | `hardware_concurrency() / 2` | **2** | work that may span frames: save writing, async loading |

The browser pools are **constructed** with their final sizes (`BS::thread_pool
m_PriorityThreadPool{4}` in the header) and `ThreadMan()` skips the `Clear()`
that the native path runs, because `reset()` joins the existing workers — on the
browser main thread, during startup, which stalls the page for no reason.

Four priority workers against sixteen prestarted pthreads leaves headroom for
SDL, the audio worklet and any transient thread. Do not raise the pool sizes
without measuring: the win from more workers is small and the cost of
oversubscribing a browser's worker budget is not.

## JSPI: the synchronous main loop

The engine's main loop presents a frame and waits. In the browser
`WindowMan::UploadFrame` calls `BrowserWaitForFrame`, which suspends the whole
C++ stack, returns to the JavaScript event loop, and resumes on the next
`requestAnimationFrame`. From C++'s point of view it is a blocking call; from the
browser's point of view the main thread went idle and came back.

The suspension uses JSPI (JavaScript Promise Integration, in Chrome since 137):
`main` is exported as a promise-returning function, and an `EM_ASYNC_JS` import
such as `BrowserWaitForFrame` suspends the WebAssembly stack in the engine until
its promise settles. The port first used Asyncify, which rewrites the program
instead: every function that might be on the stack during a suspension gets code
to unwind and rebuild it, and since the engine calls through function pointers
everywhere that meant nearly all of it — the module was twice the size (20.7 MB
against 10.7 MB). JSPI cannot suspend through JavaScript frames, so C++ exceptions
are native Wasm exceptions (`-fwasm-exceptions`); the JavaScript-based ones had
also routed every call inside a `try` block through a JavaScript `invoke_*`
trampoline, a third of the samples while loading. Measured on the Metal instance:
Play to title screen 6.3 → 2.6 s (data modules 4.4 → 1.2 s), Decision Day at
1920×1080 5.7 → 4.2 ms of engine time per frame. The serial harness logs are
identical line for line (Tutorial `c2b76d23260db076`, Dummy Assault
`1e215e18472c610a`), and audio plays. `-DCORTEX_JSPI=OFF` builds the Asyncify and
JavaScript-exception configuration again, for comparison.

`-sSTACK_SIZE=8388608` is sized for the engine's deep call stacks — scene loading
in particular goes a long way down.

**The frame's clock.** `TimerMan::Update` reads the clock once per frame and feeds
the difference into the simulation accumulator, which it caps using the measured
time per update. Natively that read happens just after a VSync-blocked swap
returns, so frames are a refresh interval apart. In the browser the frame resumes
a variable time after its refresh — up to about 2 ms later — and that jitter made
the accumulator fall just short now and then: one or two frames in every 120 ran
no update, the cap threw the time away, and the simulation ran at 59.0–59.5
updates per second (the overlay briefly showed "Sim Speed x0.91"). The wait keeps
the `requestAnimationFrame` timestamp, and `TimerMan::Update` counts the frame from
that refresh instead (`BrowserTakeFrameAgeMs`, once per wait; ordinary reads
otherwise, and always with VSync off). Decision Day now runs 60.0 updates per
second with no frame skipping one.

Four rules follow:

- **Only the engine thread may suspend**, and only inside `main`'s call stack. A
  yield from a pool worker has no promise-returning export under it and fails.
  Everything that yields must check `BrowserIsEngineThread()` first.
- **No JavaScript frame may sit between `main` and a suspension.** That is why
  exceptions are native; anything that can suspend must not be reached through a
  callback from JavaScript.
- **A suspended stack must not outlive the page.** V8 treats a suspended
  WebAssembly stack as a garbage collection root, and one still suspended when the
  page is reloaded or navigated away is never released: every reload in the same
  tab kept the whole previous page (its 350 MB of game files included), and after
  a few dozen the tab spent 85% of its time collecting garbage and ran at 47 fps.
  A heap snapshot showed the old page's context held by "(Stack roots) →
  WasmImportData". Asyncify had no such problem, its stack lives in linear memory.
  Now a `pagehide` handler (installed by `BrowserPageUnloading`) resumes whatever
  wait is pending; from then on `BrowserPageUnloading()` is true, nothing
  suspends, `Present` sets the quit flag, and `main` leaves through
  `emscripten_force_exit` before the orderly shutdown. `tools/reload-leak.mjs`
  measures it: after a forced collection the ArrayBuffer total stays at 348 MB over
  reloads at the menu, mid-mission and mid-load, where it grew by 347 MB each.
- **A suspended stack is a real suspension.** Anything that assumed it ran to
  completion without re-entrancy can now be re-entered. Presentation state in
  `UploadFrame` is arranged so the visible buffer is cleared immediately before
  the final blit and never before a wait; see [rendering](rendering.md).

## Cooperative yielding

`RTE::BrowserCooperativeYield()` (declared in `System.h`, defined in
`System.cpp`) suspends the C++ stack and resumes it from a
message posted to a `MessageChannel` — an ordinary task, so the page paints,
takes input, fires its timers and receives worker messages in between. It is:

- **throttled** by the time since the event loop last ran, and the per-frame wait
  in `UploadFrame` counts (`BrowserNoteEventLoopTurn`). It only yields after about
  a display frame (16 ms) without one, so during ordinary play — about 5 ms of
  work per frame — it never yields at all, and in a tight loading loop it costs
  almost nothing;
- **guarded** by `BrowserIsEngineThread()`, because the loaders it sits inside
  are reachable from pool threads;
- compiled to nothing natively.

**How it returns matters.** It used to call `emscripten_sleep(0)`, which resumes
through `setTimeout`. Chrome clamps a timer to 4 ms once timers nest five deep,
and a yield from inside a resumed timer callback always nests, so every yield
after the fifth left the page idle for 4 ms: a fifth of every data load. A posted
message has no clamp. `scheduler.yield()` resumes sooner still but outranks
ordinary tasks, and page timers then did not run once in a five-second load; a
worker waiting for the main thread would wait just as long. With the message the
title screen was reached in about 6 s on the Metal instance, against 7.9 s, while
the build still used Asyncify; since JSPI it is 2.6 s.

Yield points: the `Reader` line loop; between terrain layer loads and procedural
passes in `SLTerrain::LoadData`; around the placed-object loop and pathfinder
reset in `Scene`; around scene teardown in `SceneMan::LoadScene`; around image
decode and conversion in `ContentFile`; between the managers started in `main`,
between Lua states in `LuaMan::Initialize`, on every row of the transparency
tables `FrameMan` builds at startup (21 tables of 65,536 best-fit colours, most
of a second with no yield), and for every control the menus build from their
layout files; and the waits below.

Under `?perf-debug`, a yield that comes more than 250 ms after the event loop
last ran reports `Browser long block: N ms with no yield`. That report is how
the blocks were found; since the per-frame wait counts as an event loop turn, it
no longer misfires during play. The two-second frame report also counts the
mid-frame yields and the time they took. Startup has no block over 250 ms left.

## Waiting for pool work on the main thread

The engine waits for the priority pool several times every simulation update:
script and AI states (`parallelize_loop(...).wait()`), sight rays, the MOID
rebuild. In the browser that wait runs on the page's main thread, which cannot
block the way a native thread does.

`BS::multi_future::wait` (the port's copy of `BS_thread_pool.hpp`) and
`ParallelAlgorithms::ForEach` both wait with `wait_for(1 ms)` in a loop and call
`BrowserCooperativeYield()` between polls. The timed wait is a futex wait, which
on the main thread spins **and services calls the workers proxy to it** (stdout,
the filesystem), so a worker that needs the main thread is not stuck behind the
wait. The yield only returns to the event loop after a frame's worth of waiting.

This used to yield with `emscripten_sleep(0)` on every poll. Each of those went
through a clamped 4 ms timer, and a Dummy Assault update made about eight of
them: 15 ms of a 16.7 ms frame was reported as engine time while the main thread
sat idle for three quarters of it. Now the same mission at 1920×1080, scale 2, on
the Metal instance takes 4.6 ms of engine time per frame (simulation update 2.6 ms
on the overlay, was 12.3 ms), with no mid-frame yields.

### Spin-waits are the hazard

Three places busy-waited on engine state with no yield:
`Scene::BlockUntilAllPathingRequestsComplete`, `PathFinder::RecalculateAllCosts`,
and the brain-placement wait in `SceneEditorGUI`. Natively that burns a core
while workers finish. In the browser it **wedges the page** — the tab stopped
responding even to the DevTools protocol, which is how it was identified as a
blocked main thread rather than a slow loop.

The symptom was that calling `SceneMan:LoadScene` from the Lua console during a
running activity loaded one scene and hung on the next. All three now yield.

> **Treat any `while (...) {}` on engine state as a browser hazard and give it a
> `BrowserCooperativeYield()`.** This is the single most reliable way to break
> the port.

## Parallel algorithms

`ParallelAlgorithms::ForEach` wraps what upstream writes as
`std::for_each(std::execution::par_unseq, ...)`. Emscripten's libc++ has no
parallel execution policies, so the browser branch dispatches to the priority
pool and waits per-future as described above.

It falls back to a plain serial `std::for_each` when any of these hold:

- the iterator is not random-access;
- fewer than 64 elements (dispatch would cost more than the work);
- not on the engine thread (a pool worker must not queue onto its own pool);
- `Module['serialLoops'] === true` (`?serial-loops`), an escape hatch for
  bisecting behaviour against the serial path;
- the phase mask says this run must be serial (see below).

This mattered: before it, scene loading ran serially in the browser while native
spread it over every core — 1735 ms against 311 ms for a scene load.

## Determinism under threading

Threaded play is **not** reproducible, in the original or the port, and the port
keeps it that way on purpose. C++ random draws made from script threads and from
the parallel sight-ray loop all hit the one global generator, and objects created
on several threads join `m_AddedActors` / `m_AddedItems` / `m_AddedParticles` in
lock order. Both depend on scheduling. The port briefly removed both effects
(per-state generators, sorting new objects by ID) and put them back, because each
also changed single-threaded results. See [randomness](randomness.md).

`System::PhaseRunsInParallel(phase)` lets `-simulate-tutorial <steps> <mask>`
serialise individual phases (`ParallelMOScripts`, `ParallelAIScripts`,
`ParallelSightRays`, `ParallelMOIDTask`, `ParallelAlgorithms`, `ParallelLuaGC`,
`ParallelPathRequests`). The last computes `PathFinder::CalculatePathAsync`
requests in place: on the background pool a path reaches its requester (a Lua
callback or an actor's move path) at whatever step the pool finishes, which is
what kept Dummy Assault from repeating itself even with every other phase
serial.
Serialising every phase (`0`) makes a run exactly reproducible, which is what
tests use. That bisection is also how the AI phase was identified as the one
whose script-driven draws vary.

**When adding a parallel phase, ask what it shares.** Shared generators, shared
append lists and anything keyed on pointer identity are the three things that
have gone wrong so far.

## Verification and gaps

`thread_wait` (`./build.sh --target thread_wait`, then `/thread-wait-check.html`
on the development server) runs the real `multi_future::wait` and
`BrowserCooperativeYield` in the browser. It checks that twenty short waits never
reach the event loop (by any route: a pending timer and a pending message must not
fire; the old wait took 95 ms for them and failed, the current one takes 2 ms),
that an 80 ms wait lets page timers run, that stdout written by workers is
serviced during a wait within about a millisecond, and the empty, deferred,
exception and worker-side cases. `-simulate-tutorial` proves the simulation is
scheduling-independent across 3600 steps; its serial hashes did not change with
any of the waiting changes above.

Not established: behaviour when the browser throttles a backgrounded tab's
workers; and recovery if a worker dies.
