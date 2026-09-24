# How the browser port works

These are reference notes for whoever works on this next — human or agent. They
describe **how the systems work**, the invariants that hold them together, and
the traps that have already cost someone a day. They are not a changelog;
`notes/PORT-STATUS.md` is the chronological build log, and its early entries are
superseded by later ones.

## Orientation

Cortex Command compiled to WebAssembly with Emscripten. **This repository is
self-contained**: it holds its own complete copy of the engine in `engine/`
(sources, `Data`, vendored libraries) and needs nothing else to build or run
beyond the toolchain that `tools/setup-toolchains.sh` installs.

The **unmodified original game** it is compared against is *not* part of this
repository: it is a separate clone of the Cortex Command Community Project at
commit `20dfb3ea5`, by default in `../original-code` next to this repository
(`ORIGINAL_CODE` points elsewhere). Never edit it; `notes/original-game.md` says
how it is fetched, built and run. Only the comparison tools need it.

Every change the port makes to the engine is recorded in
`engine/PORT-CHANGES.patch`, a diff of `engine/` against that original, with
paths written as `original/…` and `port/…`. Regenerate it with
`tools/port-diff.sh` after changing the engine. Browser-specific code is either
`#ifdef __EMSCRIPTEN__` inside engine files or lives in `runtime/`.

```
README.md, LICENSE   what this is, how to build it; AGPL-3.0
build.sh             builds into build/wasm and dist/
CMakeLists.txt       the Emscripten build (ENGINE = engine)
engine/              the port's engine: Source/, Data/, external/, Resources/,
                     upstream's LICENSE and Licences/, PORT-CHANGES.patch
runtime/             browser platform layers (audio, filesystem)
site/                the page that loads the game (copied to dist/index.html)
vendor/              Lua 5.1.5, LuaBitOp, miniaudio
tests/               contracts and their browser pages
tools/               toolchain setup, the headless Chrome driver, comparison tools
notes/               this directory, and PORT-STATUS.md, the build log
toolchains/          Emscripten SDK and CMake (installed, not committed)
build/, dist/        build output (not committed); dist/ is the deployable app
../original-code/    the original game, untouched — outside this repository
```

The deployable set is `index.html`, `cortex.js`, `cortex.wasm`, `cortex.data` and
the `audio/` directory (every sound file and `manifest.tsv`, their list), served
with cross-origin isolation headers; the pthread and AudioWorklet glue are inlined
in `cortex.js`. Verified by serving only these from an empty directory under
`/cortex/` on a fresh origin: the main menu, and every sound file arrived. The four
files without `audio/` are not enough: the page offers a 63 MB download, and the
game then stops at "Could not load the list of sound files".

**Chrome is the only officially supported browser**, version 137 or newer: the
main loop suspends through WebAssembly JSPI (see [threads](threads.md)), and the
page says so instead of starting where it is missing. Firefox and Safari have
never been tried. Assume nothing about them.

### The adaptations, and where each lives

| platform dependency | what was done | note |
| --- | --- | --- |
| graphics | SDL3 + WebGL 2, GLSL 330 → ES 300 at runtime, indexed palette textures | [rendering](rendering.md) |
| display size | resolution always equals the browser window; the player picks only a scale | [display](display.md) |
| input | an extra event queue that survives render-only frames | [input](input.md) |
| audio | FMOD API reimplemented over miniaudio on an AudioWorklet thread | [audio](audio.md) |
| Lua | LuaJIT replaced by Lua 5.1.5 + LuaBitOp | [lua](lua.md) |
| files | `Data` preloaded without its sounds, which are fetched after the start; saves in IndexedDB via IDBFS with a write journal | [files and saves](files-and-saves.md) |
| threads | pthreads + JSPI + cooperative yields; the engine on the page's thread, or on a worker in the `--worker` build | [threads](threads.md) |
| networking | **none — online play is not supported.** The original's RakNet multiplayer (never built by upstream at this commit) and everything the port had added for it were removed on 2026-09-24 | — |

## Topics

- [Display size and scale](display.md) — why resolution follows the window,
  what may not change under a live Activity, and the traps found getting there.
