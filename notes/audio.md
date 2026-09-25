# Audio

Updated 2026-09-25.

Entry points: `engine/Source/Managers/AudioMan.cpp`,
`engine/Source/Entities/SoundContainer.cpp`, the FMOD compatibility layer in
`runtime/fmod/` with its header `runtime/include/fmod/fmod.hpp`,
`runtime/miniaudio_backend.c`, and the effect kernels
`runtime/lowpass.hpp` / `limiter.hpp` / `compressor.hpp` / `volume_ramp.hpp`.
Tests: `tests/audio_contract.cpp` (the real mixer graph against native
fixtures), `audio_check.cpp`, `worklet_probe.c`.

`runtime/fmod/`, one file per part of the API:

| file | what it holds |
| --- | --- |
| `internal.hpp` | the state behind every API object, shared by the files below |
| `system.cpp` | `System`: the mixer, its settings, `playSound`, `update` (virtualisation, end callbacks) |
| `sound.cpp` | `Sound`: a file's path and properties; nothing is decoded until a channel plays it |
| `playback.cpp` | `PlaybackSource`, the data source a channel plays: a decoder, decoded samples, or the silence of a sound whose file is missing |
| `decoded.cpp` | `DecodedSounds`, the background decoder and its cache of short sounds |
| `channel.cpp` | `Channel` / `ChannelGroup`: volume, pitch, pan, fades, the per-channel fader, 3D |
| `dsp.cpp` | `DSP`: the lowpass, limiter and compressor nodes |
| `diagnostics.cpp` | the `?perf-debug` mixer report |

## The shape of the port

`AudioMan` owns all sound and music behaviour and talks to an **FMOD-shaped
API**. Natively that is real FMOD 2.02.13. In the browser, `runtime/fmod/`
implements the same API surface over **miniaudio** and its decoders.

Keeping the API shape is the whole strategy: `SoundContainer`, `AudioMan` and all
Lua-facing audio behaviour keep their existing call flow, and only the backend
changes. The compatibility layer covers sound and channel lifecycle, channel
groups, volume, pitch, pan, position, looping, fades, callbacks and DSP.

Invariants the layer has to honour, each of which was a real defect at some
point:

- **End of stream contributes its final sample exactly once**, and delivers
  exactly one completion callback. (miniaudio's pitch-capable resampler delays by
  one source frame, so `PlaybackSource` supplies a single zero lookahead frame at
  final EOF — and deliberately **not** at loop boundaries.)
- A finished audio node must be **detached and uninitialised before its storage
  is freed**.
- **Channel reuse must not inherit stale callbacks or state.**
- Group changes and DSP placement affect **every attached channel**, so testing
  individual sounds misses the interesting behaviour.

### The graph

```
PlaybackSource (ma_decoder or decoded samples, + loop counting)
  → ma_sound
    → ControlFader        per channel, atomic gain + 64-sample ramp
      → DSP effects       lowpass / limiter / compressor as ma_node
        → channel group   nested, each with its own fader
          → master group
            → ma_engine endpoint → device
```

`ControlFader`'s ramp mirrors native FMOD's automatic fader smoothing of **64
output samples**: a new target restarts from the next sample's gain, and
assigning the same target preserves it.

`System::update` also implements **virtualisation** — channels are ranked by
priority and everything beyond `softwareChannels` has its gain forced to zero
while remaining logically playing.

**Channel stealing.** `playSound` never fails because every channel is in use: like
FMOD it takes the least important channel (the highest priority number, and of
those the oldest), reports it ended through its callback first, and plays the new
sound there. It used to return `FMOD_ERR_CHANNEL_ALLOC`, which the engine did not
survive (see [parity](parity.md)): the simulation harness, which never calls
`System::update`, ran out of channels after 1,024 plays of One-Man Army. In play,
channels are recycled every frame; One-Man Army peaked at 119 in its first minute,
from the burst of sounds when it starts. `audio_contract` covers the stealing
order and the callbacks; `?perf-debug` reports the channels allocated and stolen.

## Where a playing sound's samples come from

Short sounds are decoded once and then played from memory; everything else is
decoded as it plays.

- **The first play of a sound** reads the file into memory on the thread that
  called `playSound` (never on the audio thread; see below) and plays through a
  `ma_decoder` over those bytes, decoding FLAC or Vorbis and resampling to
  48 kHz stereo as the mixer pulls. The compressed bytes are shared: `SoundState`
  keeps a `std::weak_ptr` to them, so concurrent plays hold one copy, released
  with the last channel.
- **If the sound is 10 seconds or shorter** (`DecodedSounds::c_MaxSeconds`),
  that play also queues it for `DecodedSounds`, which decodes the whole file on
  its own thread, through the same decoder configuration, into 48 kHz stereo
  float samples.
