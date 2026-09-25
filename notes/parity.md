# Parity with the original game

Updated 2026-09-23.

Entry points: `tools/parity-probe/` (the tools), `original-code/` (the
reference), `engine/PORT-CHANGES.patch` (every change the port makes).

This note says how identical the browser port is to the original, how that is
measured, and why some things cannot be made identical. Read it before claiming
anything is or is not "the same as the original".

## The reference

`original-code/` is a plain clone of upstream at `20dfb3ea5`, with no edits at all
(`git status` is clean), next to this repository (`../original-code`, or
`ORIGINAL_CODE`). It was fetched afresh on 2026-09-24; the copy used before then
carried two `#include <cstdlib>` lines in RakNet, and its packaged binary had at
one point been built from modified source. It is built on macOS with
**g++-13 `-O3`** for arm64 (`original-code/build-mac`, packaged into
`Cortex Command.app`), linked against Apple's system libm; the missing RakNet
include is supplied as a compiler flag (`-include cstdlib`) instead of a source
edit. `notes/original-game.md` at the repository root has the exact steps. Never patch it to
make a comparison easier. To build an experimental variant, point meson at a
scratch build directory (`meson setup <scratch> original-code …`) so nothing lands
in the reference.

Native builds need two environment variables on this machine, because the Xcode
licence has not been accepted and the Command Line Tools clang rejects the
default deployment target:
`DEVELOPER_DIR=/Library/Developer/CommandLineTools MACOSX_DEPLOYMENT_TARGET=15.0`.

## The probes

All are content-only global scripts in `ParityProbe.rte`, installed into the
original's `Mods/` and into the browser's `/Mods` IndexedDB store, and enabled
through each build's own `Settings.ini` with `LaunchIntoActivity`. Neither build's
code is changed. `run-original.sh` and `run-port.sh` restore settings and remove
the mod afterwards.

| probe | script | writes | answers |
| --- | --- | --- | --- |
| start | `Parity Probe` | every terrain material pixel + 40 master-state Lua draws, at `StartScript` | is the generated mission identical? |
| timeline | `Parity Timeline` | every actor's class, preset, unique ID, position, velocity, rotation, angular velocity, health, status; item and particle counts; a 1/64 terrain sample — at steps 1, 30, 60, 120, 240, 480, 900 | does play stay identical? |
| screen | `Parity Screen` | a screenshot at step 300 through the game's own `FrameMan:SaveScreenToPNG`, camera pinned on the player's brain from step 200 | does it look identical? |
| defeat / victory | `Parity Defeat`, `Parity Victory` | gibs the human brains (or every non-human actor) at step 60 and records activity state and winner to step 600 | do missions end the same way? |

Select a probe with `PROBE_SCRIPT` and its output with `PROBE_FILE`
(`ParityTimeline.txt`, `ParityScreen.txt`, `ParityEndgame.txt`); add
`PROBE_SCREENSHOT=1` for the screen probe, which makes the run scripts collect the
newest `ParityScreen_*.png` next to the output and `sweep.sh` compare them with
`imgdiff.py`. Other environment knobs: `PROBE_SETTINGS="Key=Value …"` overrides settings, `ORIGINAL_BINARY` runs
another build of the original from the same folder, `ORIGINAL_LOG` keeps its
output.

Traps, each of which produced a false result once:

- **Match the resolution.** Sight rays reach `0.51 × player screen width`, so
  resolution changes what actors reveal. The original runs 864×558 at scale 1.5;
  give the port `./tools/ccw.sh 20 resize 1296 887` (the page's 50 px strip sits
  under the game) and
  `PROBE_SETTINGS="ResolutionMultiplier=1.500000"`.
- **The camera glides on real time.** `CameraMan::Update` scales each step of the
  glide by `ScrollTimer.GetElapsedRealTimeMS()`, so builds running at different
  speeds frame different views at the same simulation step — and the original,
  run headless without vsync, draws so fast that its glide barely advances. The
  screen probe therefore pins the camera (`SetScrollTarget(pin, 0.1, 0)` and
  `SetScroll(pin, 0)` every step from 200, as a late-update script so it runs
  after the activity sets its own target).