- [Rendering](rendering.md) — the indexed/GPU hybrid, palette lookups, BigTexture
  tiling and GL state hygiene, player views, streamed frames, image dumps.
- [Terrain and destruction](terrain.md) — material versus colour layers, what
  bypasses the setters, scripts that write terrain.
- [Audio](audio.md) — the FMOD compatibility layer, DSP fidelity against native
  fixtures, why mixing moved off the main thread.
- [Music](music.md) — dynamic song sequencing and the unsolved authority handover.
- [Lua](lua.md) — the interpreter swap, the state pool, binding hazards.
- [Threads](threads.md) — pools, JSPI, cooperative yields, determinism under
  threading.
- [Input](input.md) — the event path and why the browser needs an extra queue.
- [Files and saves](files-and-saves.md) — asset packaging, IDBFS, the persistence
  journal, `.ccsave`.
- [Activities](activities.md) — start and restart lifecycle.
- [Editors](editors.md) — the Scene Editor and what it exposed about persistence.
- [Randomness](randomness.md) — reproducing the original's random streams; **read this before
  changing anything that draws a random number**.
- [Parity](parity.md) — how the port is compared with the untouched original, the
  probe scripts, what is identical and what cannot be; **read this before
  claiming anything is "the same as the original"**.
- [Testing](testing.md) — how to run and observe the game, and how the
  native/browser comparison works.

## Parity status

Details, numbers and method are in [parity](parity.md). In short, measured
against the untouched original (`original-code/`) with content-only probe
scripts:

- **Mission start is byte-identical** in all 17 scripted activities the game
  loads: every terrain material pixel and the master Lua random stream. This
  rests on the port reproducing libstdc++'s random distributions; see
  [randomness](randomness.md).
- **The picture is pixel-identical** where the simulation agrees: screenshots
  taken by the game itself at step 300 with the camera pinned differ in 0 pixels
  on seven screens (build-phase GUI and the Scene Editor's default scene
  included), and elsewhere only around actors that have moved.
- **Endings match**: destroying the brains, or every enemy, ends each mission
  the same way in both builds (a couple at a different step, where a script
  checks on real time).
- **Play drifts over time** in positions and velocities, from floating point:
  GCC fuses multiply-adds on arm64 and Apple's float maths library is not
  correctly rounded. Neither can be reproduced in WebAssembly. The original does
  not repeat itself exactly either, and its threaded play varies run to run —
  as the port's now does too.

**Performance** on Apple's GPU (headless Chrome's Metal backend): missions run at
60 fps with about 4 ms of engine work per frame at 1920×1080, and the title
screen is reached about 2.6 s after Play. The waits and GL queries that cost the
most are described in [threads](threads.md) and [rendering](rendering.md).

**The campaign** plays in the browser: a new Conquest, turns, a battle to a
natural defeat, saving, a page reload, loading, and a battle on the saved site
(see [activities](activities.md)). Its code is the original's, and its screens,
driven by the same scripted input in both builds, match the original's apart from
per-frame random glows and real-time animation (see [parity](parity.md)); so do
the menus and the tutorial's start.

Large areas remain unverified: most missions played to a natural finish, a whole
campaign, real mouse capture, performance on other GPUs and systems, and loading
over a real network (only a throttled local one has been measured: see
[files and saves](files-and-saves.md)).

## Conventions for these notes

One file per system, `notes/<topic>.md`. For each system record:

- what it is and what gameplay behaviour it controls;
- main source files, classes and Lua entry points;
- data flow, state ownership, and interactions with other systems;
- **invariants a future change must preserve**, and what breaks if it doesn't;
- traps with their concrete symptom, so the symptom is searchable;
- what has been verified, separating source inspection, automated contracts and
  live gameplay;
- what is not known.

Do not turn assumptions into facts, and distinguish browser-specific adaptation
from upstream behaviour. Keep detailed build transcripts in `notes/PORT-STATUS.md`
and link to them; keep these files describing the system as it is now. When
behaviour changes, **rewrite the explanation** rather than appending a dated
entry — that is what turned these notes into a log the first time.
