# Rendering and player views

Updated 2026-09-24.

Entry points: `engine/Source/Managers/FrameMan.cpp`,
`PostProcessMan.cpp`, `WindowMan.cpp`, `CameraMan.cpp`,
`engine/Source/Renderer/` (`BigTexture.cpp`, `Shader.cpp`,
`BrowserShaderSource.h`, vendored `raylib/rlgl.c`).
Tests: `tests/frame_readback_contract.cpp`, `texture_tile_contract.cpp`,
`texture_gpu_contract.cpp`.

## The hybrid the whole subsystem is built around

Cortex Command is an old **indexed-colour, CPU-blitting** game that has had a GPU
renderer grafted on. Both halves are live at once, and almost every surprise in
this subsystem comes from the seam between them:

- **Allegro bitmaps** (a bundled, modified Allegro 4) hold terrain, sprites, the
  MO colour layer and the MOID layer. Terrain is **8-bit indexed** — a pixel is a
  palette index, and that index is also the *material* identity used by physics
  and destruction. See [terrain](terrain.md).
- **The GPU** (OpenGL 3.3 natively, **WebGL 2** in the browser, through a
  vendored raylib/rlgl) draws those bitmaps as textures, applies post-processing
  and composites the final image.

So indexed values must survive all the way to the fragment shader and be resolved
against a palette texture there — they cannot be converted to RGB early without
losing the material identity, and converting late is what makes the palette
lookup correctness issue below matter.

## Frame flow

```
FrameMan::Draw
  ├─ scene layers, actors, primitives, HUD   → Allegro bitmaps + GPU targets
  ├─ PostProcessMan                          → effects on the scene target
  └─ per-screen DrawPlayerView               → one pass per local (split) screen
WindowMan::UploadFrame
  ├─ composite game image + GUI/console
  └─ present                                 → SDL_GL_SwapWindow / BrowserWaitForFrame
```

`DrawPlayerView` draws one screen. It was factored out when the port also drew
views for remote players; that is gone with networking (2026-09-24), and the
function stays because the local path through it is the one verified
pixel-identical to the original. Legacy HUD code asks for *default screen
dimensions*, so it publishes the active screen's size for the duration of the
draw and restores it on exit — a no-op for local screens. `CameraMan` is the
original's code again: `SetScroll` centres on the whole window even in split
screen, an upstream quirk the smooth scroll then corrects.

## Browser adaptation

### Context and shaders

SDL creates a **WebGL 2** context, and **without an alpha channel**
(`SDL_GL_ALPHA_SIZE = 0` in `WindowMan::Initialize`, so the canvas has
`alpha: false`). A native window ignores the alpha of the final pass; a WebGL
canvas with alpha hands it to the page compositor. That matters because
`ScreenBlit.frag` treats any non-black GUI pixel as solid (`guiSolid`) yet writes
the GUI's own alpha — and Allegro's palette expansion (`draw_sprite` of an
indexed image into a 32-bit bitmap) leaves alpha 0. Chrome drew those pixels as
holes, so scene previews came out black. With the opaque canvas the original
`ScenarioGUI` code shows them exactly through the game palette (13,594 of 13,600
Tutorial Bunker preview pixels exact; the rest are under the panel frame).
Never re-enable canvas alpha to get transparency for something; nothing native
has it.

The engine's shaders are written for GLSL
330 core, so `Shader.cpp` pipes every source through
`RTE::BrowserShaderSource` (`Renderer/BrowserShaderSource.h`), which:

- drops anything before `#version`;
- rewrites `#version 330 core` to `#version 300 es` plus `precision highp float;`
  and `precision highp int;`;
- strips `#extension` lines;
- removes default initialisers from uniform declarations (`uniform float x = 1.0;`
  is legal in desktop GLSL, not in ES);
- strips the `f` suffix from float literals.

This is a textual translation, deliberately minimal. If a shader starts using a
construct ES 3.00 lacks, this is where it has to be handled — and note the
translation runs at **runtime on the shader text**, so the file itself stays
GLSL 330 for the native build.

> **Shaders are bundled at link time.** `Data` is packaged into the Wasm build by
> `--preload-file`, so editing a `.frag` does nothing until the target relinks.
> `CMakeLists.txt` lists the shader files in `LINK_DEPENDS` on `cortex` for
> exactly this reason. An earlier shader fix appeared to have no effect for this
> reason alone.

### Palette lookups must fetch an exact entry

`Background.frag` and `Dissolve.frag` used to resolve an indexed pixel by
sampling the 256×1 palette texture through `textureAA`, a helper intended for
*magnifying content textures*. On a palette it is undefined twice over:

- the V coordinate is the constant `0.0`, so `fwidth` on it is zero and the
  division inside `textureAA` produces NaN for every fragment;
- the U coordinate is constant wherever the index does not change between
  neighbouring fragments, so the same division collapses it onto a texel
  boundary, and a boundary sample with nearest filtering may return **either**
  adjacent entry.

