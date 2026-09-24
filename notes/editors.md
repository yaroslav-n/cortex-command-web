# Editors

Updated 2026-09-22.

Entry points: `engine/Source/Activities/SceneEditor.cpp` (activity level),
`engine/Source/Menus/SceneEditorGUI.cpp` (placement),
`engine/Source/Managers/PresetMan.cpp` (`GetFullModulePath`, userdata
modules), `runtime/include/BrowserEditorSaveCheck.h`.

The Scene Editor is the only editor that has been exercised in the browser. The
Actor, Gib, Area and Assembly editors compile and are reachable from the menu but
have never been used.

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