- **luabind has no default arguments.** A binding whose C++ signature has
  defaults must be called with all of them from Lua (`SetScrollTarget(target,
  speed, screen)`); a short call fails to match, the script stops, and the run
  waits for a marker that never comes.
- **Only the enabled script's class table exists.** Two probe classes in one file
  (`ParityEndgame.lua`) must each be created (`X = X or {}`) before methods are
  defined on them, or the file stops at the first missing one and the other
  class never works.
- **Stale output in IndexedDB.** The port's run waits for the output file to
  exist. `idb.py` now deletes every probe output on install and restore; before
  that, a second run silently returned the first run's file.
- **An activity's `SceneName` is not always a scene.** Signal Hunt names *Zombie
  Cave*, which is a terrain object. The menu has the player pick a scene, so this
  never shows in normal play; `LaunchIntoActivity` with a non-scene crashes the
  original in `FrameMan::Draw`. `missions.txt` pairs Signal Hunt with *Unexplored
  Cave*.
- **Refinery Assault is not in this version of the game.** `Browncoats.rte/Index.ini`
  comments out its `Activities.ini` and `Scenes.ini`, so neither build can load
  it. Launched anyway, the original segfaults and the port logs "Couldn't find
  the GAScripted named Refinery Assault" and stays on the title screen.

## Results

### At mission start: identical

Terrain material and the master Lua stream are **byte-identical** to the original
in all 17 scripted activities the game loads (`missions.txt`): Exploration,
Dummy Assault, Bunker Breach, Brain vs Brain, Decision Day, Maginot Defence,
Harvester, Signal Hunt (on Unexplored Cave), Skirmish Defense, Wave Defense,
Survival, Massacre, Keepie Uppie and all four One-Man Army variants. This rests on the port reproducing libstdc++'s distributions and the
original's reseed in `Activity::Start`; see [randomness](randomness.md).

The Tutorial is a `GATutorial`, not a `GAScripted`, so global scripts — and
therefore the probes — never run in it. Its **scene** can still be compared: the
probe mod's do-nothing activity (`Parity Blank`) loads Tutorial Bunker, and its
terrain and Lua stream are byte-identical to the original's (2110×840); a
screenshot at step 300 differs in 4,474 pixels, all on the robots and a crab
that have walked (drift). The tutorial as a player starts it — its text, room
signs and brain room — was compared through scripted input (below): identical
once the still-gliding camera is aligned.

### Menus, the campaign and the tutorial: scripted input to the original

The probes cannot reach screens outside an activity, and the Mac running the
comparisons has its screen locked, so the window server delivers no input to the
native game. `tools/native-input/` supplies it instead: `libinject.dylib`,
loaded into the untouched original with `DYLD_INSERT_LIBRARIES`, interposes
`SDL_PollEvent` and hands the game the events of a timed script (mouse motion,
clicks, drags, keys, text, window focus), keeping SDL's keyboard state array in
step for the GUI library. `run-original.sh` runs the original with a script and
collects the ScreenDumps its F12 key writes; `run-browser.sh` plays the same script
into the port through the headless Chrome driver and collects the port's. Both run
at 864×558 ×1.5, so script coordinates (window points) are game pixels ×1.5, and
both restore their settings afterwards. `EXTRA_USERDATA` installs files into the
original's Userdata for one run (put back afterwards) — used to give both builds the
same saved campaign.

Results, pixels differing of 482,112 (compared with `imgdiff.py`):

