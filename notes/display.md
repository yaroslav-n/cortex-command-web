# Display size and scale

Updated 2026-09-24.

Entry points: `engine/Source/Managers/WindowMan.cpp` / `.h`
(`ComputeWindowResolution`, `AdoptWindowResolution`, `SetResolutionMultiplier`,
`ApplyPendingWindowResolution`, `GetLetterboxOffset`),
`engine/Source/Menus/SettingsVideoGUI.cpp` (`ConfigureForBrowser`),
`engine/Source/Managers/ActivityMan.cpp` (`StartActivity`),
`engine/Source/Managers/UInputMan.cpp` (mouse motion),
`engine/Source/Managers/SettingsMan.cpp`, `site/index.html`.

## The rule

**The game's resolution is always the browser window divided by the scale.** The
player never picks a resolution; the only sizing choice is the scale (1x, 1.5x,
2x…). There are no windowed/fullscreen toggles, no preset or custom resolutions
and no multi-display option in the browser — `SettingsVideoGUI::ConfigureForBrowser`
hides them in both the main-menu and pause-menu layouts and relabels the heading
"- Scale:". Choosing a scale applies immediately; there is no Apply button.

Upstream picks a resolution and letterboxes it into whatever window it gets. In a
browser the page owns the window size, so a separately chosen resolution could
only ever disagree with it.

## How the window size reaches the engine

1. `index.html` gives the canvas an explicit CSS size: it fills `#game`, which
   covers the page above a 50 px strip (links, and a note that Ctrl+F opens
   fullscreen). So the game's "window" is the browser window less 50 px; in
   fullscreen it is the screen.
2. SDL3's Emscripten backend treats a **resizable** window whose canvas has an
   external CSS size as following that size: at creation it sizes the canvas
   backing store from the CSS size, and on every browser resize it re-reads it and
   sends `SDL_EVENT_WINDOW_RESIZED`. Without an explicit CSS size SDL imposes its
   own size instead — which is what the page used to do, with `object-fit:contain`
   letterboxing it.
3. `WindowMan::Initialize` calls `ComputeWindowResolution` right after
   `CreatePrimaryWindow` and **before** `InitializeOpenGL`, so the back buffers and
   everything FrameMan builds later are sized correctly from the start.
4. `SDL_EVENT_WINDOW_RESIZED` calls `AdoptWindowResolution`, which recomputes and,
   if the result changed, rebuilds FrameMan's back buffers, PostProcessMan's GL
   buffers, the viewport and the back buffer texture, and sets
   `m_ResolutionChanged` so the menu loop re-creates the menus.

`ComputeWindowResolution` rounds **down** so the scaled image never exceeds the
canvas, then clamps to the engine's minimum (`c_MinResX` 640, `c_MinResY` 360). A
canvas too small for the minimum at the chosen scale letterboxes rather than
rendering an unusable GUI.

Each change prints `Browser resolution: WxH at scale S, from the canvas` or
`… following a resize` to the console.

## Two multipliers, on purpose

`m_ResMultiplier` is **not** the player's scale. `SetViewportLetterboxed` rewrites
it on every call with whatever fits the image to the window, and mouse mapping
(`UInputMan`, `GUIInputWrapper`) divides by it, so it must stay the *fitted* value.
Using it as the preference too made the scale drift (1.5 → 1.5006) after one
resize.

So the browser keeps `m_PreferredScale` — the player's choice — separately.
`ComputeWindowResolution` uses it, the settings list selects it, and
`SettingsMan` saves **it** as `ResolutionMultiplier` under Emscripten.
`ResolutionX`/`ResolutionY` are still written but ignored on load.

### The default is 2x

