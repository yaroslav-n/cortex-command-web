# Editors

Updated 2026-09-25.

Entry points: `engine/Source/Activities/SceneEditor.cpp` (activity level),
`engine/Source/Menus/SceneEditorGUI.cpp` (placement),
`engine/Source/Managers/PresetMan.cpp` (`GetFullModulePath`, userdata
modules), `runtime/include/BrowserEditorSaveCheck.h`.

The Scene Editor, Conquest's base design and in-game building (Skirmish Defense's
build phase) have been exercised in the browser. The Actor, Gib, Area and Assembly
editors compile and are reachable from the menu but have never been used.

## Two state machines

`SceneEditor` (an Activity) owns the activity-level dialogs, scene loading and
saving, and the dirty flag. `SceneEditorGUI` owns object picking, placement and
the pie menu. They are **distinct state machines**, and the coupling is worth
knowing:

- an activity dialog makes the placement GUI inactive;
- entering `EDITINGOBJECT` chooses `ADDINGOBJECT` or `PICKINGOBJECT` depending on
  whether the cursor already holds an object.

`SceneEditor::Update` updates `PLACEONLOAD` objects, updates the placement GUI,
then accumulates `EditMade` into `m_NeedSave`.

The GUI has four modes: `ONLOADEDIT`, `BLUEPRINTEDIT`, `AIPLANEDIT` and
`INGAMEEDIT`. Drawing selects the corresponding scene object list; blueprint and
AI-plan modes also draw the existing objects as context. Cursor positioning uses
scene bounds and camera targeting, and the held actor is disabled and inactive
while being edited.

## Building in-game, and taking things back out

Activities with a build phase — Skirmish Defense, Survival, and any other
`GameActivity` that starts in `Editing` — use the same GUI in `INGAMEEDIT` mode,
which places straight into the scene: a module is drawn onto the terrain's three
layers (`TerrainObject::PlaceOnTerrain`) and its copy deleted, a unit or item goes
into `MovableMan`, and the team pays at once. The original offers no way back: the
in-game pie menu's "(Re)Move Object" slice is `Enabled = 0` in
`Base.rte/GUIs/PieMenus/PieMenus.ini`, and the remove mode only searches the
blueprint list, which in-game building never fills.

The port enables that slice (`SceneEditorGUI::SetFeatureSet`) and keeps a record of
each placement in the current build phase (`m_InGamePlacements`):

- a module is stamped by `StampInGame`, which does what `PlaceOnTerrain` and
  `SceneMan::AddSceneObject` do for it and its child objects — a door frame's door
  actor, a turret, a ladder's pieces — but first copies the terrain under each
  piece it draws (material, background and foreground, wrapping as the terrain
  does), and records the unique IDs of the child actors and items;
- a unit, an item or a deployment records the unique ID of what it put into
  `MovableMan`; an item handed to a unit placed in this phase adds its cost to that
  unit's record, so they are refunded together.

In remove mode, holding the button over one of those blinks it white (units through
their `g_DrawWhite`; `TerrainObject::Draw` gained the same mode) and releasing takes
it out: its actors and items are deleted, its cost refunded, and the terrain is
restored. Later placements may have been drawn over it, so every stamp from the
newest down to this one is undone in reverse, and the later ones are copied and
drawn again (`RemoveInGamePlacement`). Actors and items are picked before modules,
newest first; an actor also within 20 px, as the blueprint editor does.

The records are dropped when the battle starts (`GameActivity::UpdateEditing`, the
switch to `Running`), so a later build phase — `DebugFunctions.lua` can re-enter one —
never restores terrain copied before a battle. What cannot be taken back: items
handed to the brain or to units not placed in this phase, and anything placed
before the phase began.

Checked on Skirmish Defense (Maginot Mission): a Green Dummy removed (1955 → 2000 oz);
two overlapping Closed Tombs over the bunker's edge, then the lower one and the upper
one removed — funds back to 2000 and the material under them hashing the same as
before (2124521404 over 161×141 px), the picture showing the dirt and the bunker
wall again; two Door A removed with their door actors (12 → 10 `ADoor`s).

## The drawing surface

`SceneEditorGUI::Draw` returns immediately for `DONEEDITING`. Otherwise it clears
an **owned Allegro bitmap**, draws placed and current objects into it, uploads it
through `BigTexture` and composites. The picker and pie menu also draw using the
target bitmap.

The backing bitmap is rebuilt, as in the original, only when **both** width and
height differ from the target's — the original's `&&`, which would reuse a stale
surface after a one-axis resize. It cannot matter here: the port applies a new
window size only between activities (see [display](display.md)), so an editor's
target never changes size while it runs. The port had "fixed" the condition and
went back to the original's to match it.