| screens | differences |
| --- | --- |
| title menu; Scenario Battle; Saved Games; Mod Manager; Game Editors | stars' brightness and the logo's glow only (2–7%) |
| Credits | the same, plus the credits text, which scrolls on the wall clock |
| Conquest notice, New Campaign dialog, Day One, Gold Sites Found, Player 1's Turn | stars; the station orbiting the planet, the pulsing site crosshairs and the blinking "> Continue <" (real time) — about 2% |
| loaded campaign: map, day phases, site panel (text, funds 1815/1266, brains) | stars, the station, and the site's leader line, label and icon border |
| a campaign battle from that save (Space advances the phases): its landing-zone screen, and the field after landing | a drifting cloud and blinking delivery arrows; after landing the same units at the same spots, the view 23 px apart (camera timing), funds identical to the cent (546.049988, then 61.049988, from the console) |
| Game Editors → Scene Editor: Load Scene, Create New, New Scene, Build (Alezer Canyon) | Load Scene 0 pixels; New Scene's background 1 px apart (the camera's floored step again); the built scene's terrain, water and object picker match, its scattered boulders and gold differ because new-scene generation draws from the global stream that menu frames have advanced, and the clouds drift |
| the Tutorial from a fresh session, settled (40 and 60 s in), the original paced to 60 fps | the view is 2 px apart; aligned, terrain, bunker, rooms, robots, the SMG and the tutorial's text match, leaving the parallax sky, drifting clouds, the brain's pulsing marker and the screen-fixed HUD |

Every one of those is either redrawn with a fresh random number each frame — the
stars are re-tinted and the logo, site lines and labels get a random blend amount
per frame (`TitleScreen.cpp:565`, `MetagameGUI.cpp:1677–1706`), so two runs of the
original differ the same way — or animates on the wall clock. The same random
stream also decides where a new campaign's gold sites appear, which is why a new
game can put them elsewhere on each run (once they came out identical); comparing
later campaign steps therefore loads the same saved campaign in both builds. The
original registers Conquest saves in `UserSavesConquest.rte/Index.ini`, which
saving rewrites (`MetagameGUI.cpp:1128`), so that file travels with the save.

**The camera's resting point depends on frame rate — in the original.** Each
update moves the camera a fraction of the remaining distance proportional to the
real time since the last update, and `CameraMan::SetOffset` floors the result
(`CameraMan.cpp:45`), so once a step is under a pixel the camera stops short of
its target. Controlling the tutorial's brain (target 696.5, offset target 264.5),
the camera rests at 253 in the port (60 fps) and at 194–195 in the original run
uncapped at several hundred frames a second — a 60 px difference that looked like
a port bug. The Lua console showed every input to the formula identical (actor,
view point, aim, scroll target, occlusion 0, screen 864×558, scene 2110×840).
With the original paced to 60 fps by the injector (`CORTEX_INPUT_FPS`, now the
default of `run-original.sh`, standing in for VSync, which a locked screen stalls)
it rests at 255: the last 2 px depend on whether one frame near the end ran long
enough to allow one more whole-pixel step.

Both builds also reacted identically to input that missed its target (a click on
the logo did nothing, one between two menu entries opened Settings), and the
Video Settings screen differs only where the port hides resolution and fullscreen
by design ([display](display.md)).

### On screen: identical except where the simulation has drifted

`Parity Screen` at step 300, 864×558 at scale 1.5, camera pinned, the port on
headless Chrome's **Metal** backend (the GPU a player's Chrome uses; see
[testing](testing.md)). Pixels differing, of 482,112:

| pixels | missions |
| --- | --- |
| **0** | Harvester, Skirmish Defense, Wave Defense, Survival, Massacre (build phase, object picker included), Keepie Uppie; the Scene Editor's default new scene (measured on an earlier build) |
| 9–59 | Maginot Defence 9, Dummy Assault 28, One-Man Army (Diggers Only) 33, Brain vs Brain 42, both Zero-G variants 42, Bunker Breach 57, One-Man Army 59 — the brain turret's aim, one actor's feet |
| 192–1,529 | Decision Day 192, Signal Hunt 1,529 — actors that have moved, their HUD markers, blinking icons |
| 9,304 | Exploration — its crabs and the dropped brain are mid-walk |