A player with no saved settings starts at **scale 2**
(`WindowMan::c_DefaultBrowserScale`; [the spec](../specs/settings.md)): the canvas
fills the browser window, and at 1x a typical window gives a game resolution so
large that the pixel art and text are tiny. Two places carry the default, and both
are needed: `WindowMan::Clear()` sets `m_ResMultiplier` (what the game starts with
when `Settings.ini` has no value), and `m_PreferredScale`'s member default is what
the *first* `Settings.ini` is written with — `SettingsMan` overwrites the new file
before the window exists. Miss the second and a fresh profile saves 1 and starts at
1 on its next visit. Verified in a fresh headless-Chrome profile: a 1600×900 canvas
gives 800×450 at scale 2, the saved file records `ResolutionMultiplier = 2.000000`,
and the reload starts at 2.

A scale is kept only once the player has chosen it. Every browser `Settings.ini` is
written with `BrowserSettingsVersion = 1`; a file without it gets the default scale
and is written again at boot (`SettingsMan::Initialize`). Its `ResolutionMultiplier`
is no choice: files from before the 2x default hold that old default, 1, and until
2026-09-24 the Video settings saved a scale by themselves (below). Such a file loses
its scale once, even one a player did choose then; nothing tells the two apart.

A window too short for the scale keeps it. Below 720 px of game area (a window under
770 px tall, with the strip) 2x would take the game under the engine's 640×360
minimum, so `ComputeWindowResolution` stops at the minimum and the image is fitted
into the canvas, about 1.6x with bars at the sides in a 583 px area; a taller window
gets true 2x again.

### The scale list

`PopulateResMultplierComboBox` offers the scales the canvas holds at the engine's
minimum resolution, in halves from 1x, and also 2x and the player's own scale where
it holds neither. The box shows the player's scale, `GetPreferredScale`. Closing the
list applies the box's text (`ApplySelectedScale`), and `SetResolutionMultiplier`
does nothing when that is the scale already chosen, so opening and closing the list
changes nothing.

It used to show `GetResMultiplier`, the fitted scale, and to offer only what the
canvas held. In a window too short for 2x the box read "1.62x" and the list held 1x
and 1.5x: merely opening and closing it saved 1.62 as the player's scale, and 2x
could not be chosen again. The strip made such windows common.

The scale list sits in the custom-resolution box, which the browser always shows.
Its layout height, 80, covered "Enable VSync" below it, which then took no clicks; in
the browser the box is only as tall as the list's closed box, 40, except while the
list is open (`c_BrowserScaleBoxHeight`). VSync matters here:
`BrowserWaitForFrame` paces on animation frames with it and on timers without.

## Resolution must not change under a live Activity

Upstream never changes resolution while an Activity is Running or Editing: its
settings screen asks for confirmation and then calls `g_ActivityMan.EndActivity()`
first, and the **menu loop** is the only place that rebuilds what depends on
resolution. Scene-layer scroll ratios and scale factors, cameras and every
Activity GUI are sized once, when they are created.

A browser window can change size at any moment, and changing resolution under a
live Activity breaks it — the Scene Editor's object picker stayed laid out for the
old width and was cut off by the new one.

So `AdoptWindowResolution` checks `ActivityBlocksResolutionChange()` (an Activity
exists and is Running or Editing — a paused Activity is still Running). If it
does, it keeps the current resolution, fits its image to the new window with
`SetViewportLetterboxed`, and sets `m_WindowResolutionPending`. The pending change
is applied by `ApplyPendingWindowResolution` from two places:

- **the menu loop**, when no Activity is live — upstream's own place for it;
- **`ActivityMan::StartActivity`**, right after the new Activity is cloned and
  before `SetupPlayers`/`Start` — the old Activity is gone and nothing of the new
  one is sized yet. Without this a player who always has an Activity (pausing
  keeps it alive; starting another replaces it) would stay letterboxed forever.

A scale change made from the pause menu is held the same way, and says so in the
console.