## Scene files and where they land

`SceneEditor::SaveScene` updates the preset name and asks `PresetMan` to register
it. Then:

| module kind | files written |
| --- | --- |
| user scenes | `<name>.ini` and `<name>.preview.png` directly under the user scenes module |
| other modules | the same under `Scenes/`, plus an `IncludeFile` appended to `Scenes.ini`, plus an `Index.ini` include if `Scenes.ini` did not exist |

A rejected preset registration opens the overwrite dialog. Writer creation
failure produces an error and returns failure. Preview and module-include failure
paths deserve end-to-end inspection.

### Browser durability

`PresetMan::GetFullModulePath` routes **userdata modules** to
`System::GetUserdataDirectory`, and `UserScenes.rte` is one of `PresetMan`'s
declared userdata modules. `/Userdata` is an IDBFS mount, restored before `main`
through a run dependency. So user scenes are routed to persistent storage. Missing
`UserScenes.rte` is created with directory scanning enabled, and userdata modules
load **after** ordinary modules so scene references resolve.

Two consequences that are easy to get wrong:

- **`SaveScene` returning true means serialization succeeded, not that an
  IndexedDB transaction committed.** The save button closes its dialog and clears
  `m_NeedSave` on that return value. There is no per-scene durable-save
  acknowledgement in the UI.
- **Edits to official modules resolve into the `Data` tree, which is outside the
  IDBFS mounts.** Do not promise those survive a reload just because `SaveScene`
  succeeded.

See [files and saves](files-and-saves.md).

## The persistence bug this editor exposed

The editor was how the stale-instance data loss was found, and it is a good
cautionary tale.

A scene saved by one tab, validated by SHA-256, and confirmed committed to
IndexedDB was **absent from a fresh runtime's preset registry**. The files were
genuinely gone from IndexedDB, while `UserScenes.rte` folder scanning was still
enabled — so it was missing files, not a preset lookup problem.

Cause: Emscripten's bundled IDBFS reconciliation deletes destination entries
missing from the source, so an **older tab that changed nothing** deleted the
newer tab's scene on its next five-second sync. Fixed by the per-file persistence
journal; see [files and saves](files-and-saves.md) for the mechanism.

After the fix, the full-game round trip passes: Main Menu → Game Editors → Scene
Editor → Create New → Build creates the default Alezer Canyon scene, the
production serializer saves it, and a **separate fresh game instance** finds the
preset registered with INI and PNG matching the originals byte for byte by
length and SHA-256. The restored scene also **loads through the editor UI** —
selected from the Load Scene list, it displays terrain, backgrounds and the
normal object picker.

### The diagnostics that did it

`?editor-save-check` runs after the New Scene Build action: requires
`UserScenes.rte`, generates a unique `BrowserEditorCheck<timestamp>` name, calls
the real `SceneEditor::SaveScene`, verifies non-empty INI with `AddScene` and a
valid PNG signature, hashes both with SHA-256, then **explicitly waits for
`FS.syncfs(false)`** before storing the expected manifest in `localStorage`.

`?editor-save-restore-check` runs in a fresh runtime after filesystem restore and
module loading, requires the scene in `PresetMan`, and verifies restored lengths
and hashes, showing failure in a visible overlay.

These bypass only pie-menu navigation — they use the actual serializer,
filesystem and startup module loader. They leave test scene files in browser
storage and do not overwrite an existing name.

## The black sky is the original's

A new scene built with the editor's defaults (Alezer Canyon terrain; Default
Front, Clouds Layer A and Editor Background backdrops) shows a grey grid strip at
the top of the world and **black** everywhere below it, with the clouds floating
on black. That looks broken and was reported as such, but it is exactly what the
original draws: `Editor Background` is `Base.rte/Scenes/Backdrops/Static/Grid.png`
with `WrapY = 0`, so it covers only its own height, and nothing is drawn beneath
it. Verified against the untouched original with the parity probe: the same scene
(`ParityProbe.rte/ParityScenes.ini`, hosted by a do-nothing scripted activity
because the editor runs no global scripts), photographed with the camera pinned
at 2520,1310, is **pixel-identical** in both builds — 0 of 482,112 pixels differ.

## The object list

The port's object list (the category list and items shown by every editor and by
in-game building, `ObjectPickerGUI`) departs from upstream in two ways, both set by the
user. See [specs/editors.md](../specs/editors.md).

### Fewer categories

Upstream lists every group that holds something placeable: 36 in in-game building and
37 in the editors, many of them subcategories such as Actors - Heavy or Tools - Diggers.
The port lists 28 (29 in the editors, with Assemblies - Schemes), modelled on Build 31,
the Data Realms game.