(The final build — JSPI, native exceptions, the frame clock from the refresh —
measured 2026-09-24; earlier builds gave the same picture with slightly different
counts, because the drifted positions differ from run to run.)

Everything left is either simulation state (drifted positions, AI aim) or a
real-time animation: `Actor::DrawHUD` blinks the team icon with
`m_HeartBeat.AlternateReal`, so its phase depends on the wall clock at capture.
The build-phase screens show the placement cursor, whose transparency alternates
every 333 ms of wall-clock time (`SceneEditorGUI`, `m_BlinkTimer.AlternateReal`):
after the port's frame time dropped, Harvester and Massacre caught the other
phase and differed in 349 pixels, every one of them the cursor's disc.
Terrain, backdrops, bunkers, HUD text, fonts, the object picker and the palette
are pixel-identical to the untouched original.

What had to be controlled to get there: the camera (pinned; build-phase screen
occlusion compensated; shake cancelled — earlier runs differed by 32–37% from a
glide the picker's occlusion started, and Decision Day by exactly one pixel of
camera shake) and the renderer (SwiftShader draws false seams).

### Endings: the same rules decide the same way

`Parity Defeat` gibs every human player's brain at step 60 and records the
activity's state and winner until the activity ends (or step 600). Every mission
where both builds produced a record ends the same way:

| mission | at step 60 the brain is | result in both builds |
| --- | --- | --- |
| Dummy Assault | the Brain Case | over at step 61, team 1 wins |
| Bunker Breach | the Commander | over at step 119, team 1 wins |
| One-Man Army (all four variants) | the one soldier | over at step 60, team 1 wins |
| Brain vs Brain | an Imperatus brain robot | over, team 1 wins — step 112 vs 65, because the script checks only every 2 s of *real* time (`winTimer:IsPastRealMS(2000)`) |
| Signal Hunt | the rocket still carrying it | over, team 1 wins — step 96 vs 121, because the brain thrown out of the gibbed rocket dies when the (drifted) physics says so |
| Decision Day | a brain robot | winner set to team 1 at step 62, activity keeps running |
| Maginot Defence | the Brain Case | activity keeps running |
| Exploration | still the drop crate | activity keeps running |
| Harvester, Skirmish Defense, Wave Defense, Survival, Massacre | not placed yet (build phase, state 2) | nothing happens |
| Keepie Uppie | none — no actors at all by step 60 (running) | nothing happens |

`Parity Victory` gibs every actor not on a human team (doors excepted) at step 60.
Twelve of the seventeen records are byte-identical: Dummy Assault ends at step 60
and Bunker Breach at step 119 with the human team the winner; Exploration, Signal
Hunt, Maginot Defence, One-Man Army (Zero-G) and (Diggers, 0-G) keep running;
the build-phase activities and Keepie Uppie have nothing to gib. The other five
reach the same result by a different route: Brain vs Brain ends with team 0 the
winner at step 114 in the original and 62 in the port (its real-time 2 s win
check again); Decision Day and One-Man Army (Diggers Only, and on Ketanot Hills)
are still running at step 600 with one more or one fewer reinforcement on the
board, which follows the drifted simulation.

The probe gibs from an *ordinary* global-script update. From the late update it
hung the original in Bunker Breach and the One-Man Army missions on an assertion
dialog — an upstream race described in [lua](lua.md).

### Over time: the same game, not the same bits

Dummy Assault, 900 steps (15 s of play), default settings:

| comparison | differing lines of 203 |
| --- | --- |
| original run 2 vs run 3 | 0 |
| original run 1 vs run 2 | 1 (one Dummy, step 900) |
| port vs original | 92, from step 30 |
| port vs original rebuilt with `-ffp-contract=off` | 30, from step 10 |

