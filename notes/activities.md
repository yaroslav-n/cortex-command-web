# Activities and restart lifecycle

Updated 2026-09-24.

Entry points: `engine/Source/Managers/ActivityMan.cpp` / `.h`,
`engine/Source/Activities/` (`GameActivity`, `GATutorial`, `GAScripted`),
`engine/Data/Missions.rte/Activities/*.lua`.

## What an Activity is

An Activity is the game mode: the tutorial, a scenario battle, a campaign
offensive, an editor. It owns the win and loss conditions, player and team setup,
starting funds, spawning, and the objective display. Scenes are separate — an
Activity is started *against* a Scene.

Most shipped missions are `GAScripted`, meaning the real logic is a **Lua class**
in `Data/Missions.rte/Activities/`. The C++ side provides the framework and calls
into `StartActivity`, `UpdateActivity`, `OnSave`, `ResumeLoadedGame` and the late
global scripts.

## Two activity pointers, and why it matters

`ActivityMan` holds:

- **`m_StartActivity`** — the *configuration*, what a restart will use.
- **`m_Activity`** — the *running clone*.

`StartActivity(Activity*)` waits for priority and background tasks, **takes
ownership** of the supplied activity, clones it into `m_Activity`, resets music
state, sets up players and calls the clone's `Start()`. A failed start marks the
clone `HasError`. Success enters the activity, requests resume, clears post
effects and screen text, resets mouse magnitude, unpauses game sounds and resets
performance timings.

> **`StartActivity` deletes and re-sets the start activity, so it must be given a
> clone, not the stored pointer.** `RestartActivity` does exactly that, with its
> own comment saying so. Passing `GetStartActivity()` directly is a use-after-free
> — it aborts inside `Entity::Clone`.

The class/preset overload looks up and clones through `PresetMan`, and for a
`GameActivity` initialises starting gold before delegating. So changes made to the
*live* activity are not what a restart replays.

## Pause, resume, end

`PauseActivity` sets both the activity's pause flag and `ActivityMan`'s
in-activity flag, pauses game sounds and muffles music; unpausing ends
interrupting music. `ResumeActivity` rebuilds the human-player list for
multi-mouse and multi-keyboard handling, unpauses simulation and resets
performance timings.

`EndActivity` waits for both task pools and calls `End()`. **It does not clear
the stored activity pointer.** Do not assume ending an activity destroys its
scene or nulls anything.

`Update` dispatches the running activity's update with performance measurement.
`LateUpdateGlobalScripts` dispatches late global scripts, for `GAScripted` only.

## Restart ordering

`RestartActivity` is order-sensitive and the order is deliberate:

1. clear the restart request;
2. stop audio;
3. purge movable objects;
4. **reset `TimerMan`** — before creating anything, so new objects' timers use the
   new clock origin;
5. clone the saved start configuration, mark it `NotStarted`, route through
   `StartActivity` (or the default preset if there is no start configuration);
6. unpause simulation.

Failure leaves the game outside the activity and opens the console.

A general rule the restart order implies: **anything that must outlive a restart
cannot be timed against `TimerMan`**, whose origin and tick count reset inside
`RestartActivity` itself (step 4).

## A worked example: Dummy Assault

`Data/Missions.rte/Activities/DummyAssault.lua` with
`Missions.rte/MissionActivities.ini` and `Missions.rte/Scenes/Dummy Assault.ini`.

`StartActivity` resolves named scene areas, creates a spawn timer, computes a
difficulty-dependent reinforcement interval, and selects the CPU team and
technology. A new game starts battle music, sets funds, initialises fog, sets
scene enemies to sentry mode, finds the Dummy Controller and creates and assigns
player Brain Case actors. `UpdateActivity` maintains brain ownership, defeat
conditions, the enemy-controller objective and alarm-driven reinforcements.

`OnSave` explicitly stores `spawnTimer.ElapsedSimTimeMS` and `alarmTriggered`;
`ResumeLoadedGame` restores them and reacquires the CPU controller from the
loaded actors. **This is why saving terrain and actors alone is insufficient** —
mission state lives in the activity's own save hooks. See
[files and saves](files-and-saves.md).

## Evidence

- **Tutorial**: launches and runs; `-simulate-tutorial` drives it headlessly for
  thousands of steps, and the result is byte-identical between the threaded and
  serial browser builds.