- **Every later play** finds those samples and plays them directly:
  `PlaybackSource` copies from memory, and the audio thread does no decoding or
  resampling for it. The samples are shared by every channel playing them and by
  the cache, so a sound evicted while playing finishes normally.

In the browser a sound's file may not be there yet when it plays: the page fetches
the sounds after the game has started. FMOD then creates the `Sound` from the
page's list (length and sample rate), plays silence until the file arrives, starts
the sound from its beginning when it does, and asks the page for that file first;
an Activity only starts once every file is here, or once the page has stopped
waiting for the last ones. The page then writes a marker in their place
(`c_SoundFileFailed`), and FMOD plays such a sound as silence exactly as long as
the sound, loops and pitch included, then ends it as it would have ended
(`PlaybackSource::InitializeSilent`, and `Fail` for a channel that was waiting): a
sound whose file could not be downloaded keeps its timing, which scripts see. A
sound that loops forever, which would never end anyway, keeps waiting for its file
instead, and starts when it arrives. See
[files and saves](files-and-saves.md).

Music and long ambience never enter the cache; they always decode as they play.
Guns, footsteps and impacts, which repeat constantly, are exactly the sounds the
cache holds. The cache is bounded at 192 MB (`c_BudgetBytes`) and evicts the least
recently played sounds beyond that; a minute of the Tutorial holds 26 sounds in
5.4 MB. A sound whose decode fails is remembered and never retried.

The decoded samples are **bit-identical** to what the decoder produces while
playing: `audio_contract` plays a sound until it ends, waits for the cache, plays
it again from memory, and requires every rendered sample to be equal — for the
22,050 Hz pistol FLAC looped once and for a generated 44,100 Hz mono WAV (so
resampling, upmixing and a loop seek are covered). The playback source keeps the
decoder's end-of-stream behaviour for decoded samples too: a read reaching the
end returns what remains, and only a read with nothing left reports the end.

`?perf-debug` reports the cache every two seconds, beside the mixer report:
`decoded sounds: 26, 5.4 MB; plays from memory 32, decoding as they play 30`.

## DSP fidelity

The effects are reimplementations measured against real FMOD, not guesses.
Native FMOD 2.02.13 was probed offline using the bundled macOS library and the
results checked into `tests/fixtures`:

| effect | result |
| --- | --- |
| multiband EQ lowpass | matches a Q=0.707 biquad; max sample error 6.5e-7 / 3.0e-8 / 2.4e-7 at 400 / 8000 / 22000 Hz |
| limiter | instantaneous attack, per-channel by default, native defaults release 10 ms, ceiling 0 dB, maximizer 0 dB, linked false; max error 1.4e-6 to 5.2e-5 |
| compressor | max error 4.9e-7 to 4.7e-6 across mono, unequal, group-pre/post and channel-pre/post placements |

The earlier browser lowpass used two one-pole stages, which is audibly wrong; the
biquad replaced it. Longer-release limiter cases still differ numerically — this
is parameter-specific evidence, **not** universal audible equivalence. Spatial
sound, resampling and pitch, scheduled fades, broad parameter combinations and
latency remain open.

`audio_contract` runs these against the **real production graph**, not a
kernel in isolation: it creates a float32 impulse WAV, decodes it through the
production `Sound` path, attaches the real effect node and compares both mixer
output channels. A known two-frame startup offset from miniaudio's pitch-capable
channel and master nodes is accounted for explicitly.

It also covers nested group pause: the real pistol FLAC through
child → parent → master, paused at active frame 4096 for 32 blocks, requires
every sample below 1e-7 during the pause (after one block of buffered tail) and
requires the resumed run to finish after exactly the same active duration —
40960 frames in both cases. Measured at 128-frame block resolution, so it is not
a sample-exact cursor comparison.

## No network sound

The original's `AudioMan` queued play, stop, pitch and fade events for network
clients whenever it was in multiplayer mode. That mode is gone with the rest of
networking (2026-09-24): `AudioMan` is the original's code minus that queue
(`SetMultiplayerMode`, `RegisterSoundEvent`, `GetSoundEvents`,
`ClearSoundEvents`, the `NetworkSound*` types), and `AudioMan::Update` is the
original's again, without the port's streaming-client pitch switch.

## Where the mixing happens

Audio now mixes on an **AudioWorklet** thread, through miniaudio's worklet
backend (`MA_ENABLE_AUDIO_WORKLETS` with `-sAUDIO_WORKLET=1 -sWASM_WORKERS=1`).
`CORTEX_AUDIO_WORKLET` in `CMakeLists.txt` switches back to the
ScriptProcessorNode path if the two ever need comparing again.

### Why the main thread was not good enough