Actor and item counts (28 and 5) and the terrain sample agree at every
checkpoint; the handful of live particles (0–3) differs at two. What differs is
positions and velocities. The first difference, at
step 10, is the last digit of one robot's angular velocity; a few steps later
standing actors' vertical velocities differ outright, because foot contact
decisions amplify last-bit differences. There are four causes, and none can be
removed from the port without making it less like the original: two in floating
point, two in timing.

**1. Fused multiply-add.** GCC contracts `a*b + c` into one `fmadd` on arm64 by
default (`-ffp-contract=fast`, even with `-std=c++20`). WebAssembly has no scalar
FMA; the port compiles with `-ffp-contract=off`, rounding twice. Rebuilding the
original with contraction off cuts the difference from 92 lines to 30, which
isolates it. Matching the original would mean fusing exactly the expressions
GCC's `widening_mul` pass fuses after inlining and optimisation, through a
software `fma` — not something a source change or compiler flag can express.

**2. Apple's float maths library.** The original calls Apple's `sinf`, `cosf`,
`atan2f` etc. They are not correctly rounded, and they match no open-source
implementation. On 2 million inputs:

| function | Apple ≠ correctly rounded | Apple ≠ musl float | port's choice |
| --- | --- | --- | --- |
| sin | 1.4% | 1.4% | double, rounded |
| cos | 1.2% | 1.2% | double, rounded |
| tan | 7.0% | 20.1% | double, rounded |
| atan2 | 5.8% | 17.0% | double, rounded |
| asin | 1.6% | 4.9% | double, rounded |
| acos | 1.1% | 8.8% | double, rounded |
| atan | 6.3% | 8.4% | double, rounded |
| exp | 0.6% | 0.6% | musl float (equally close) |
| pow | 0.6% | 0.6% | musl float (equally close) |

The port's `FloatMath.h` computes float trig in double and rounds once (the
correctly rounded column), which is the closest available to Apple for every
function measured. Getting closer means reproducing Apple's closed-source
implementation; that is out of scope.