**Nor under a campaign.** Conquest's map (`MetagameGUI`) is laid out once, for the
resolution it is built at, and the menu loop can only rebuild it from scratch
(`MenuMan::Reinitialize`), which loses the campaign's place: going fullscreen on
the map dropped the player to the main menu, and re-entering showed "Round 1" with
the panels back where a new game puts them. It also keeps the scene of a battle in
progress, whose result it takes when the battle ends. Upstream only changes
resolution from its settings, rarely mid-campaign; a browser window changes size
whenever its player resizes it or goes fullscreen. So `AdoptWindowResolution` also
holds a change of the *window* while `g_MetaMan.GameInProgress()` — fitting the
image to the window meanwhile, as under an Activity — until the campaign is over.
A scale chosen in the settings still applies at once, as upstream's resolution
changes do, with upstream's consequences for a campaign.

**Where the rebuilt main menu opens.** Upstream reopens its settings screen after a
resolution change (`MainMenuGUI`'s constructor), since only the settings make one.
In the browser that happens only after a scale change
(`ResolutionChangeCameFromSettings`); after a resize or fullscreen the main menu
stays on its main screen.

## The strip under the game, and fullscreen

The page keeps a 50 px black strip under the game (`#bar` in `site/index.html`),
laid out as [the spec](../specs/page.md) says: on the left "Open fullscreen by
pressing Ctrl + F"; on the right three links separated by bullets, each with an icon
and a faint underline: "Cortex Command Web" and "Cortex Command Community Project"
with GitHub icons, to this port's repository and the original's, and "Submit a bug"
with a bug icon (both icons Octicons), which opens a new issue on this repository. Below 700 px
the Ctrl+F note is hidden and the rest stays on the right; below 600 px the strip's
text is smaller. The game's area (`#game`, the canvas and the start screen) is the
window less the strip.

Ctrl+F puts `#game` alone on the screen, so the strip is not shown, and Esc leaves
it: Chrome takes Esc in fullscreen for itself, before the page sees it (in headless
Chrome, which has no such handling, the key reaches the game instead). The game's
resolution follows the new size as for any resize, or is fitted to it under an
Activity or a campaign (above).

- SDL registers a `fullscreenchange` listener on the document and takes any
  fullscreen on the page for its own window going fullscreen
  (`Emscripten_HandleFullscreenChange`); it then ignores resizes, so the game kept
  its old size. The page stops the event at `#game`; SDL sees only the resize.
- Ctrl+F is the page's only outside fullscreen. Its listener captures on the window,
  so it runs before SDL's, which is on the window too but not capturing; it keeps
  the key from the browser's find and stops it, so the game never sees the F, nor
  the F's auto-repeats, the first of which SDL would take for a press. In fullscreen
  the key is left to the game, where it means something: player one's default
  controls (`PresetMouseWASDKeys`) make Left Ctrl crouch and F pick up. In the
  window, crouching and picking up therefore opens fullscreen instead.
- F is the key that types f, or, on a layout whose letters are not Latin, the key
  in F's place (`KeyF`). Alt and Meta must be up: Cmd+F on a Mac stays the
  browser's find, and AltGr+F, which Windows reports with Ctrl and Alt, is left alone.
- Where the page cannot go fullscreen (`document.fullscreenEnabled` is false, as in
  a frame without `allowfullscreen`) the note is hidden and Ctrl+F is the browser's.
  Below 700 px the note is hidden too; Ctrl+F still works there.
- A mouse button released over the strip still reaches the game — SDL listens for
  releases on the whole document — at the game's last cursor position.

## Mouse mapping while letterboxed

Upstream converts a window position to game coordinates by dividing by the
multiplier, and never subtracts the letterbox bars — it rarely letterboxes. The
browser now letterboxes whenever the window changes under a live Activity, and
every click then landed a bar's height too low (a click on *Game Editors* hit
*Exit*).

`UInputMan`'s mouse-motion handler subtracts `WindowMan::GetLetterboxOffset()`
under Emscripten. The offset is derived from `m_PrimaryWindowViewport`, which is a
**GL rectangle measured from the bottom** of the window.

