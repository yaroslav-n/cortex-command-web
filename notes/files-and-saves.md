# Assets, files and saves

Updated 2026-09-24.

Entry points: `runtime/filesystem.js` (mounts and flush policy),
`runtime/persistence-journal.js` (the per-file journal),
`runtime/sound-files.js` and `tools/audio_manifest.c` (the sounds, fetched after the
start), `CMakeLists.txt` (`--preload-file`), `serve.py`,
`engine/Source/System/System.cpp` (directory constants),
`engine/Source/System/ContentFile.cpp`,
`engine/Source/Managers/ActivityMan.cpp` (`.ccsave` writing).

There are two completely separate kinds of storage here, and keeping them
separate is the main design rule:

| | shipped game data | user state |
| --- | --- | --- |
| what | `Data/` — INIs, sprites, sounds, shaders | settings, saves, scenes, screenshots, mods |
| where | Emscripten preload bundle, and sound files fetched after the start; read-only in practice | IndexedDB via IDBFS |
| lifetime | replaced wholesale by a new build | must survive builds |

Mixing them is how save migrations break. A new build replaces all of column one
and must not assume anything about column two.

## Shipped assets

`--preload-file "${ENGINE}/Data@/Data"` packages the `Data` directory into the
Emscripten filesystem at the same path the engine already uses, so no engine code
needed a path change: about 6,250 files, 51.6 MB (`cortex.data`). The sounds, 2,248
of them in 2,155 distinct files and 294 MB, 85% of the data, are left out of it
(`--exclude-file`) and arrive after the game has loaded; see below.

Consequences worth knowing:

- **Paths are case sensitive** in the browser filesystem, where macOS and Windows
  are not. A mismatched case works natively and fails silently here.
- **Shaders are ordinary asset files.** Preload must complete before rendering
  initialisation, or shader lookup fails. The bundle is also a *link-time* input:
  `CMakeLists.txt` sets `LINK_DEPENDS` on the shader files so editing a
  `.frag` actually relinks. Without that, shader edits appear to do nothing —
  this cost real debugging time once. The `--pre-js` files are listed the same way.