The same holds between the original's own platforms: its Windows and Linux builds
link different maths libraries (MSVC's CRT, glibc) and are compiled by compilers
that contract differently, so they cannot follow the Mac build's trajectories
either.

The original's own simulation also depends on frame rate. Paced to 60 frames a
second by the injector (`ORIGINAL_FPS=60` in `run-original.sh`), it differs from
itself uncapped in 21 of 176 timeline lines over 900 steps of Dummy Assault; the
per-frame camera draw from the random generator and the camera-dependent code are
the likely paths. Pacing does not bring it closer to the port (96 lines, against 98
uncapped; without contraction 40 against 35), so contraction stays the main cause.

**3. Scheduling.** Threaded script draws and the parallel sight rays race on the
global generator in the original, and the port does the same; see
[threads](threads.md). The port's scripts within one state run in unique-ID
order where the original's run in heap-address order.

Asynchronous pathfinding adds to it: a path computed on the background pool
reaches the actor or script that asked for it at a scheduling-dependent step.

**4. Frame rate.** `CameraMan::Update` runs once per screen for every *drawn*
frame and, whenever the activity is `Running`, draws the screen-shake angle with
`RandomNormalNum()` from the global generator — even when the shake magnitude is
zero. The game loop draws once per pass whether or not a simulation step ran. So
the simulation's random stream advances with the display's frame rate, and any
two runs whose frame pacing differs (vsync jitter, a 120 Hz display, a headless
run without vsync) draw different values from then on. This is the original's
design; the port keeps it.

## Where the port deliberately behaves differently

Everything else in `engine/PORT-CHANGES.patch` is platform plumbing (WebGL,
the audio layer, IndexedDB, waiting and yielding, diagnostics) or restructuring
with the same behaviour. These change what a player or a script can observe:

| difference | why |
| --- | --- |
| Video settings offer only the scale; the resolution is the window's; no fullscreen toggle in the game (the page's Ctrl+F, or the browser's own fullscreen, resizes the game instead) | the user's decision; see [display](display.md) |
| outside fullscreen, Ctrl+F opens fullscreen and never reaches the game: in player one's default controls that is crouch and pick up | the user's choice of key; see [display](display.md) |
| settings are written to `Settings.ini` as they change, not when the player leaves the Settings or Mod Manager screen | the user's rule ([specs/settings.md](../specs/settings.md)): a page can be closed at any moment; see [files and saves](files-and-saves.md) |
| a `Settings.ini` without `BrowserSettingsVersion` starts at the default 2x scale, whatever scale it holds | the user's rule: the old 1x default and scales the Video settings saved by themselves are not choices; see [display](display.md) |
| Conquest's DONE Designing Base returns to the campaign map | upstream pauses into the pause menu there (`BaseEditor::Update` calls a plain `PauseActivity()`), whose Resume returns to the editor that Done has just made inactive, where no click works; its own comment says "Quit to metagame view" |
| in a scenario, the pause menu starts with Restart Scenario, then a gap, then the back button grouped with the rest | the user's request ([specs/pause-menu.md](../specs/pause-menu.md)). Restart runs as the Scenario screen's start does (`ActivityMan::RestartActivity`) and clears the pause's music muffling; not offered in Conquest or the editors, which keep upstream's layout. The port lays the buttons out itself (`PauseMenuGUI::LayOutButtons`), centred where upstream's column was. Upstream restarts only with RAlt+R |
| once a scenario is lost, Esc, Space or Enter opens a menu of Restart Scenario, Load Game and Exit to Menu | the user's request. Upstream's Esc went straight to the Scenario screen, and Space ended the Activity only once "Press [SPACE] or [START] to continue!" showed, five seconds after the loss; a loss is a game Activity outside a campaign, over, with no human player on the winning team (`ActivityMan::ScenarioLost`). Load Game cannot save, and a game loaded from it is played at once, as that menu has no Resume |
| no online play | removed at the user's request |
| Exit (main menu) and Quit Program (Conquest's game menu) return to the page's start screen | the user's decision: a page cannot close its tab. Saves are written to IndexedDB first, then the page reloads (`BrowserReturnToStartScreen`, `index.html`'s `onExit`). The loop stops before drawing another frame: natively that frame, drawn on Conquest's map where no scene is loaded, trips `CameraMan::CheckOffset`'s assertion |
| the orbiting station is drawn centred on its position | the user's request. The original rotates the sprite about its top-left corner (raylib's `DrawTextureEx`) placed on the station's position, so it floats half a sprite away from the point Conquest's "TradeStar Midas" income line and circle aim at |
| float trigonometry through `FloatMath.h`, no fused multiply-add | GCC's contraction and Apple's float libm cannot be reproduced in WebAssembly; see [randomness](randomness.md) |
| `LuaStateWrapper::m_RegisteredMOs` iterates by unique ID | the original iterates heap addresses; see [lua](lua.md) |
| RAlt+W world dumps and Alt+W scene preview dumps save an image | in the original both **crash the game** (segfault in `IMG_SavePNG`, observed with scripted input): it passes a 3-byte pitch where SDL wants the row stride, and sends the 32-bit preview through the indexed saver |
| sounds are ready as soon as they are created | real FMOD decodes every sound on a background thread after startup (`FMOD_NONBLOCKING`); on this Mac that ends about 4 s after launch, when loading does. A mission started at that instant (`LaunchIntoActivity`) finds its first music choices not ready: the original prints "Could not update sound properties … not ready", skips them and picks again — which draws from the global random generator. A player coming through the menus never meets it; its length depends on the machine, so it is not emulated |
| Lua `AHuman:EquipShieldInBGArm()` without an argument | the original binds it through a cast to a function of another signature, which traps in WebAssembly; the port passes the declared default, `false` |
| assertion boxes are a browser `confirm()` (OK aborts, Cancel ignores) | SDL has no multi-button message box in the browser |
| the loading splash is not tiled across very wide windows | at upstream resolutions it never tiled; a wide browser window showed the word three times |
| on a first visit, a sound played before its file has downloaded starts when the file arrives; an Activity waits until every file is there | the sounds download after the game has loaded (see [files and saves](files-and-saves.md)); in practice this is the menu's music, a second or so late |
| a sound that fails to play is not positioned | `AudioMan::PlaySoundContainer` calls `UpdatePositionalEffectsForSoundChannel` even when `playSound` or the calls after it failed, with whatever the stack held as the channel, and writes through it. Real FMOD almost never fails there; the port's FMOD refused a play when all 1,024 channels were in use (it now steals one, as FMOD does), and One-Man Army in the simulation harness trapped with "memory access out of bounds" — a garbage pointer that lands inside the heap corrupts it silently instead |
| Conquest's base design shows the ground around the editing player's own units — the resident brain and the humans and robots placed as blueprints — as far as each would see in a battle; a brain that was never placed is put on the ground when the editor opens | the user's request. An expedition to an empty site is settled on the map (`MetagameGUI::AutoResolveOffensive`): the site is never loaded and its brain is left at (-1, -1), for the next battle to drop somewhere random in the player's quarter of the site (`MetaFight.lua`). Base design then opened all black, the fog never having been lifted, with nothing of the player's on the map to lift it. The editor now puts that brain where the battle looks, at the first spot out from the middle of that quarter with 25 px of air above the ground at five points (`BaseEditor::GroundSpotForBrain`), and sweeps each unit's sight all the way round (`BaseEditor::RevealAroundOwnUnits`; `Actor::CastSeeRays`' reach and ray strength, no random numbers). The brain's spot and the revealed fog are saved with the site like any the player makes, so the next battle starts with the brain there |
| building in-game (Skirmish Defense and other build phases), "(Re)Move Object" takes back what was placed in this build phase, refunding it and restoring the terrain | the user's request: the original ships that slice disabled and cannot remove anything placed in-game. See [editors](editors.md) |
| an unhandled exception or a fatal error on a worker thread writes no AbortSave, AbortScreen or AbortLog | the original writes them from whichever thread failed: the save reads a scene the engine thread is still changing, and the screen and hardware report call GL on a thread without the context. In the browser that crashed again ("function signature mismatch" in `Scene::SaveSceneObject`), re-entered the terminate handler until the stack overflowed, and buried the first error. The port prints the error first, stops at a second failure, and shows the same message box |

Four earlier differences were made to match the original again, since a player
could observe them and none prevents a crash: F12 prints the original's "ERROR:
Unable to save bitmap" after a successful dump (it checks `IMG_SavePNG(...) == 0`
against SDL3's boolean); one text event keeps at most 32 bytes; the Scene Editor
rebuilds its texture only when both dimensions change (the port changes
resolution only between activities, so this never differs); Command no longer
stands in for Control in text fields.

Dumping a scene or loading one without an Activity (`-dump-scene-preview`, the
harness) is allowed; the original asserts. Only the port's own tools do that.

## What "identical" can mean here

Established against the untouched original: the same generated world and random
streams at the start of all 17 scripted activities; the same picture wherever the
simulation agrees (0 differing pixels on seven screens, including a build-phase
GUI and the editor's default scene); the same endings when brains or enemies are
destroyed; and the same object and terrain evolution over the first 15 s of a
battle by actor and item counts and terrain samples.

Not achievable: bit-identical trajectories over time. The original's own play
depends on its compiler's multiply-add fusion, Apple's float maths, thread
scheduling, background pathfinding and the display's frame rate, and it does not
repeat itself exactly either.

Not yet compared: anything after 900 steps, and missions played to a *natural*
victory or defeat against the original (the probes force them). The campaign's
screens, a campaign battle's start and the tutorial are compared through scripted
input (above); a battle played on is not, since each run's random stream and the
camera's timing differ by then.