> **Assign the corrected position; do not subtract from both mice.** When the
> event's mouse id is 0, `mouse` and `m_MouseStates[0]` are the same object, and
> subtracting from each applied the offset twice — clicks then landed a bar's
> height too **high**.

## The alert trap

Under Emscripten, SDL's simple message box is a plain `alert()`, and in headless
Chrome an open JavaScript dialog blocks the page until DevTools dismisses it. Every
`RTEError::ShowMessageBox` therefore freezes the headless test browser — with
DevTools, input and even `Debugger.pause` unanswered, since the renderer is waiting
outside JavaScript.

`ValidateResolution` raised one at startup: it compares the saved multiplier with a
ceiling derived from the **screen**, and headless Chrome reports an 800×600 screen
(ceiling 1.25), so a saved 1.5x stopped the game before it had a window. It is
skipped in the browser, where the saved resolution is irrelevant anyway. Other
`ShowMessageBox` calls remain; in a real browser the player can dismiss them.

## Other things that assumed a fixed resolution

**The loading splash** is a single 1200×300 card with "LOADING" in the middle, and
its layer wrapped horizontally, tiling the whole card, word included. Upstream's
fixed resolutions rarely exceeded the ~2030 px at which a second word appears; a
wide browser window at 1x showed three. It no longer wraps: one card, centred,
cropped as before on narrower screens.

**Backdrops that end.** A non-wrapping `SLBackground` only fills its gap with an
edge colour when the *whole* bitmap is shorter than the view. The Scene Editor's
"Editor Background" grid is 1080 px tall, scrolls 1:1 and does not wrap
vertically, so below scene row 1080 the sky is black. `SLBackground::Draw` is
identical to the original's, so this is the original game's behaviour, left as is.
Tall windows at 1x show more of it.

## Verified

- Boot at 1280×633, 1600×900, 2000×1030 and 3200×700: resolution equals the canvas
  divided by the scale, the image fills the window edge to edge.
- Scale 1x → 1.5x from Settings applies instantly; the list offers only scales the
  canvas can hold (1.5x at 633 px tall, 2.5x at 900) and grows after a resize;
  the scale survives a reload.
- Resizing the window at the menu refits live, with correct mouse mapping.
- The strip: the game area is the window less 50 px (1280×583 in a 1280×633
  window, from the canvas). Fullscreen (then a button) at the main menu went to the
  headless screen's 800×600 and back to 1280×583, staying on the main screen; on
  Conquest's map it kept 1280×583 fitted into 800×600 (letterboxed) and then back,
  with the campaign's turn intact and clicks landing where they should.
- Ctrl+F, as DevTools key events: on the start screen `#game` went fullscreen. At
  the main menu, with three auto-repeats of F, the game went to 800×600, and a
  listener after SDL's saw Ctrl go down and F come up but no F go down; back in the
  window the game returned to 1280×583. Pressed in fullscreen, the F reached the
  game, with the browser's action prevented by SDL.
- Resizing under the Scene Editor keeps its layout intact (fitted, with bars),
  clicks land correctly in the fitted menu, and the held resolution applies when
  the next Activity starts.
- Loading screen at 2133 px wide: one card.
- The scale list (2026-09-24, fresh headless profile, 1280×583 game area): the game
  starts at 2x (640×360, fitted); the list offers 1x, 1.5x and 2x with 2x shown;
  opening and closing it leaves the scale and the file alone; choosing 1.5x applies
  at once and survives a reload. A file saved by the old build with 1.62 and no
  `BrowserSettingsVersion` starts at 2 and is rewritten with both. "Enable VSync"
  toggles, and its change is in the file within a second.

Not tested: devicePixelRatio > 1 (the canvas is not high-density, so the browser
upscales), and a resize during gameplay in a mission rather than an editor.