miniaudio's default Web Audio path renders through a `ScriptProcessorNode`
callback on the page's main thread, so any long synchronous engine block stalls
playback. `BrowserCooperativeYield` (declared in `System.h`) was added to let the
browser event loop run during long loads — per line in `Reader`, between terrain
layer loads and procedural passes in `SLTerrain::LoadData`, around the placed
object loop and pathfinder reset in `Scene`, in the two pathfinding spin waits in
`Scene` and `SceneEditorGUI`, around scene teardown in `SceneMan::LoadScene`, and
around image decode and conversion in `ContentFile`. It is throttled to roughly
one display frame and compiles to nothing natively.

That took the worst callback lateness during a mission load from 1743 ms to
826 ms to 52 ms, and removed the multi-second freeze during data module loading.
But yields cannot fix this class of gap in general: any block longer than the
audio buffer produces one. A ScriptProcessorNode build measured across a Tutorial
load still shows 11 late callbacks with a worst lateness of 52 ms.

### Why the worklet used to emit silence

The backend was tried twice before and reverted twice, recorded as "links and
runs, device started, node connected, output exactly zero, no error". That
conclusion was right about the symptom and wrong about the cause: the backend was
never at fault.

Four measurements separated the possibilities, and each one is worth keeping:

1. `tests/worklet_probe.c` (target `worklet_probe`, page `worklet-probe.html`)
   drives a raw `ma_device` with a sine written by the data callback, under the
   engine's exact flags — worklet, Wasm Workers, the suspend mechanism (Asyncify at the time, JSPI now), `-pthread`, memory
   growth. Peak 0.25 at the destination, 375 callbacks per second. The backend
   works. Growing the heap by 256 MB mid-playback changes nothing.
2. The same probe through `ma_engine`'s node graph: also 0.25. The graph works.
3. `audio_check` decoding the real `PistolFire.flac` through `ma_engine`: peak
   1.10. Real assets work.
4. The game itself: engine clock advancing at exactly 48 kHz, one channel
   playing, unpaused, unmuted, every volume 1.0 — and mixer output exactly zero.

The fourth measurement needed the engine to report its own mixer, which it now
does: `ma_engine_config.onProcess` samples the peak on the audio thread, and
`?perf-debug` prints it along with the device state, the active channels, the
peaks either side of the per-channel fader, and each channel's playback cursor.
The cursor was frozen at 1115 frames while the fader ran 3800 times per two
seconds. The graph was running; the **data source** was producing nothing.

The playback source then wrapped a `ma_decoder` created with `ma_decoder_init_file`,
so every read reached the filesystem on whichever thread pulled the graph. In the browser
that filesystem is Emscripten's JavaScript glue, and **it does not exist on an
AudioWorklet thread** — a Wasm Worker has no such environment. Reads returned
nothing, silently. On the ScriptProcessorNode path the callback ran on the main
thread, where the filesystem does exist, which is exactly why that path worked.
`audio_check` worked for the same reason in reverse: `ma_engine_play_sound`
decodes the whole file into memory up front, so its audio thread only touches RAM.

### Starting the device from a worker

miniaudio's Web Audio backend creates the AudioContext and its worklet through
`window`, which only the page's thread has, and waits for the worklet with
`emscripten_sleep`. When the engine runs in a worker (`./build.sh --worker`),
`FMOD::System::init` therefore has the page's thread run `ma_engine_init`: it
calls the export `cortex_audio_start_engine` (listed in `JSPI_EXPORTS`, so the
page's thread may suspend in it) with `MAIN_THREAD_ASYNC_EM_ASM`, and the worker
waits on an atomic flag that the export sets. The engine owns its device exactly
as before, so nothing about the mix changes; the device's teardown goes back to
the page's thread the same way. On the page's thread the call is direct.

### The fix

`System::playSound` reads the file into memory on the calling thread and the
decoder reads those bytes (`ma_decoder_init_memory`); decoded sounds are plain
memory. The audio thread makes no host call at all, and anything added to it must
keep it that way. See "Where a playing sound's samples come from" above.

### Measured after the change

| | ScriptProcessorNode | AudioWorklet |
| --- | --- | --- |
| where it mixes | page main thread | own thread |
| late callbacks over a Tutorial load | 11, worst 52 ms | none possible |
| engine clock across the load | — | continuous, exactly 48 kHz |
| destination blocks non-silent | 1488 of 1502 | 803 of 811 |

The remaining silent blocks in both are the music transition when the activity
starts. The full `audio_contract` (native-fixture lowpass, limiter and compressor
comparisons, finite loop, callbacks, fades, teardown) passes on the worklet build,
and live play reports FPS 62, UPS 60, sim speed x1.00, Sound Channels 1/128 real.

Measurement tooling: `tools/cc.sh audio` reports peak, non-silent block ratio,
callback count and worst callback lateness; `audioreset` clears the counters. The
callback counters only exist for a ScriptProcessorNode; on the worklet build use
the non-silent ratio and the engine's own `?perf-debug` report. See
[testing](testing.md).