Adjacent palette entries belong to unrelated colour ramps, so a flat region
dithered between two indices rendered speckled with a colour from somewhere else
entirely. The Tutorial Bunker sky showed full-width bands of dark navy speckles
along the parallax ridges and on cloud sprites; the source art has no such bands.

Both shaders now use `texelFetch`, which cannot depend on filtering or
derivatives:

```glsl
vec4 paletteEntry(float index) {
    return texelFetch(rtePalette, ivec2(int(clamp(index, 0.0, 1.0) * 255.0 + 0.5), 0), 0);
}
```

**Any new palette lookup must do the same.** This removed undefined behaviour
rather than working around a driver, so it applies to the native build too.

### Presentation and suspension

`WindowMan::UploadFrame` composites into a **separate framebuffer** in the
browser (`m_BrowserCompositeBuffer`), because WebGL forbids sampling a texture
that is attached to the current framebuffer — the scene texture is both input and
output of the composite step otherwise.

Presentation suspends the C++ stack (`BrowserWaitForFrame`, see [threads](threads.md)), so
the visible buffer is cleared **immediately before the final blit and never
before a cooperative wait**. Clearing earlier leaves a blank frame on screen for
however long the suspension lasts. `SDL_GL_RETAINED_BACKING` is requested so the
last completed image survives until explicitly replaced.

Every swap goes through `WindowMan`'s static `PresentWindow`, which skips
presentation entirely in scene-dump mode — a run that only writes a PNG has
nothing to show, and on a machine with no awake display the Cocoa vsync wait
never returns. See [testing](testing.md).

## BigTexture: bitmaps larger than a texture

A full scene bitmap can exceed `GL_MAX_TEXTURE_SIZE`, so `BigTexture` splits it
into tiles of **half** that size and draws them individually.

`SceneLayerImpl::Draw` calls `UpdateTargetRegion` for owned, non-static bitmap
layers on every draw, which computes the visible source region and calls
`BigTexture::Update`. Dynamic terrain edits therefore reach the GPU without
depending on `m_MainBitmapUpdated`. `StaticSceneLayer` explicitly promises its
bitmap never changes after the initial upload. (The `SetUpdated` calls after
`return` in `SLTerrain`'s FG/BG getters are unreachable dead code, but they are
not the cause of anything.)

### Tile coordinate mapping

`Draw` used to pass a whole-bitmap intersection straight to each tile as its
source rectangle, so tiles in later rows and columns sampled coordinates outside
their own extent, and cropped rectangles wrongly kept their whole-bitmap origin
in destination placement.

`TextureTileMapping` is the shared helper that does it correctly: intersect the
requested source with the tile **in bitmap coordinates**, subtract the tile
origin for texture sampling, and subtract the requested source origin before
scaling into destination coordinates. Covered by geometry contracts for
second-row/column tiles, crops inside and across tiles, unequal scaling, partial
edge tiles, non-intersection and zero source width.

### GL state hygiene — the recurring trap

**Pixel-unpack settings are context state, not texture properties.** `BigTexture`
stages tightly packed rows, so a caller's non-zero `GL_UNPACK_ROW_LENGTH`,
`GL_UNPACK_SKIP_ROWS` or `GL_UNPACK_SKIP_PIXELS` must not affect its upload — and
it must not clobber the caller's bindings either. `Update` previously set only
alignment and overwrote the caller's texture and unpack-buffer bindings.

Worse, **construction** calls `rlLoadTexture`, which binds textures and changes
alignment. A null data pointer *with a pixel-unpack buffer still bound* is
interpreted as an offset into that buffer rather than an allocation with no
source data — so merely creating a texture could fail depending on what the
renderer did previously.

Construction uses `ScopedTextureUnpackState`, an RAII guard that saves six state
values, sets a tightly packed layout, and restores bindings and layout on exit
(including during C++ unwinding), and explicitly unbinds the unpack buffer before
allocating.

`Update` runs several times a frame, and the same guard cost six WebGL queries
each time — a few percent of the main thread. In the browser it now sets the
packed layout (alignment 1, row length and skips 0, no unpack buffer) without
asking what was there, and leaves that layout behind with no texture bound,
which is the state the original's `Update` leaves. Neither changes the active
texture unit.

> When adding any upload path, assume the caller left the GL pixel-store state in
> an arbitrary condition. Restore it only where that is cheap or rare: a query is
> a round trip through the browser's WebGL binding every time.

### Texture format must follow the bitmap

`BigTexture` computed the correct `PixelFormat` for its metadata but **hard-coded
grayscale when allocating the actual texture**, so 32-bit RGBA content rendered
black. `SceneEditorGUI` creates its backing bitmap at the target's colour depth,
and `SceneLayer` wraps supplied bitmaps, so callers genuinely do supply both
depths — allocation must use the computed format.

