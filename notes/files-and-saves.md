# Assets, files and saves

Updated 2026-09-25.

Entry points: `runtime/filesystem.js` (mounts and flush policy),
`runtime/persistence-journal.js` (the per-file journal),
`runtime/sound-files.js` and `tools/audio_manifest.c` (the sounds, fetched after the
start), `CMakeLists.txt` (`--preload-file`), `site/index.html` (the data package's
download) and `tools/data-package-json.cmake`, `serve.py`,
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
- **The page brings the package's bytes, the packager makes its files**; see the
  next section. A returning player's browser keeps the package in Cache Storage.

### The data package's download

Emscripten's file packager writes `cortex.data` and, into `cortex.js`, the code that
makes its files: every directory first, then every file in the order of its path
(`FS_createPath`, then `FS_createDataFile` with views into the package's bytes). That
order matters: MEMFS lists a directory's entries in the order they were made, and
`DataModule::FindAndRead` reads a module's INI files in the order its directory lists
them, which decides the order presets are registered in. So the packager still makes
the files, and the page only brings the bytes, through the packager's hook
`Module.getPreloadedPackage(name, size)` (built without `--use-preload-cache`, which
would replace the hook with Emscripten's own IndexedDB cache).

The hook is called as `cortex.js` starts, and must return the bytes then; it is not
asked again. Waiting for them before starting `cortex.js` would put the program's
download, compilation and the thread pool's start after the package's download
instead of beside it. So `site/index.html` hands over the buffer the bytes will arrive
in, an empty `Uint8Array` of the package's size, and the packager, which may own the
buffer, makes every file as a view into it. The page's `Module.preRun` adds a run
dependency, `cortex-data`, which it removes only once every byte is there and has
matched; `main` cannot start before that. **The buffer must never be replaced or
detached** (no BYOB reader, no transfer): the files are views into it.

What the page does after Play, in `loadPackage`:

1. It fetches `cortex.data.json` (`no-cache`), which the build writes after each link
   (`tools/data-package-json.cmake`): the package's size and SHA-256.
2. It looks in Cache Storage (`cortex-data`) for the package under that hash
   (`cortex.data?sha256=…`). A copy that is whole and has the right hash is used; any
   other is deleted.
3. Otherwise it downloads `cortex.data` with `cache: 'no-store'` (Cache Storage keeps
   it; the HTTP cache would only keep a second copy) straight into the buffer, showing
   "Downloading the game: 12 of 52 MB". An attempt that gets no byte for 20 s is
   aborted (`AbortController`). A failed attempt is tried again after 1, 2, 4 and then
   8 s, asking for the rest only (`Range`, with `If-Range` so the rest is of the same
   file); a 206 continues where it stopped, a 200 starts again from the first byte. A
   compressed response is never resumed: its ranges are not the package's. Five failed
   attempts in a row, counting from the last one that got further into the package than
   any before, end the download; an attempt that starts again from the first byte and
   breaks off where the one before did is not progress, so a server that ignores
   `Range` on a connection that always breaks at the same place ends too. While it
   waits the page says "Downloading the game: 12 of 52 MB (trying again…)". If every
   byte came but the connection never ended, the stall's abort leaves the rest to the
   hash.
4. It checks the SHA-256 (`crypto.subtle.digest`). A mismatch downloads the whole
   package once more, in case a resumed download mixed two versions of the file that
   `If-Range` could not tell apart, and then fails.
5. It deletes Emscripten's old preload cache, the `EM_PRELOAD_CACHE` database, if it is
   there. Only that one: the saves are in the databases named after their directories
   (`/Userdata`, `/Mods`, `/ScreenShots`).
6. A downloaded package goes into Cache Storage once that database and the entries under
   other hashes, from earlier versions of the game, are gone, which makes room for it on
   a nearly full disk. The copy is a `Blob`, taken at once, while the bytes are the
   checked ones: once `main` runs, the package's files, views into them, could be
   written to. The browser writes a `Blob` off the page's thread; a body streamed from
   the buffer is written only as fast as the loading engine lets the page's thread run,
   and was still unfinished, and lost, when a tab closed as the menu appeared. A copy that
   cannot be kept (no room) is only reported in the console; the next visit downloads
   the package again.

If the package cannot be had, the page says so, "The game could not start." with the
reason on its last line, for instance "Could not download the game data: cortex.data:
503 Service Unavailable (5 tries). Reload the page to try again." or "…what arrived is
not this version's cortex.data (SHA-256 4473969c24ab…, expected 94f20f78d310…)…". The
package is never kept, and never used, unless its hash matched. A `cortex.data.json`
whose size is not the one `cortex.js` was built with means the two come from different
versions of the game, and the page says that instead. Once the page has given up, for
whatever reason, the download stops and `main` never starts, even when every step it
waited for arrives after all: the page does not start it (`startGame`) and drops what
would follow it, the sounds' download (`Module.postRun`). The host must let
browsers revalidate the game's files (`serve.py` sends `Cache-Control: no-cache`): with
a `max-age`, a browser can keep an earlier `cortex.js` beside the newer
`cortex.data.json`, which the page fetches `no-cache`, and would report different
versions on every reload until its copy expired (and an earlier `cortex.js` beside a
newer `cortex.wasm` fails too).

Two guards cover the rest of loading. A promise that fails with nothing to handle it
while `body[data-state]` is `loading` fails the load with its reason (once the game runs
it is left to the console: it need not be the game's end). And every step reports news:
every piece of the program's download and of the package's, a download's retries, each
step `main` waits for as it starts and as it ends (Emscripten's run dependencies,
through `Module.monitorRunDependencies`: the program, the thread pool, the saves, the
sound list, the package), and Emscripten's `setStatus`. With none for 30 s the page
gives up with "Loading made no progress for 30 seconds. Reload the page to try again."
The download's own deadlines (20 s, at most 8 s between attempts) keep it within that.
Only time the page runs counts: it looks once a second, and a look more than 5 s after
the one before means the page did not run in between, as while the computer sleeps (on
some systems the page's clock runs on meanwhile), so that gap is left out, and a
computer waking in the middle of the download gets it going again before the page
judges it. A hidden tab's timers can be held back to one a minute while its downloads
still report news, so there only a gap of more than 90 s is left out.
`?load-timeout=<ms>` sets the 30 s and scales the download's deadlines with it, for
`tools/run-checks.mjs`.

For that the page fetches the program, `cortex.wasm`, itself (`Module.instantiateWasm`):
Emscripten's own fetch of it reports nothing until it is compiled, and a returning
player whose package is kept still downloads a new program, 10.7 MB, after every update
of the game, which under about 3 Mbit/s takes longer than 30 s. The page splits the
response in two (`Response.clone()`), counts the pieces of one as they arrive, and gives
the other to `WebAssembly.instantiateStreaming`, which compiles it as it arrives, as
Emscripten does. Both halves are still the fetched response, which matters: Chrome keeps
the compiled program beside the response's HTTP cache entry (its code cache) and gives
it back to a later visit's compile, but only for a response fetched from its URL; one
the page made up (`new Response(stream)`) was never kept (no `v8.wasm.cachedModule`
trace event). Measured with the trace event `v8.wasm.moduleCacheHit`, on the Metal
instance, each time after a load that let the game run and a restart of the browser:
the page reused the kept code in every load (Play to the menu 2.32–2.39 s), as
Emscripten's own fetch did (2.33–2.42 s) under a URL new to the profile. Under one URL
whose cache many earlier runs had written, Emscripten's own fetch kept missing; which
runs leave a usable entry was not worked out. A host that does not send
`application/wasm` gets the program compiled once all of it is here, as Emscripten's
fallback did. If it cannot be had the page says, for example, "Could not load
cortex.wasm: 404 Not Found. Reload the page to try again."; one that stops arriving is
left to the 30 s guard, and is not tried again.

Until 2026-09-25 Emscripten's generated loader fetched the package with no deadline and no
resume, retried once from the first byte and then failed with a promise nothing
handled, which left the page on "Downloading the game" for good; a connection that
stopped sending did the same. Its IndexedDB cache stored whatever had arrived under
the build's hash without checking it.

**The directory order is unchanged.** Checked with a listing of `/Data` in `readdir`
order, taken just before `main`, in both builds: the 504 directories and 6,241 files
the packager makes are listed in the same order in every directory, and the 576 INI
files in the same order (a hash of the INI paths in listing order, equal cold and
warm, before and after). Only the sounds' stand-ins moved: they are written when the
sound list arrives (below), which used to be before the package's files were made, a
race the list won in every load checked, and is now always after, so in 118
directories they are listed after the package's files instead of before them. Nothing
in the game lists them together (only `LuaMan:FileList`/`DirectoryList` would, which
no shipped script uses), and both simulation hashes are unchanged.

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
   it, 24 at a time and in the list's order otherwise, and writes it over the
   stand-ins of every sound with those contents. A small note in the corner shows
   the progress ("Loading sounds: 42%").
3. FMOD creates every `Sound` from the list alone, since a sound needs only its
   length and sample rate until it plays. **A sound played before its file is
   here** plays silence from its first frame and starts from the beginning once
   the file arrives (checked every 50 ms), and FMOD asks the page for that file
   ahead of the rest (`Module.requestSoundFile`). The mixer's `?perf-debug` report
   counts these plays ("waiting for their file").
4. **A file that does not arrive is tried again, for as long as the page is
   open.** A try fails when the cached copy cannot be read or has the wrong length
   (it is dropped, and the file downloaded), when the request is refused or fails,
   when the body breaks off (as Chrome ends downloads under way when the network
   changes, `ERR_NETWORK_CHANGED`), when more or fewer bytes arrive than the list
   says, or when nothing arrives for 20 s: the request is then aborted.
   There is no limit on a download's total time, so a slow link that is still
   delivering is never cut off. Each file waits 1, 3, 9, 27 and then 60 s between
   tries; retries that are due go before files not tried yet, the one that failed
   longest ago first. The corner note adds how many sounds are being retried
   ("Loading sounds: 42%, 3 retrying"). Once six tries in a row have failed, as in
   an outage, the page tries one file at a time, after 1, 3, 9, 27 and then every
   60 s, until one arrives, rather than cycling through all 2,155. Each of those
   tries takes a file not tried yet, or else the one that failed longest ago, so a
   file that always fails (one missing from the server) cannot take every turn and
   leave the rest waiting. The browser's `online` event ends that at once and makes
   every waiting file due. FMOD asks for a sound's file the first time the sound
   plays in a session (not again on later plays): that file goes first, its wait
   cleared, and is tried at once even while the page tries one file at a time, if
   no other try is under way. The body is read into one array of the listed size,
   which the file in the filesystem then owns, so it is not copied again, and the
   cache gets a copy only after the length is checked: a cut-off download is never
   kept.
5. **An Activity starts once every file is here, or once the page has stopped
   waiting.** `ActivityMan::StartActivity` waits (suspended, 100 ms at a time)
   while the page shows the progress in the middle of the screen. Whether a sound
   is playing is visible to Lua scripts, so a game that began with sounds still
   missing could play differently; normally every game plays with all of them.
   But when nothing has arrived for three minutes (no file, and no bytes on a
   download still under way) the page stops waiting. Only time the page runs
   counts: it looks once a second, a hidden tab's timers can be held back for up
   to a minute, and a longer gap between looks, as while the computer sleeps (on
   some systems the page's clock runs on meanwhile), counts as one minute: a
   computer waking from sleep gets its downloads going again before the page gives
   up on them, unless they had already stalled for two minutes before it slept.
   Stopping, the page sets `Module.soundFilesComplete`, which is all the engine's
   wait reads (the engine
   keeps no deadline of its own), writes `c_SoundFileFailed` over the stand-ins of
   the files still missing, says in the corner how many sounds could not be
   downloaded ("2 sounds could not be downloaded; still trying") and keeps trying
   them. A file that arrives replaces its marker; the note goes once all are here.
   FMOD plays a sound whose file holds the marker as **silence as long as the
   sound**, loops and pitch included, then ends the channel and reports its end
   once, as the sound would have; `playSound` succeeds as before, so `AudioMan`
   draws the same random numbers. A channel already waiting for the file turns
   into that silence, from its beginning, when the marker appears. A sound that
   loops forever would never end, silent or not, so it keeps waiting for its file
   instead, and starts once the file arrives, as the menu's music does; told to
   stop looping while it waits (`AudioMan::FinishIngameLoopingSounds`, when an
   Activity ends), it becomes the silence of one play and ends. So a game that
   starts after a long outage keeps the timing of every sound, which is what
   scripts see, and hears each missing one from its first play after its file
   arrives; any other channel that began silent stays silent to its end. The
   simulation harness goes through the same wait, and its hashes did not change.

`audio_contract` checks the FMOD side. A listed sound whose file is a stand-in is
created with the listed length, plays silence, is asked for once, and after the
bytes arrive renders sample for sample what a sound that was there all along
renders. A failed sound's silence is exactly as many frames as the decoder makes of
the file at every sample rate the game's sounds use (11,025 to 96,000 Hz, the odd
43,989, 44,110 and 44,150 Hz included; FLAC, Ogg Vorbis and WAV): the decoder makes
the length times 48,000 over the file's rate, rounded up, which can be a frame more
than it reports as its length. A waiting channel whose file fails, and a later play
looped three times at pitch 1.5, are silent exactly as long as the real sound
renders (to the 128-frame block), report one end and free their channel. A sound
that loops forever keeps waiting, on a channel that was waiting when the marker
appeared and on a play after it; told to stop looping, the first is silent as long
as the sound and ends, and the second, once the file arrives, renders sample for
sample like the reference, as does the next play of the other failed sound once its
file arrives.

The `sound-files` check waits for every file to arrive and requires no failures.
`sound-files-faults` has `run-checks.mjs`'s own server cut 20 downloads off after
their first bytes; once 300 files are here, the cut ones among them, refuse every
sound file (503) for 3 s, long enough for the page to go to one file at a time, and
answer the first file it refused with 404 from then on; and after that never answer
one request. Every other file must arrive while that one keeps failing, with no
`online` event (a page that gives its one-at-a-time try to a retry that is due, as
that file always is, had only 380 of them after the check's two minutes); then it
is served and the page gets an `online` event, and it must arrive too. The request
never answered must be aborted and made again, and every cut-off file must be in
the cache whole. `sound-files-deadline` blocks two files (404): the Activity (the
harness's) must start once the page stops waiting, with the page saying "2 sounds
could not be downloaded; still trying", and both files must arrive once they can be
had and the page gets an `online` event. Both empty the cache first and shorten the
page's timeouts with `?sound-file-timeouts=4,180` and `=20,10` (the stall and the
deadline, in seconds), which a player's page never has.

Driven by hand with a scratch server, when the page downloaded six files at a time: with
every sound file refused from the start,
the page made 15 requests in the first 45 s (11 at once, until six in a row had
failed, then one after 1, 3, 9 and 27 s) and, with no `online` event, had every
file 23 to 56 s after the server answered again, as the next try was at most a
minute away. In that state a click on the main menu's Settings asked for
`ButtonPress.flac`, which was requested within 2 s, 22 s before the next try was
due. At 10 kB/s per download, with the timeouts shortened to 2 and 6 s, files that
took 6 s and more arrived and none was dropped in 40 s. With every file refused, the
deadline shortened to 90 s and the page's thread then kept busy for 100 s (a stand-in
for a computer asleep, whose ticks come as late), the page stopped waiting 28 s after
the thread was free again rather than at once. With the intro and menu music
blocked and the deadline shortened to 10 s, the page stopped waiting; both songs,
which loop forever, went on waiting (`?perf-debug`: "waiting for their file", none
"silent without it"), the main menu was silent (peak 0 over 3 s), and once the files
were unblocked and `online` fired, the menu's music started without being played
again (peak 0.39, every block audible).

### The start screen downloads nothing

Opening the page fetches none of the game. `site/index.html` first checks that the
browser can run it — `crossOriginIsolated` (the page's COOP/COEP headers) and
WebAssembly JSPI — and otherwise says which is missing and stops there. Then it
offers **Play Game**, and says nothing else: no size, no note about downloading.
Only the button loads `cortex.js`, which fetches the program, and the package (above),
and the game starts as soon as it has loaded. The first time, that is a download, whose
progress shows under the title ("Downloading the game: 12 of 52 MB"); a returning
player's comes from what the browser kept (below). `body[data-state]` names the
step (`checking`, `unsupported`, `offer-play`, `loading`, `running`, `failed`,
`closed`) for the tools: `tools/run-checks.mjs` and the driver's `play` command go
through it as a player would.

Until 2026-09-24 a first visit was offered **Download game (357 MB)**, the size added
up from `HEAD` requests and the sound list, and then Play; only a browser that kept
the game was offered Play at once. Telling the two apart needed the package's
`METADATA` record in `EM_PRELOAD_CACHE`, written for the page's own directory, not
merely the database: the file packager creates the database before it downloads
anything and writes the record only once every chunk is stored, so a first download
cut short had come back as Play. With one button for both, the page no longer looks,
and that database is gone (above); a download cut short keeps nothing now, since only
a whole package that matched is kept, and the next visit downloads it again.

It used to load `cortex.js` from the page itself, so anyone who opened the link, and
a browser that was then told it could not run the game, downloaded everything;
after the sounds were split out that meant the package and then every sound too,
358 MB with no click at all.

### What a load costs

Measured with `serve.py` on the Metal instance, every request recorded (a fresh
profile, then the same profile again):

| visit | fetched |
| --- | --- |
| opening the page, nothing pressed | the page alone (10 KB) |
| Play Game | 358 MB: the program 11.6 MB, the package 51.6 MB, 2,160 sound files 295 MB |
| returning, opening the page | nothing |
| returning, Play Game | nothing: `cortex.js`, `cortex.wasm`, `cortex.data.json` and the sound list are revalidated (304), the package and the sounds come from Cache Storage |

The browser keeps the package once, in Cache Storage, and the sounds once: both are
fetched with `cache: 'no-store'`. Until 2026-09-25 it kept the package twice
(IndexedDB, and the HTTP cache from Emscripten's own fetch, 61 MB with
`cortex.wasm`). Before any cache, a returning visit fetched the whole package again,
and Chrome's HTTP cache never kept the single 353 MB file.

Through a 50 Mbit/s link with 20 ms latency (measured with Emscripten's loader, before
the page brought the package itself; the bytes fetched are the same):

| | first visit | returning |
| --- | --- | --- |
| game loaded (from pressing Play Game) | 10.8 s | — |
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
overlapping explicit flushes. Flush errors are logged, not fatal. None of them
commits a file that is still open for writing; its last close does (see [the
journal](#a-file-goes-out-once-it-is-closed)).

An engine-side write returning success does **not** mean the bytes are in
IndexedDB. Persistence is asynchronous; only a reload proves it.

Quitting (the main menu's Exit, Quit Program in Conquest) returns to the page's
start screen: the engine marks the return (`BrowserReturnToStartScreen`) and
exits, and the page's `onExit` runs `Module.persistSaves()`, which waits for a
flush in progress and flushes again, before it reloads. It is the page that
flushes because the filesystem is the page's, even when the engine runs in a
worker. Before it exits, the engine closes the files scripts left open (see [the
journal](#a-file-goes-out-once-it-is-closed)). Checked in both builds: a file opened
from the Lua console in `UserSavedGames.rte` and written without being closed, its
line still in the C library's buffer (0 bytes on the filesystem), then Escape at
the main menu, was in IndexedDB with its line after the reload.

### Settings are written as they change

Upstream writes `Userdata/Settings.ini` when the player leaves the Settings or Mod
Manager screen (`HandleBackNavigation`), at boot, and on a few resolution changes.
A browser player can close the page or reload it at any moment, which lost every
change made since entering the screen. So both screens call
`SettingsMan::UpdateSettingsFileIfChanged` every frame they are open, in the main
menu and the pause menu alike: at most four times a second it serializes the
settings exactly as the file is written and writes the file if they differ from
what was last written. From there `autoPersist` takes it to IndexedDB within a
frame. Nothing else in the game changes saved settings, apart from a command-line
mode ([the spec](../specs/settings.md)).

Checked in a fresh headless profile: "Enable VSync" off and "Skip intro" on were in
the stored file a second after each click, with the Settings screen still open;
a reload started from there, Back never pressed, came up with both, and with the
scale chosen earlier.

Every browser `Settings.ini` also carries `BrowserSettingsVersion`; a file without
it has its scale reset to the default once ([display](display.md)).

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
- A file goes out only once **the streams writing it have closed** (below), so
  IndexedDB holds either the previous complete version of a file or the new
  complete one. The exception is a file other than a save archive that a writer
  leaves open and unwritten for a minute.

What it is not: conflicting edits to the same file resolve by transaction commit
order. There is no semantic merge of save files.

`CMakeLists.txt` sets explicit `LINK_DEPENDS` on the pre-JS files so editing
the journal relinks the game.

### A file goes out once it is closed

A scenario save is written in place. `ActivityMan::SaveCurrentGame` prints "Game
saved" and hands the writing to a background-pool task, which opens
`<name>.ccsave` on its final path with `fopen("wb")` (minizip), truncating it,
writes `Index.ini` and `Save.ini`, PNG-encodes every scene layer, writes those and
closes the zip. Measured on the Metal instance, with other builds loading the
machine: the file stays open 0.37–0.46 s in Dummy Assault (2.5 MB) and 0.76–0.95 s
in Decision Day (4.9 MB), most of it spent encoding the PNGs, when nothing is
written. The pool thread's file calls run on the page's thread, and each write used
to queue a commit, so within milliseconds of the save's start IndexedDB held the
truncated empty file, and then zips with no central directory (0, 59,524 and 318,046
bytes in one Decision Day save) until the complete one. If the tab was closed,
killed or crashed in that window, the previous save of that name (F5's QuickSave is
always the same file) was replaced by one that cannot be loaded, and a new name left
a file that cannot be loaded. The five-second and `visibilitychange` flushes would
have committed the same partial file, so queuing fewer commits alone would not have
fixed it.

So the journal counts, on each file's node, the streams open for writing: its
`stream_ops.open`, `dup` and `close` wrappers count a stream whose access mode is
not read-only, and tag it so that its close counts once; a duplicated descriptor is
a writer of its own. An outgoing sync leaves out every path whose file still has a
writer. The path stays dirty (the transaction clears only the versions it
captured), and IndexedDB keeps its last committed version, or for a new file
nothing. The last writer's close queues the commit. Filtering the sync is what
covers every trigger (`mark`'s queued commit, the interval, `visibilitychange`,
`persistSaves`), and the count cannot be checked when marking instead: `FS.open`
truncates, which marks, before the stream exists. Files written in one go
(settings, scenes, screenshots) still go out within a frame of their close.

Each open for writing starts the count again at one, and only that open's stream
and its duplicates count down. A save that fails never closes its archive: the
background pool keeps a task's exception in its future, `ActivityMan` only waits on
it, and a throw inside the task (`std::bad_alloc` near the memory cap, say) skips
`zipClose`. Counting every writer, that stream would hold back every later save of
the name for the rest of the session. Saves of one game never overlap
(`SaveCurrentGame` waits for the previous one), so an earlier writer still open
when the next opens the archive is always such a stream.

A file can stay open indefinitely: nothing makes a script close a
`LuaMan:FileOpen` handle. Such a file goes out anyway once it has gone **60 seconds
without a write**, as the old flushes took it, with two exceptions. A **save
archive** (`.ccsave`) never goes out while open: only the save task writes one, and
it closes it unless the save failed; a failed save, one that stalls, or one whose
tab the browser froze for a minute while it encoded its PNGs, would otherwise
replace the last good save. And nothing goes out while open once the runtime **has
crashed**: Emscripten's `ABORT` is set or the page shows `data-state="failed"`.
After a crash the page stays open and keeps flushing, and a file whose writer never
closed it would otherwise go out a minute later. The minute is
`installPersistenceJournal`'s argument, which only the storage fixture passes.

Quitting closes the files scripts left open before the page flushes:
`emscripten_force_exit` skips the native teardown, whose `LuaMan::Destroy` closes
them, and the C library's flush, so `main` calls `g_LuaMan.FileCloseAll()` before
`BrowserReturnToStartScreen`. Closing or reloading the tab has no such step: a
handle still open then keeps out everything written since the file last went out,
which takes a minute without a write. Nothing in the shipped data opens one.

### Verified journal behaviour

The real-IndexedDB fixture (`storage_runtime`, `storage-race-check.html`) runs
separate modularized runtimes against the production adapter and passes:
stale-instance preservation, newer-content protection, writes with deliberately
identical timestamps, intentional deletion without resurrection, recursive
directory rename including an empty directory, remote-child preservation under
stale `rmdir`, injected database failure and retry, transaction abort with atomic
rollback and retry, overlapping flushes with an intervening write, and automatic
persistence. For open files: a file being rewritten keeps its committed version
through a flush while open, including through a duplicated descriptor after the
original is closed, and goes out when the last one closes; a stream open for
reading holds nothing back, and an open file holds back no other; a new file is
absent until closed; with `autoPersist` the close is what schedules the commit; a
file left open goes out after the idle time (shortened to 300 ms there), but not a
save archive, not while the page shows a failure, nor after the runtime has
aborted (`storage_runtime`'s `crash()` calls `abort()`); and an archive left open
by a failed save does not hold back the next save of it. Against the journal
without the hold-back the fixture fails at the first of these.

In the game: a script handle (`LuaMan:FileOpen`, a 3,000-byte line written,
left open) was absent from IndexedDB while its last write was 58.8 s old and there
at 67.8 s (and in a second run absent at 58.5 s, there at 64.5 s); closed, the file
went out within 1.6 s. `Settings.ini` deleted from the store came back at boot. For
quitting see [flush policy](#flush-policy), for interrupted saves [saved
games](#saved-games).

Not covered: storage eviction, real quota exhaustion, and arbitrary conflict
resolution.

## Saved games

`ActivityMan` writes scripted saves as **`.ccsave` zip archives** containing
`Index.ini`, `Save.ini`, and terrain and fog layers as PNGs. It serialises the
activity, retrieves scene objects *without* transferring ownership, invokes each
movable object's `OnSave` hook, snapshots the bitmap layers, and dispatches the
archive write to the **background** pool, which writes the archive in place under
its final name. The menu waits on `IsCurrentlySaving` before refreshing its list.
The journal commits the archive only once it is closed ([above](#a-file-goes-out-once-it-is-closed)).

Mission-specific state lives in the activity's own save hooks — Dummy Assault's
script, for instance, stores `spawnTimer.ElapsedSimTimeMS` and `alarmTriggered`. Saving only
terrain and actors would lose it. See [activities](activities.md).

Verified end to end: Dummy Assault launched through the normal UI, saved through
the pause menu, then discovered and loaded by a **newly initialised** game
instance, resuming with brain, fog, objective and funds intact. That exercises
archive creation, persistence, decoding and scripted resume together.

A save cut short keeps the previous one. Checked on the Metal instance beside the
same build without the hold-back, each time after a first F5 QuickSave that passed
`unzip -t`: in Dummy Assault, a second F5 flushed with every mount by the first
write that took its zip past 64 KB (to 176,984 bytes), the page frozen when the
flush finished and Chrome killed; in Decision Day, Chrome killed (`kill -9`) 0.6 s
after a second F5, inside the write (0.48 s after the file was opened, in this
build's run). Reopened, the old build's QuickSave was the partial zip (176,984 and
59,715 bytes; `unzip`: "cannot find zipfile directory"), this build's the first
save, byte for byte. Repeated in Dummy Assault with the rules as they are now, in
the page-thread and worker builds (the flush at 176,917 and 176,868 bytes): the
same. A QuickSave, a reload and F9 in Dummy Assault printed `Game "QuickSave"
loaded!`, and so did F9 after the reopen that followed a kill.

Not tested: failed storage commits, every serialized field, and playing a loaded
mission through to completion.

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
- There is no incremental or lazy asset loading; the whole 51.6 MB package loads
  before the menu appears, and any change to `Data` makes every returning player
  download all of it again.
- The list of sound files (`audio/manifest.tsv`) is fetched once: if that fails,
  the game stops at "Could not load the list of sound files".
- A sound that began as silence, its file missing, stays silent to its end even if
  the file arrives meanwhile; only later plays are heard. (One that loops forever
  waits for its file instead, and is heard once it arrives.)
- A file missing from the server (404) is tried again like any other failure, so it
  holds the first Activity for the whole three minutes before the page stops
  waiting.
- The sound downloads' retries and deadline have been tried against a local server
  that refuses, stalls, cuts off and slows them, not against a real network change.
- `serve.py` does not answer `Range` requests, so there a failed download starts
  again from the first byte; `tools/run-checks.mjs`'s server answers them, as real
  hosts do.
