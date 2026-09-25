# Terrain and destruction

Updated 2026-09-24.

Entry points: `engine/Source/Entities/SLTerrain.h` / `.cpp`,
`engine/Source/Managers/SceneMan.cpp`,
`engine/Source/Managers/MovableMan.cpp` (MOID layer),
`engine/Source/Main.cpp` (simulation ordering).

## Three layers, three different meanings

`SLTerrain` keeps **three** bitmaps, and conflating them is the classic mistake:

| layer | member | what a pixel means |
| --- | --- | --- |
| material | `m_MainBitmap` | a **material ID** — what the pixel *is* physically |
| foreground colour | FG bitmap | a **palette index** — what it looks like |
| background colour | BG bitmap | appearance behind the foreground |

Removing a visible foreground pixel is **not** the same as removing its material:
the hole looks dug but still stops bullets. Background colour is independent of
both.

A fourth, unrelated layer holds **MOIDs**. `MovableMan::UpdateDrawMOIDs` clears
old drawings, rebuilds the index and draws actors, items and particles with
`g_DrawMOID`. Those IDs are not material IDs and not palette indices, and the
MOID index is rebuilt every frame (which is why anything reading it must wait for
that task; see [threads](threads.md)).

## Destruction

`CleanAir` scans the material bitmap, converts cavity material to air, and masks
foreground colour wherever material is air. `CleanAirBox` does the same inside a
box, with bounds checks and wrapping adjustments. **Neither touches background
colour.**

`EraseSilhouette` transforms a sprite into a temporary bitmap and tests its
non-transparent pixels against terrain. It handles scene bounds and wrapping,
clears intersected material to air, and clears foreground colour to the mask
colour. It can return dislodged `MOPixel` objects — sampled, with a maximum count
— and a material may name a different spawn material, in which case the particle
uses *that* material's density. **The caller receives the returned pointers;
`EraseSilhouette` does not insert them into `MovableMan` itself.**

It appends an approximate, unwrapped affected box to `m_UpdatedMaterialAreas`.
Upstream's own comment says the fit could be better. That list covers deposits
and unwrapped boxes — **it is not a complete dirty-region journal** and must not
be used as one.

## Writes bypass the setters

This is the most important thing to know before instrumenting terrain.

`SLTerrain` exposes `SetFGColorPixel`, `SetBGColorPixel` and `SetMaterialPixel`,
**and also hands out mutable `BITMAP*` pointers**. Cleanup and silhouette erasure
write directly through bitmap operations, and so do external callers. Hooking
only the setters misses most real mutation.

Related source wart: the bodies of `GetFGColorBitmap` and `GetBGColorBitmap`
place `SetUpdated()` *after* an unconditional `return`, so it is unreachable.
This is **not** currently gating anything — `SceneLayerImpl::Draw` updates the
visible regions of owned non-static layers on every draw regardless (in the
browser by comparing them with what the GPU already holds, see
[rendering](rendering.md)) — but any future use of layer update flags must audit
their real producers first.

## Simulation timing

The main loop may run several fixed simulation steps before drawing one frame.
Each step: input updates → wait for queued MOID drawings → activity
and scene update → movable objects → audio, music, late global scripts.
`MovableMan` also schedules actor visibility rays and the next MOID drawing task
on workers.

Consequences for anything that wants to observe terrain (a probe script, a
test hook, a dump):

- Reading **before** the simulation, or before late scripts, misses changes made
  later in the same step.
- Reading **after** those stages still needs a worker-synchronisation argument.
  A main-thread hook alone does not prove every terrain writer has finished.
  (Inspection found the overlapping actor-visibility jobs use the unseen map and
  MOID drawing uses the MOID layer, not terrain FG/BG — an audit result, not a
  guarantee for new work.)

## Scripts write terrain too

`GATutorial` draws animated instructional screens and room signs **directly into
the terrain BG bitmap**, at screen origins including (673,688), (961,688),
(817,688), (961,592), (961,424), (961,316) and (1201,316). These are legitimate
gameplay writes, not static background art — a test that marks background pixels
and expects them to survive will fight the activity itself.

## Open

- Broader auditing of script and worker terrain mutation would be useful before
  anything relies on terrain being stable within a step.

See [rendering](rendering.md) for how these layers reach the GPU. (The port once
replicated terrain to network clients; that was removed with the rest of
networking on 2026-09-24.)
