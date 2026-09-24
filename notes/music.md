# Music sequencing

Updated 2026-09-24.

Entry points: `engine/Source/Managers/MusicMan.cpp` / `.h`,
`AudioMan.cpp`, `engine/Source/Entities/SoundContainer.cpp`,
the `DynamicSong` and `DynamicSongSection` presets,
`engine/Source/Menus/TitleScreen.cpp` (menu music transitions).

## Music is not a separate player

The single most useful fact here: **`MusicMan` has no audio backend of its own.**
It sequences ordinary `SoundContainer`s, and `AudioMan` routes them to the music
channel group. Everything that applies to sounds — bus routing, volume, mute,
effects, pitch, the FMOD compatibility layer — applies to music unchanged. See
[audio](audio.md).

## Dynamic songs

A `DynamicSong` holds selectable **section types**, with a default section
fallback, and each section has transition and regular sound-container choices.

`PlayDynamicSong` clones the preset, selects the section and the upcoming
container, and optionally starts immediately. Changing section can either wait
for the current musical boundary or transition immediately.

`CyclePlayingSoundContainers` is where the sequencing lives. It **retains the
previous container while the next starts**, which is what allows overlapping
tails and crossfades. The next boundary is:

```
current container's exit time (or its sound length if exit time is zero)
  − next container's pre-entry time
  clamped to ≥ 0
```

**Real-time timers drive cycling and delayed fades.** A smooth premature
transition fades over the upcoming pre-entry; the alternative schedules a short
fade at that point. `EndDynamicMusic` disables further sequencing and may fade
the current sound. `ResetMusicState` stops retained containers and clears
sequencing state.

## Interrupting music

Intro, menu and script requests go through interrupting music: it clones and
plays a **separate** `SoundContainer`, pauses the existing dynamic containers,
and records elapsed sequencing time. Ending the interruption stops it, unpauses
the retained containers, and **extends the sequence deadline by the interruption
duration**.

> Pause must preserve playback position, not simulate stop and restart. A
> container that restarts from zero on resume is a bug, not a rounding issue.

## Verification status

`MusicMan` is the original's code unchanged, and `AudioMan` is the original's
minus its dormant multiplayer event queue (removed with networking on
2026-09-24). Music plays in the browser build — the port's audio
diagnostics (`?perf-debug`) report the intro music on the mixer at peaks of about
0.08–0.16 — but nobody has compared overlaps, fades or an interrupted song's
resume against the native game by ear or by measurement.
