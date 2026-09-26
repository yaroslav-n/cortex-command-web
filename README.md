# Cortex Command in the browser

A port of the [Cortex Command Community Project](https://github.com/cortex-command-community/Cortex-Command-Community-Project)
to WebAssembly: the original C++ engine, compiled with Emscripten, running in
Chrome with its game data, Lua scripts, audio, saves and editors.

**Play it at https://yaroslav.au/cortex-command/**, deployed from `main` whenever every
check passes ([hosting](notes/hosting.md)).

It is built from upstream commit `20dfb3ea5`, with the engine's platform layers
adapted for the browser:

| original                    | in the browser                                                                                   |
| --------------------------- | ------------------------------------------------------------------------------------------------ |
| SDL3 + desktop OpenGL       | SDL3's Emscripten backend + WebGL 2 (shaders translated to GLSL ES at load)                      |
| FMOD                        | the FMOD API reimplemented over miniaudio, mixing on an AudioWorklet thread                      |
| LuaJIT                      | Lua 5.1.5 interpreter with LuaBitOp                                                              |
| files on disk               | game data preloaded into the browser filesystem, sounds fetched after the start; saves, settings and mods persisted in IndexedDB |
| threads, blocking main loop | pthreads on Web Workers; the main loop suspends with WebAssembly JSPI                            |
| resolution settings         | the game fills the window above a strip with links; Ctrl+F gives it the whole screen; only the scale is chosen |
| online multiplayer (RakNet) | not supported                                                                                    |

How each of these works, what was compared against the original and how, and
where the port deliberately differs, is written up in [`notes/`](notes/README.md).

## Requirements

- **Chrome 137 or newer** (JSPI). Other browsers are untested.
- The page must be served with cross-origin isolation headers
  (`Cross-Origin-Opener-Policy: same-origin`,
  `Cross-Origin-Embedder-Policy: require-corp`); `serve.py` does this.
- Building needs macOS or Linux with `git` and Python 3.

## Build and run

```sh
tools/setup-toolchains.sh        # once: Emscripten 4.0.15 and CMake into toolchains/
./build.sh --target cortex       # the game, into dist/
python3 serve.py --port 8080     # then open http://127.0.0.1:8080/
```

`dist/` then holds the whole app: `index.html`, `cortex.js`, `cortex.wasm`,
`cortex.data` (the game data without its sounds, 52 MB), `cortex.data.json` (its size
and SHA-256) and `audio/` (the sounds, 295 MB). Opening the page downloads none of it:
the start screen offers "Play Game", which downloads the program and the package the
first time (the browser keeps them for later visits) and starts the game, and the
sounds follow in the background once the rest has arrived. Host `dist/` anywhere that
sends the two headers above and lets browsers revalidate the files
(`Cache-Control: no-cache`, as `serve.py` sends; a browser that kept an earlier
`cortex.js` would not match a newer package); the test pages in it can be left out. A
host that answers `Range` requests, as most do, lets a download that broke off go on
from where it stopped.

To run every automated check (test programs, recorded outputs, the game booting
and its deterministic simulation) in a fresh headless Chrome:

```sh
./build.sh --target checks
node tools/run-checks.mjs
```

CI runs the same on every push. `./build.sh --debug` builds with Emscripten's
runtime assertions into `dist-debug/`, and `./build.sh --worker` runs the engine on
a worker thread instead of the page's main thread, into `dist-worker/` (it plays the
same and starts about a second slower; see [`notes/threads.md`](notes/threads.md)).
See [`notes/testing.md`](notes/testing.md).

## Layout

```
engine/      the port's copy of the engine: Source/, Data/, external/, Resources/
             (PORT-CHANGES.patch lists every change from the original)
runtime/     browser platform layers: audio, filesystem, persistence
site/        the page that loads the game
vendor/      Lua 5.1.5, LuaBitOp, miniaudio
tests/       browser test programs and their pages
tools/       toolchain setup, headless Chrome driver, comparison tools
notes/       how the port works; notes/PORT-STATUS.md is the build log
specs/       the rules the game and its page must keep (see AGENTS.md)
```

## Licence

AGPL-3.0, the licence of the Cortex Command Community Project it is derived from
(see [`LICENSE`](LICENSE)). The game's code and data are the Cortex Command
Community Project's. Bundled third-party components keep their own licences:
those that came with the engine are in [`engine/Licences/`](engine/Licences) and
beside their sources in `engine/external/`; Lua and LuaBitOp are MIT
(`vendor/`); miniaudio is public domain or MIT-0.

FMOD is proprietary ([`engine/Licences/FMOD.TXT`](engine/Licences/FMOD.TXT)). The
browser build uses only FMOD's API headers, implemented here over miniaudio; the
native FMOD libraries in `engine/external/lib/` are upstream's, kept so the
engine copy stays complete, and are not used by the browser build.