The destructor deletes its upload-buffer objects as well as its textures; it used
to leak the buffers. The browser path uses CPU staging and never maps them, so it
does not allocate them at all.

## Image dumps

Three dump paths exist and all three were broken in ways worth remembering,
because each produced a plausible-looking success:

| dump | what it writes |
| --- | --- |
| `ScreenDump` | the presented frame |
| `WorldDump` (`-dump-scene-world`) | the whole terrain at scene resolution |
| `ScenePreviewDump` (`-dump-scene-preview`) | the 170×80 preview |

The bugs were: the IDBFS mount spelled `/Screenshots` while the engine's constant
is `ScreenShots/` (case-sensitive filesystem, everything vanished on reload); the
savers passed **one pixel's size** where `SDL_CreateSurfaceFrom` wants the **row
stride in bytes**; both call sites tested the SDL3 return against `0`, which is
SDL2's success value and SDL3's *failure* value; the preview went through the
indexed saver while its buffer is `c_BPP` (32-bit), so pixels were read as
palette indices and produced a striped image a quarter of the intended width; and
`DrawWorldDump` cleared the preview background with `makecol32(255, 0, 255)`,
assuming stock Allegro's magenta mask, while the bundled custom Allegro defines
`MASK_COLOR_32` as `0x00000000` — so `masked_stretch_blit` painted magenta over
the sky.

All five are in shared engine code and affected the native build identically.

## Parity status

Native and browser rendering of scenes is **byte identical**:

- all **47** mission scene previews, 0 of 13,600 pixels differing on each;
- **11 full terrain dumps** up to 5040×2620 — 81,396,616 pixels, none differing,
  matching PNG file sizes;
- the terrain left after 3600 simulated steps of threaded gameplay, weapon damage
  included, byte identical at 2110×840.

Getting there required cross-build determinism work unrelated to rendering; see
[randomness](randomness.md). Comparisons against the **shipped** `*.preview.png`
files can never reach 100% — those came from a build whose standard library
mapped the random stream onto ranges differently, so terrain debris is placed
differently. Compare against a freshly built native binary, not against `Data`.

Tooling: `tools/compare_png.py` (indexed, RGB or RGBA) reports differing
pixel counts and channel deltas. See [testing](testing.md) for the dump workflow.

## What the GPU contracts establish

`texture_gpu_contract` constructs the real `BigTexture` from an 11×9 bitmap with
the tile size forced to 4 px — nine textures including partial right and bottom
edges — uploads known distinct pixels, draws through `BigTexture::Draw` into an
RGBA framebuffer, flushes the real render batch and reads back every destination
pixel. It runs for **both** 8-bit indexed and 32-bit RGBA, checks surrounding
clear pixels, performs partial updates across tile boundaries, and verifies
`glIsBuffer`/`glIsTexture` are false after destruction. It also binds an
unrelated texture on unit 3 and an unrelated unpack buffer with awkward
pixel-store state: construction must restore all six values plus the active unit,
and a partial update must upload correctly regardless and leave the packed
layout with nothing bound on the unchanged unit.

`frame_readback_contract` verifies exact RGBA and alpha, orientation and GL state
restoration for framebuffer readback, which is what the screenshot function
(`FrameMan::SaveScreenToBitmap`) uses.

## Synchronous GL calls

Most WebGL calls are queued for the GPU process, but a query (`glGetError`,
`glGetIntegerv`) has to answer now: `glGetError` waits for the GPU process
(about 0.6 ms each on the Metal instance), and `glGetIntegerv` goes through the
binding for every call. Natively both are cheap, and upstream checks for errors
after every GL call (`GL_CHECK`). In the browser:

- `GL_CHECK` (`GLCheck.h`) only checks after each call with `?gl-check`, for
  locating a GL error;
- otherwise `UploadFrame` checks once every 120 frames. GL errors stay latched
  until read, so none is lost, only attributed less precisely;
- ImGui's draw data is only submitted when it has command lists;
- `BigTexture::Update` makes no queries (above).

Together with the waiting change in [threads](threads.md) this took Dummy Assault
at 1920×1080, scale 2, on the Metal instance from 15.2 to 4.6 ms of engine time
per frame. `cc.sh profile <ms>` samples the page's main thread and lists the
functions that took the most time, which is how these were found; the main
thread is now idle about three quarters of each frame.

## Known gaps

- **Draw costs quoted above come from software WebGL** (SwiftShader in headless
  Chrome). On the Metal backend (`CORTEX_ANGLE=metal`, see [testing](testing.md))
  missions run at 60 fps with under 5 ms of engine work per frame; nothing has
  been measured on a slower GPU or a Windows machine.
- **Context loss recovery** is unimplemented and untested.
- The **native mapped-buffer** update path uses some pixel offsets as byte
  offsets — its source X offset omits multiplication by bytes per pixel. The
  browser CPU staging path multiplies correctly. This needs a native upload
  regression before anyone calls the native path verified.
- Local split-screen fog behaviour is unverified.
