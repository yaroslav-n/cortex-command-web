# Randomness and fidelity to the original

Updated 2026-09-23.

Entry points: `engine/Source/System/RandomGenerator.h`,
`engine/Source/System/RTETools.cpp` (`SeedRNG`),
`engine/Source/Entities/Activity.cpp` (`Activity::Start`),
`engine/Source/Managers/LuaMan.cpp` (state seeding),
`tests/random_contract.cpp`, `tools/parity-probe/`.

## How randomness flows

Everything random in the simulation — where terrain debris lands and which sprite,
flip and rotation it gets, weapon spread, gib velocities, AI, scripted content —
draws from one `std::mt19937`, `g_RandomGenerator`, seeded with a constant derived
from the string "Bubble".

It is seeded **twice**, and the second one is what makes the game reproducible:

1. `SeedRNG()` in `main`, at startup.
2. `SeedRNG()` at the top of **`Activity::Start()`** ("Reseed the RNG for
   determinism"), *before* `g_SceneMan.LoadScene()`.

So every mission starts from the same generator state regardless of what drew
from it earlier — menu time, window width, core count. The map a mission generates
depends only on how each draw is mapped onto a range.

Lua scripts have their own streams: each `LuaStateWrapper` owns a
`RandomGenerator m_RandomGenerator` (what `math.random`, `RangeRand`, `SelectRand`,
`PosRand` and `NormalRand` draw from), seeded at startup with
`RandomNum<uint64_t>(0, max)` from the global stream — two draws per state, master
first. These are **not** reseeded by `Activity::Start`.

## Reproducing the original's distributions

`std::mt19937` is specified exactly; `std::uniform_int_distribution` and
`std::uniform_real_distribution` are not. The original is built with GCC 13
(libstdc++), the browser with libc++, and the two map the same generator output
onto ranges differently — measured with the same seed, the first integers in
[0, 1000] were 699 565 797… for libstdc++ and 877 457 400… for libc++.

`RandomGenerator` therefore reproduces **libstdc++'s algorithms by hand**, taken
from the GCC 13 headers the original is built with:

- **Integers** (`bits/uniform_int_dist.h`). For a range narrower than 32 bits:
  Lemire's multiply-and-shift (`_S_nd`) — `product = draw × range` in 64 bits; if
  the low half is below `range`, reject while it is below `(2³² − range) mod range`;
  return the high half. A range exactly 32 bits wide takes one draw as is. Wider
  ranges combine a recursive draw for the high part (× 2³²) with one draw for the
  low part, retrying on overflow — so a full 64-bit draw is high word first.
- **Reals** (`generate_canonical` in `bits/random.tcc`). For `float`, one draw:
  `float(u) / 2³²`. For `double`, two: `(double(u₁) + double(u₂) × 2³²) / 2⁶⁴`.
  A result that rounded up to 1 becomes `nextafter(1, 0)`. The distribution then
  returns `canonical × (b − a) + a`.

The public methods are the original's, unchanged apart from the distribution —
including the `nextafter(max − min, …)` upper bounds, the swap when `max < min`,
and the passthrough quirk where `RandomNum<int>()` with no arguments resolves to
the float overload and truncates.

`tests/random_contract.cpp` contains a verbatim copy of the original's class
using the real `std::` distributions. Compiled natively with g++-13 at the
original's `-O3`, and with the port's class compiled to Wasm, the two produce
identical results for twenty call shapes (all the engine uses, plus wide and
degenerate ranges) over 20,000 interleaved rounds — 400,000 draws. Integers are
hashed at a fixed 64 bits because `unsigned long` is 4 bytes in wasm32 and 8
natively.

## Verified against the untouched original

`tools/parity-probe/` compares the port with the **unmodified** original game,
without changing its code. The probe is a content mod: a global script whose
`StartScript` — which runs inside `GAScripted::Start`, after the reseed and the
Scene load, before any simulation step — reads every terrain pixel with
`SceneMan:GetTerrMatter` and writes the map, plus a run of the master Lua state's
`SelectRand`/`PosRand` draws, with `io.open`. Settings launch straight into a
mission (`LaunchIntoActivity`). The original then calls `os.exit`; the browser's
Lua 5.1 has no `jit` global, which is how the script tells them apart and keeps the
browser running long enough to persist the file. See [testing](testing.md).

Every scripted mission that launches this way produces **byte-identical** output
in both — terrain materials and the Lua stream:

| mission | scene | size |
| --- | --- | --- |
| Exploration | First Signs | 1656×768 |
| Dummy Assault | Dummy Assault | 4000×1800 |
| Bunker Breach | Zekarra Mining Outpost | 3984×1152 |
| Brain vs Brain | Fredeleig Bunkers | 4008×1410 |
| Decision Day | Decision Day | 8160×2304 |
| Maginot Defence | Maginot Mission | 3072×1608 |
| Harvester | Ketanot Hills | 3600×1570 |
| One-Man Army (Diggers Only) | Fredeleig Plains | 4008×1410 |
| One-Man Army (Zero-G) | Zero-G Battle | 1920×1080 |

About 55.8 million material pixels. These maps are full of randomness — scattered
gold, plants, fossils and boulders, each placed, chosen, flipped and rotated from
the stream, plus topsoil and grass frosting — so a single differing draw would
show. *Signal Hunt* and *Refinery Assault* crash the **original** when launched
this way (a segfault in `FrameMan::Draw`), probably because they expect setup the
scenario menu normally provides; they are unverified, not failing.

## Arithmetic

Two further port changes make the port's native and browser builds agree
bit-for-bit, and are not what the original does:

- Both port builds compile with **`-ffp-contract=off`**. GCC otherwise fuses
  `a * b + c` into a single rounding on ARM64; WebAssembly has no scalar fused
  multiply-add and must round twice. (The original's release build, `-O3` with
  GCC's default contraction, does fuse.)
- `Source/System/FloatMath.h` provides `RTE::Sin`, `Cos`, `Tan`, `Asin`, `Acos`,
  `Atan` and `Atan2`, computed in double and rounded once, because the system libm
  and Emscripten's musl disagree by one unit in the last place on `sinf` and
  `atan2f`. All seventeen engine call sites use them. `tests/float_contract.cpp`
  checks this; its raw `sinf`/`atan2f` lines differ on purpose, as documentation.

Terrain generation still matched the original exactly, rotated debris included.
Physics over time is where these would show: trajectories integrate many
multiply-adds, and the original fuses them where the port does not.

## Threaded play

The original is **not reproducible against itself** once threads are involved.
C++ `RandomNum` calls made while scripts run on the thread pool all go to the one
unguarded `g_RandomGenerator`, and so do the sight rays (`Actor::CastSeeRays`, six
draws per actor per step while anything is unseen), which run as a parallel loop
over actors. Which caller gets which value depends on scheduling.

The port does exactly the same. In the browser Tutorial, two fully threaded
`?simulate=600` runs gave 35,401 and 35,500 draws and different states; the
serial mode (`&parallel=0`) gives state hash `c2b76d23260db076`, 82 objects and
34,095 draws every time, and is what regression tests should use.

Frame rate feeds the stream too: `CameraMan::Update` draws the screen-shake angle
(`RandomNormalNum()`) from the global generator on every drawn frame of a
`Running` activity, shake or no shake, and the loop draws a frame on every pass.
Any comparison of play over time is therefore also a comparison of frame pacing.

The one ordering difference kept on purpose: `LuaStateWrapper::m_RegisteredMOs`
is a `std::set` ordered by unique ID, where the original iterates an
`unordered_set` of pointers in heap-address order (see [lua](lua.md)).

## Choices tried and reversed

Recorded because each looks like a good idea and was undone for a reason.

- **The title screen had its own generator.** `TitleScreen::CreateTitleElements`
  sizes the star field from the window width, five or six draws per star, and
  re-tints every star each frame. That made the port's no-activity scene dump
  depend on window width. But real missions reseed in `Activity::Start`, so it
  never affected gameplay, and it changed the title screen. The dump harness now
  calls `SeedRNG()` itself before loading, exactly as `Activity::Start` does, and
  `TitleScreen` is the original's code again.
- **A fixed count of 8 Lua states with separately seeded Lua streams.** Adopted to
  make the port's native build (12 cores) and browser build (4-worker pool) agree.
  But objects are assigned to states round robin, so the count decides which
  stream a script draws from, and the original uses one state per logical CPU with
  seeds from the global stream. The port now does the same; in the browser the
  count comes from `navigator.hardwareConcurrency`, which reports 12 here, as the
  native game sees. The master state's stream is verified identical to the
  original's above.
- **Per-state generators for C++ draws, and sorting new objects by unique ID.**
  Each script state had a private `m_SimulationRandom` that C++ drew from while
  the state ran, and objects added during a step were sorted by unique ID before
  joining the simulation's lists. Together they made threaded play reproducible
  (threaded == serial). But both changed single-threaded behaviour too: script-
  initiated draws left the global stream, and a gibbed actor's attachables (old
  IDs) moved ahead of its gibs (new IDs) in update order. Removed on 2026-09-23.
- **Explicit rejection-sampling distributions** of my own design. Correct and
  cross-build consistent, but not the original's values; replaced by the libstdc++
  reproduction.

## Consequences

- The shipped `*.preview.png` files still match only about 90% after palette
  quantisation, unchanged by the libstdc++ reproduction. Whatever generated them
  differs in something the random stream does not explain — mismatches concentrate
  where a preview pixel samples dense texture. Compare against the original
  running, not against those files.
- Any new code that draws random numbers must go through `RandomGenerator`, and any
  new distribution must reproduce libstdc++'s algorithm, or the port stops matching
  the original from that draw on. Re-run `random_contract` and the parity probe.