- Rebuilding the inventory is required after changing assets.
- **A returning player's browser keeps the package** in IndexedDB
  (`--use-preload-cache`, Emscripten's `EM_PRELOAD_CACHE` database) and uses it
  while its SHA-256 matches the build's.

### The sounds arrive after the start

At build time `tools/audio_manifest.c`, compiled with the port's miniaudio and run
under Node, reads every sound file's length and sample rate exactly as
`FMOD::System::createSound` would, and copies the file into `dist/audio/` under a
name made from its contents (FNV-1a, 64 bits; identical sounds share a file), so a
copy never changes and can be cached for good. `dist/audio/manifest.tsv` lists
each sound: its engine path, size, length, sample rate and copy.

`runtime/sound-files.js` (a `--pre-js`) then does the rest on the page:

1. **Before `main`**, it fetches the list, writes it to `/audio-manifest.tsv`, and
   puts a stand-in file wherever the engine expects a sound. The stand-in is a
   short text (`c_SoundFileOnItsWay`), not an empty file: the engine refuses to load
   a sound from an empty file (`ContentFile::LoadAndReleaseSound`).
2. **Once the game has loaded** (not earlier, so the package gets the bandwidth),
   it takes each distinct file from Cache Storage (`cortex-sounds`) or downloads
   it, six at a time and in the list's order otherwise, and writes it over the
   stand-ins of every sound with those contents. A small note in the corner shows
   the progress.
3. FMOD creates every `Sound` from the list alone, since a sound needs only its
   length and sample rate until it plays. **A sound played before its file is
   here** plays silence from its first frame and starts from the beginning once
   the file arrives (checked every 50 ms), and FMOD asks the page for that file
   ahead of the rest (`Module.requestSoundFile`). The mixer's `?perf-debug` report
   counts these plays.
4. **An Activity starts only once every file is here.** `ActivityMan::StartActivity`
   waits (suspended, 100 ms at a time) while the page shows the progress in the
   middle of the screen. Whether a sound is playing is visible to Lua scripts, so
   a game that began with sounds still missing could play differently; this way
   every game plays with all of them. The simulation harness goes through the
   same wait, and its hashes did not change.

`audio_contract` checks the FMOD side: a listed sound whose file is a stand-in is
created with the listed length, plays silence, is asked for once, and after the
bytes arrive renders sample for sample what a sound that was there all along
renders. The `sound-files` check waits for every file to arrive and requires no
failures. A file that cannot be fetched after four tries is given up on (its sound
stays silent, and the page says so in the console); it does not hold up an
Activity.

### The start screen downloads nothing

Opening the page fetches none of the game. `site/index.html` first checks that the
browser can run it — `crossOriginIsolated` (the page's COOP/COEP headers) and
WebAssembly JSPI — and otherwise says which is missing and stops there. Then it
offers **Download game (357 MB)**, the size added up from `HEAD` requests for
`cortex.js`, `cortex.wasm` and `cortex.data` and from the sound list, or **Play**
when the browser already keeps the data package (its record in `EM_PRELOAD_CACHE`,
below). Only the button loads `cortex.js`, which fetches the program and the
package; a player who asked to download it then presses Play, and a returning one's
Play starts the game as soon as it has loaded. `body[data-state]` names the step
(`checking`, `unsupported`, `offer-download`, `offer-play`, `loading`, `ready`,
`running`, `failed`, `closed`) for the tools: `tools/run-checks.mjs` and the driver's
`play` command go through it as a player would.

**What counts as kept** is the package's record, not the database. Emscripten's
file packager creates `EM_PRELOAD_CACHE` before it downloads anything and writes a
package's `METADATA` record only once every chunk is stored, so a first download
cut short leaves the database empty. The page used to offer Play whenever the
database existed: a player who closed the tab at 6 of 52 MB on a 50 Mbit/s link
came back to Play, which downloaded all 357 MB again with no size shown. It now
wants a record whose key is `metadata/`, then this page's directory URI-encoded
(the packager's `PACKAGE_PATH`), then the package's name as built — an absolute
path on the build machine, so the page matches the directory and not the whole
key. A package stored by a page in another directory of the same site does not
count either. `start-screen-cut-short` blocks the package's request, comes back,
and requires the download again. After an update the record is the old build's,
which the page cannot tell before `cortex.js` has loaded, so a returning player's
Play then downloads the new package.

It used to load `cortex.js` from the page itself, so anyone who opened the link, and
a browser that was then told it could not run the game, downloaded everything;
after the sounds were split out that meant the package and then every sound too,
358 MB with no click at all.

### What a load costs

Measured with `serve.py` on the Metal instance, every request recorded (a fresh
profile, then the same profile again):

| visit | fetched |
| --- | --- |
| opening the page, nothing pressed | 0.2 MB: the page, the sound list, three `HEAD` requests |
| Download game, then Play | 358 MB: the program 11.6 MB, the package 51.6 MB, 2,160 sound files 295 MB |
| returning, opening the page | nothing |
| returning, Play | nothing: `cortex.js`, `cortex.wasm` and the sound list are revalidated (304), the package comes from IndexedDB and the sounds from Cache Storage |

The browser then keeps the package twice (IndexedDB, and the HTTP cache from
Emscripten's own fetch, 61 MB with `cortex.wasm`) and the sounds once: they are
fetched with `cache: 'no-store'`, since Cache Storage has them. Before the preload
cache, a returning visit fetched the whole package again, and Chrome's HTTP cache
never kept the single 353 MB file.

Through a 50 Mbit/s link with 20 ms latency:

| | first visit | returning |
| --- | --- | --- |
| game loaded (from pressing Download / opening) | 10.8 s | — |
| main menu | 13.8 s | 2.6 s |
| every sound here | 59.3 s | 2.8 s |

Without the split the first visit needed every byte of the 353 MB before Play,
about a minute at that speed. The one sound played at the main menu before its
file arrived, the menu music, started about a second late. From a local server
the game is ready in under half a second and the sounds are all there a few
seconds later. gzip saves 80% of `cortex.wasm` and 86% of `cortex.js` and nothing
worth having on the sounds, which are compressed already; a host should compress
the first two.

After Play, the title screen takes about 2.6 s on the Metal instance: startup
managers, then all data modules (1.2 s). See [threads](threads.md) for why yields during
loading must not go through timers.

## User state: IDBFS

`filesystem.js` runs in `preRun` and mounts IDBFS at three paths:

```js
for (const path of ['/Userdata', '/Mods', '/ScreenShots']) {
  FS.mkdirTree(path);
  FS.mount(IDBFS, { autoPersist: true }, path);
}
```

These **must match `System::s_UserdataDirectory`, `s_ModDirectory` and
`s_ScreenshotDirectory` exactly.** The mount was once `/Screenshots` while the
constant is `ScreenShots/`; `System::Initialize` then created a plain in-memory
`/ScreenShots` and every screen dump, world dump and scene preview was silently
discarded on reload. When adding a mount, copy the constant out of `System.cpp`
rather than retyping it.

### Startup ordering

`preRun` adds a run dependency (`restore-cortex-saves`), mounts, and calls
`FS.syncfs(true, …)` to restore from IndexedDB. The dependency is only released
on **successful** restore, which keeps `main()` from reading settings before they
exist.

On restore failure the dependency is deliberately **never released** and
`Module.onAbort` fires. Starting with default state after a failed restore would
let the first flush overwrite the user's real saves with defaults. A hung startup
is the safe failure here.

### Flush policy

Writes rely on `autoPersist`, plus `Module.flushSaves()` on a five second
interval and on `visibilitychange` → hidden. A `syncing` flag prevents
overlapping explicit flushes. Flush errors are logged, not fatal.

An engine-side write returning success does **not** mean the bytes are in
IndexedDB. Persistence is asynchronous; only a reload proves it.

Quitting (the main menu's Exit, Quit Program in Conquest) returns to the page's
start screen: the engine marks the return (`BrowserReturnToStartScreen`) and
exits, and the page's `onExit` runs `Module.persistSaves()`, which waits for a
flush in progress and flushes again, before it reloads. It is the page that
flushes because the filesystem is the page's, even when the engine runs in a
worker. Checked in both builds: a file written from the Lua console into
`UserSavedGames.rte`, then Escape at the main menu, was in IndexedDB after the
reload.

## The persistence journal

This is the least obvious part of the system and the easiest to regress.

**The problem.** Emscripten's bundled `libidbfs.js` reconciles by enumerating
destination entries missing from the source and deleting them, and writes local
files whenever timestamps differ. For a local→remote sync that means a runtime
which restored an older snapshot will *delete* files it has never heard of, and
overwrite newer remote content with unchanged older copies. Two tabs of the game
open at once, and the stale one silently destroys the other's saves. This was
reproduced deliberately: two runtimes restore the same empty store, A writes a
scene and commits, a third runtime confirms the exact bytes, the stale instance
syncs **without making any changes**, and a fourth runtime finds the scene gone.

**The fix.** `persistence-journal.js` is installed by `filesystem.js` *before*
mounting, and wraps the IDBFS-backed MEMFS node and stream operations. It records
successful local creates, writes, attribute changes, removals and rename
descendants. Restore operations suppress recording.

The properties it guarantees:

- An outgoing sync snapshots **only journaled entries** and commits them in one
  IndexedDB transaction. A runtime that changed nothing has no outgoing entries
  and therefore cannot erase or overwrite anything.
- Journal records carry **versions**. A successful transaction clears only the
  versions it captured, so writes made while it was in flight survive. A failed
  or aborted transaction retains everything pending.
- Each mount **serialises** its sync requests.
- Missing parent directories are created as needed.
- A stale `rmdir` preserves a directory if remotely added children would
  otherwise be orphaned.
- Unsupported IDBFS node types **fail** rather than silently claiming
  persistence.

What it is not: conflicting edits to the same file resolve by transaction commit
order. There is no semantic merge of save files.

`CMakeLists.txt` sets explicit `LINK_DEPENDS` on the pre-JS files so editing
the journal relinks the game.

### Verified journal behaviour

The real-IndexedDB fixture (`storage_runtime`, `storage-race-check.html`) runs
separate modularized runtimes against the production adapter and passes:
stale-instance preservation, newer-content protection, writes with deliberately
identical timestamps, intentional deletion without resurrection, recursive
directory rename including an empty directory, remote-child preservation under
stale `rmdir`, injected database failure and retry, transaction abort with atomic
rollback and retry, overlapping flushes with an intervening write, and automatic
persistence.

Not covered: browser process termination, storage eviction, real quota
exhaustion, and arbitrary conflict resolution.

## Saved games

`ActivityMan` writes scripted saves as **`.ccsave` zip archives** containing
`Index.ini`, `Save.ini`, and terrain and fog layers as PNGs. It serialises the
activity, retrieves scene objects *without* transferring ownership, invokes each
movable object's `OnSave` hook, snapshots the bitmap layers, and dispatches the
archive write to the **background** pool. The menu waits on `IsCurrentlySaving`
before refreshing its list.

Mission-specific state lives in the activity's own save hooks — Dummy Assault's
script, for instance, stores `spawnTimer.ElapsedSimTimeMS` and `alarmTriggered`. Saving only
terrain and actors would lose it. See [activities](activities.md).

Verified end to end: Dummy Assault launched through the normal UI, saved through
the pause menu, then discovered and loaded by a **newly initialised** game
instance, resuming with brain, fog, objective and funds intact. That exercises
archive creation, persistence, decoding and scripted resume together.

Not tested: abrupt closure during writing, failed storage commits, every
serialized field, and playing a loaded mission through to completion.

## File calls

Every file call (open, stat, read, seek, close) is a call into Emscripten's
JavaScript filesystem, and from any thread but the page's it is a synchronous
round trip to the page's thread, about 13 µs each. Loading the data modules made
about 215,000 of them, mostly from images: `IMG_Load` finds a file's format by
asking each of SDL_image's decoders in turn, and each one seeks the stdio stream
back, which discards the read buffer and reads again, so a sprite cost some forty
calls. `ContentFile::LoadImageAsSurface` now reads the whole file with four calls
(open, fstat, read, close) and decodes the same bytes from memory
(`IMG_LoadTyped_IO` on `SDL_IOFromConstMem`, with the extension as the type hint,
as `IMG_Load` passes it). Startup now makes about 57,000: 15,000 stats (asset
existence checks), 16,500 reads, 9,000 opens, 8,000 closes, 4,700 fstats and 4,000
seeks, the seeks from reading sound headers. With the engine on the page's thread
the calls are direct and the difference is small; with the engine in a worker it
was 2.6 s of startup and is now 0.9 s.

## ContentFile and asset identity

`ContentFile` maps paths to hashes and cached assets. The legacy keys are
`size_t`, which is **32-bit under Wasm and 64-bit natively** — the same path
hashes to different widths on the two platforms.

## Screenshots and dumps

`System::s_ScreenshotDirectory` is `ScreenShots/`, relative to the working
directory, and is mounted. `FrameMan::SaveBitmap` writes there. Pull files out
with the driver:

```sh
./tools/cc.sh getfile /ScreenShots /ScreenShots/<file>.png /tmp/out.png
```

That is also how the native/browser image comparisons are collected; see
[testing](testing.md).

## Known gaps

- Loading a mod from the mounted `/Mods` directory has never been exercised.
- Quota behaviour when saves plus mods exceed the browser's limit is unknown.
- There is no incremental or lazy asset loading; the whole 336 MiB bundle loads
  before the menu appears.