It changes only what the list shows. The game's groups stay exactly as upstream has them,
because other code picks items by them: AI deliveries take troops from Actors - Light
and crates from Craft - Crates (`DeliveryCreationHandler.lua`, `SkirmishDefense.lua`), and
the buy menu's Mecha tab is Actors - Mecha and Actors - Turrets. So `ObjectPickerGUI.cpp`,
under `__EMSCRIPTEN__`, maps groups to categories. `c_GroupsListedElsewhere` names the
groups listed inside another category (Actors - Heavy in Actors, Bunker Clutter in
Bunker Backgrounds) or under another name (Craft - Crates as Crates, Actors - Wildlife
as Wildlife). A category shows the items of all its groups, each once, in load order
(`PresetMan::GetAllOfGroups`), and is judged by upstream's rules over those items. Groups
not named there, a mod's included, are listed as they are.

Checked against the data before changing it: every item of the subcategories no longer
listed is also in the parent category, except 41 decorations only in Bunker Clutter
(lights, small crates, Dummy racks, Ronin furniture) and the Crab, only in Actors -
Wildlife. Hence the merge into Bunker Backgrounds and the Wildlife category.

Assemblies - Passages, - Prefabs and - Rooms come from the assembly schemes'
`AssemblyGroup`, and every assembly is in exactly one of them. A group holding nothing
but assemblies is listed only if `Settings.ini` names it in a `VisibleAssemblyGroup`
line. Build 31's engine names these three by default, and upstream's names none, so upstream
never shows them. The port lists them from `c_ListedAssemblyGroups`.

### No Actor Spawners

Upstream's development code adds a **Generic Actor Spawner** (March 2024, part of the
Browncoat mission work): a scripted marker for mission makers, buyable at 200 oz and alone
in a new category, Actor Spawners. That category sorted first, and the list selects its
first category when it opens, so the list opened on it. The released game (v6.2.2,
February 2024) has neither. The port sets the spawner's `Buyable = 0`, which removes it
and its category from the list; Actors is first again and the list opens on it, as in
the release.

It is hidden rather than left unloaded. Reading a preset takes a unique ID
(`Serializable::CreateSerializable` ends in `MovableObject::Create`), so not loading it
would give every object loaded or spawned after it an ID one lower than the original's.
That changes the recorded simulation hashes in `tools/run-checks.mjs`, which fold in
every object's ID, and every id the Parity Timeline prints. Hidden, it keeps loading
identical. Nothing that is switched on uses it: Refinery Assault, the mission that places
copies of it, is commented out of `Browncoats.rte/Index.ini` in both versions.

The list's other differences from v6.2.2, all from the same mission work, are left as
upstream has them: two scripted consoles and an item dispenser demo; blast doors, vault
doors and Coalition bunker cannons that v6.2.2 shipped switched off; one background
piece; the MG-85 Manbreaker, which is also new in the battle buy menu; and seven bare
door motors that upstream took out of the list.

## Scene creation is slow, not broken

An early investigation recorded black screenshots after pressing Build and nearly
concluded scene creation was failing. Instrumenting it showed the truth: terrain
and background cloning finish immediately, **`SceneMan::LoadScene` takes about
10.6 seconds**, and GUI reconstruction finishes ~20 ms later, after which terrain,
backdrops and the object picker are all visible.

Browser-only stderr stage markers in `SceneEditor::Update` report *Build
requested*, *Terrain cloned*, *Loading scene*, *Scene loaded / rebuilding GUI*,
*Build completed*. They are diagnostics, not errors, despite the browser logger
classifying stderr as error. Note that *Scene loaded* means `LoadScene`
**returned** — the existing handler does not check its return value.

## Input notes specific to the editor

Short clicks in the editor used to highlight a button without activating it, and
a left-button drag inside the button was needed. That was the render-versus-
consumption defect, now fixed: normal single clicks activate Game Editors, Scene
Editor, Create New and Build. See [input](input.md).

**Do not use editor buttons as evidence about keyboard navigation.**
`GUIButton::OnKeyDown` is empty and `GUIManager` has no generic Tab traversal, so
Enter cannot activate a button by design. Adding that would be a shared UI
improvement, not a browser compatibility fix.

The pie menu is also easy to misread: the default mouse/WASD preset maps the
analog pie menu to **held right mouse**, `SceneEditorGUI` updates it only while
that state is held, and Save Scene is a downward slice. A brief right-click does
not select it.

## Not established

Arbitrary placed-object serialization round trip; interactive pie-menu Save
selection; crash durability and quota behaviour; loading restored terrain into a
*running* editor; one-axis resize; any native/browser comparison of editor
behaviour or of regenerated terrain pixels; every editor other than the Scene
Editor.