- **Mission start matches the original**: the parity probe
  (`tools/parity-probe/`, see [testing](testing.md)) launched Exploration,
  Dummy Assault, Bunker Breach, Brain vs Brain, Decision Day, Maginot Defence,
  Harvester and both One-Man Army variants in the unmodified original and in the
  browser. Every material pixel of the generated terrain and 40 draws from the
  master Lua state were byte-identical in all nine. Signal Hunt and Refinery
  Assault crash the original when launched through `LaunchIntoActivity`, so they
  are unverified.
- **Dummy Assault**: launched through Scenario Battle with P1 on Attackers at
  Medium / 3000 funds; terrain, background, fog, brain and the Destroy objective
  rendered; saved through the pause menu; a **freshly initialised** game instance
  discovered and loaded that save and resumed with brain, fog, objective and funds
  intact.

## Conquest (the campaign)

The metagame's code is the original's, byte for byte: `MetaMan`, `MetaPlayer`,
`MetaSave`, `MetagameGUI`, `GAScripted` and `Base.rte/Activities/MetaFight.lua`.
What the port adds underneath is the platform: rendering, input, and the files a
campaign writes. Played through in the browser (Metal instance, 1296×837, one
human against one AI):

- Conquest → Continue to Conquest → New Campaign (defaults) → Start Game: "Day
  One", gold sites found, the site panel with its budget slider, the AI choosing
  its target, "Battle 1 of 1".
- The battle: an unscanned site starts entirely black (the unseen layer), the
  landing-zone selector, the brain rocket revealing the cave as it descends,
  Protect/Destroy markers. Left idle, the human's brain was killed by the AI: a
  natural defeat, "FAIL", then "Press [SPACE] or [START] to continue!".
- That key opens the pause menu, not the map. This is the original's behaviour:
  `MenuMan::HandleTransitionIntoMenuLoop` shows the pause menu whenever a
  metagame is in progress. "Back to Conquest Menu" returns to the map with the
  site now held by the AI and both players' funds and brain counts updated.
- Save Conquest wrote `AutoSave.ini` and the named save, each with the site's
  five layer images (`BG`, `FG`, `Mat`, `UST0`, `UST1`, 4.8 MB together), to
  `/Userdata/UserSavesConquest.rte/` in IndexedDB. After a page reload, Load
  Conquest listed both, and loading restored the map, funds, brains and owner.
- Day two: attacking the AI's site loaded the battle from the saved layers, with
  the fog revealed where the first battle revealed it.
- Base design on a site taken without a battle (two human players, so no AI
  competes for it; an expedition alone is settled on the map): the original opens
  the editor all black with the brain at (-1, -1). The port puts the brain on the
  ground (Rhias Forest, 4008×1460: at (501, 401), the middle of player one's
  quarter) and reveals what it sees — 653 px at 1280 px wide, perceptiveness 1;
  a Whitebot placed out in the dark revealed its valley up to the slopes on
  either side. Leaving and reopening the editor kept the brain there and the
  revealed ground. A deliberate difference; see [parity](parity.md).

Compared with the original through scripted input (`tools/native-input`, see
[parity](parity.md)): the Conquest notice, New Campaign dialog, Day One, Gold
Sites Found and Player 1's Turn, and — with the same saved campaign installed in
both — the loaded map, its day phases and a site panel. They differ only in
per-frame random glows and real-time animation. A new game's gold sites come from
a random stream the menus have already drawn from per frame, so they can land
elsewhere on each run, in either build.

## Upstream behaviour that looks like a port bug

**"Play Tutorial" restarts the last activity, not necessarily the tutorial.** The
button on the Conquest notice only calls `g_ActivityMan.SetRestartActivity()`
(`MainMenuGUI::HandleMetaGameNoticeInputEvents`, `MenuMan::Update`). In a fresh
session the start activity is the default from Settings (`GATutorial` / `Tutorial
Mission`), so the tutorial starts; after an editor or a scenario has run, the
same button restarts that instead — the Scene Editor's Load Scene dialog appears.
The original's code is identical; leave it.

## Not established

Missions played to victory or defeat through the probes end the same way as the
original's (see [parity](parity.md)); the campaign battle above ended in a natural
defeat. About 35 missions have never been launched in the browser, and no
campaign has been played to its end.

A note on a false lead: a guessed `B` buy-menu shortcut did nothing, but `B` is
the weapon auxiliary hotkey in the mouse/keyboard preset — that is not evidence
of a broken buy menu.
