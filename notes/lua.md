# Lua scripting

Updated 2026-09-23.

Entry points: `engine/Source/Managers/LuaMan.h` / `.cpp` (states, pool,
callbacks), `engine/Source/Lua/` (bindings and adapters),
`engine/Source/Entities/MovableObject.cpp` (how an object acquires a
state), `vendor/lua-5.1.5/`, `vendor/LuaBitOp-1.0.4/`,
`runtime/include/lua.hpp`, `tools/audit_lua_bindings.py`.

## What the system is

Nearly all game content behaviour is Lua. Every `MovableObject` preset can carry
scripts, activities are Lua classes (`GAScripted`), AI is Lua, and mods are
almost entirely Lua. The engine exposes itself through **luabind 0.7.1**, a
template-heavy C++/Lua binding library, in `Source/Lua/LuaBindings*.cpp` grouped
by domain (Entities, Managers, Activities, GUI, Input, Primitives, System, Misc).

Scripts do not run on one interpreter. The engine keeps a **pool of independent
`lua_State`s** so object scripts can run on several threads at once, and an
object is bound to one state for its whole life.

## The interpreter: LuaJIT replaced by Lua 5.1.5

LuaJIT compiles Lua to native machine code. WebAssembly cannot execute generated
native code, so the browser build uses **stock Lua 5.1.5** — the language version
LuaJIT implements — plus **LuaBitOp 1.0.4**, which supplies the `bit` library
that LuaJIT provides natively and that shipped content uses.

Built by `CMakeLists.txt` as `cortex_lua` (all of `lua-5.1.5/src` except
`lua.c`, `luac.c`, `print.c`, plus `LuaBitOp-1.0.4/bit.c`, with `LUA_USE_POSIX`)
and `cortex_luabind` from the engine's vendored luabind. `runtime/include/lua.hpp`
is the shim the engine includes; it declares `luaopen_bit` and defines
`LUA_BITLIBNAME` so the registration table below compiles unchanged.

`LuaStateWrapper::Initialize` registers: base, package, table, string, math,
debug, io, os, and `bit`. Under Emscripten the **`ffi` and `jit` libraries are
not registered**, and the `luaJIT_setmode` call that follows is compiled out.
Content that calls `jit.*` or `ffi.*` will fail in the browser; nothing shipped
does, but a mod could.

The target is interpreter **compatibility**, not JIT performance. Expect script
throughput well below a LuaJIT build. If gameplay is slow, measure before
assuming the port is at fault — the Tutorial runs at 60 FPS with scripts taking
about 46% of the update budget under software rendering.

### Coroutines and the main thread

LuaJIT exposes the main thread of a state through the internal macro
`mainthread(G(L))`. Stock Lua 5.1.5 has no public equivalent, and
`LuaAdaptersScene::CalculatePathAsync` needs it: `luabind::object` is only a
**weak reference — a `lua_State*` and a stack index** — so an async callback must
be stored somewhere durable, and it must be stored against the *owning* state
rather than whatever coroutine happened to call in.

`LuaStateWrapper::Initialize` therefore does, under Emscripten only:

```cpp
lua_pushthread(m_State);
lua_setfield(m_State, LUA_REGISTRYINDEX, "Cortex.MainThread");
```

and `LuaAdapters.cpp` reads it back. The registry is shared between a state and
its coroutines, so this reliably recovers the owning thread. Any other adapter
that needs the main thread must use the same key, not the caller's `interpreter()`.

## The state pool

`LuaMan` owns:

- `m_MasterScriptState` — one state for objects that must not run concurrently.
- `m_ScriptStates` — a `std::vector<LuaStateWrapper>`, **one per logical CPU**
  (`std::thread::hardware_concurrency()`), exactly as the original.

The count matters more than it looks: objects are handed to states round robin as
they are created, so it decides which state runs an object's scripts and therefore
which random stream they draw from. In the browser the count comes from
`navigator.hardwareConcurrency`, which reports the same number the native game
sees on the same machine (12 here), so the assignment matches the original's.
`NumberOfLuaStatesOverride` in Settings.ini still overrides it; `EnableLuaDebugging`
forces it to 0, which puts everything on the master state.

(The port briefly used a fixed 8 to make its own native and browser builds agree.
That departed from the original and was reverted; see [randomness](randomness.md).)

Each `LuaStateWrapper` owns:

| member | purpose |
| --- | --- |
| `m_State` | the `lua_State*` |
| `m_Mutex` | `std::recursive_mutex`; under the current design it should never actually block, and exists mainly to fire asserts |
| `m_RegisteredMOs` | the objects whose scripts run here |
| `m_AddedRegisteredMOs` | staging, merged by `LuaStateWrapper::Update` |
| `m_ScriptCache` | compiled script functions as `LuabindObjectWrapper*`, keyed by script path then function name |
| `m_ScriptTimings` | per-script timing for the performance overlay; only the master state's are kept, and in the browser the clock is read for them only while the overlay is shown (see [testing](testing.md)) |
| `m_RandomGenerator` | what Lua's `math.random` / `RangeRand` / `PosRand` / `NormalRand` draw from |

`m_RandomGenerator` is seeded exactly as the original does:
`RandomNum<uint64_t>(0, max)` from the global stream at startup, two draws per
state, master first — so each state's Lua stream is the original's. The parity
probe checks the master state's stream against the untouched original.
C++ code called from a script (`RandomNum` inside `GibThis`, `Create`, …) draws
from the global `g_RandomGenerator`, as in the original — see below.

### How an object gets a state

`MovableObject::GetAndLockStateForScript` (`MovableObject.cpp`):

1. If `m_ForceIntoMasterLuaState` is set, use the master state. The flag is an
   INI property and is also forced on when Lua debugging is enabled. Upstream's
   own comment calls it awful; it exists for automovers, which mutate global
   state freely.
2. Otherwise, if the object already has `m_ThreadedLuaState`, lock it.
3. Otherwise ask `LuaMan::GetAndLockFreeScriptState()`.

`GetAndLockFreeScriptState` has two paths. If the calling thread has a **state
override** set (`SetThreadLuaStateOverride`, used while a threaded phase runs),
the new object is assigned to *that* state — so objects created by a script
belong to the same state as their creator, which is what keeps the assignment
deterministic under threading. Otherwise it round-robins
`m_LastAssignedLuaState` and asserts that the mutex was free.

An object stays on its state for life. Unregistering happens through
`LuaStateWrapper::UnregisterMO`.

### Execution order is part of the contract

`MovableMan::Update` runs, per simulation step, the master state's registered
objects serially, then every threaded state — one task per state on the
priority pool. Within a state it iterates `m_RegisteredMOs` and calls
`RunScriptedFunctionInAppropriateScripts` on each.

`m_RegisteredMOs` is a `std::set<MovableObject*, MovableObjectIDLess>`, ordered by
`GetUniqueID()`. This is one of the few deliberate differences from the original,
which uses an `std::unordered_set<MovableObject*>`: there, scripts run in hash
iteration order, and a pointer's hash is its address, so the order comes from
the allocator's heap layout (which macOS randomises per launch). No port can
reproduce that; the unique ID order is at least stable. **It must not become a
hash container again** — libstdc++ and libc++ bucket differently too.

### Threaded phases and the global generator

`LuaMan::SetThreadLuaStateOverride(state)` records which state the thread is
running, so objects created by a script are assigned to that same state.

C++ `RandomNum` calls made while scripts run — on any state, on any thread — go to
the one global `g_RandomGenerator`, exactly as in the original. With several
threaded states drawing at once that is a data race, and it is the original's:
which caller gets which value depends on scheduling, so threaded play is not
reproducible run to run, in the original or here. In the port it is memory-safe:
libc++'s `mersenne_twister_engine` wraps its index modulo the state size on every
call, so a lost or duplicated draw cannot read out of bounds.

The port once gave every state a private generator for these draws
(`m_SimulationRandom`, selected through a `thread_local`) to make threaded play
reproducible. It was removed on 2026-09-23: it changed which values the
simulation drew even on a single thread, which is exactly what the port must not
do. For reproducible runs, serialise the phases instead (`?simulate=<steps>&parallel=0`,
see [testing](testing.md)).

Anything that runs script code on a thread **must** set and clear the override, or
new objects are assigned to the wrong state.

### An upstream race: scripted objects created during the late update

`LuaMan::GetAndLockFreeScriptState` gives a new scripted object a threaded state
by `try_lock`, and asserts if the lock fails. `MovableMan::Update` ends by
starting asynchronous Lua garbage collection on the pool, which locks each state
while it collects, and `LateUpdateGlobalScripts` runs right after. So a
late-update global script that creates a scripted object — gibbing an actor
whose gibs carry scripts is enough — can find its state busy, depending on
timing. The original then stops on a modal "Script mutex was already locked"
assertion. Found when a probe script gibbed brains from a late update: the
original hung on the dialog in Bunker Breach and the One-Man Army missions,
and not in others. Ordinary global scripts run after the previous collection
has been waited for and are safe. The port has the same race and, since
2026-09-24, the same dialog (see [testing](testing.md)).

## Callbacks from other threads

`LuaMan::AddLuaScriptCallback(std::function<void()>)` queues work under
`m_ScriptCallbacksMutex`; `LuaMan::Update` swaps the queue out and runs it on the
main thread. This is how the async pathfinder gets back into Lua: the pathing
thread cannot touch a `lua_State`, so it queues a closure that calls
`_TriggerAsyncPathCallback`.

Any new engine-side async work that needs to call into Lua should use this, not a
direct call from the worker.

## Garbage collection

`LuaMan::StartAsyncGarbageCollection` normally submits one `LUA_GCSTEP` per state
to the priority pool, each under that state's mutex, followed by `LUA_GCSTOP`, as
upstream does. So the collector runs only in those steps, once per update. The
comment in `LuaStateWrapper::Initialize` about keeping "the normal GC on so it can
catch any big spikes or runaway allocs" is out of date: after the first step the
collector is stopped, and LuaJIT's `LUA_GCSTOP` stops it the same way. A script that keeps
allocating inside one call grows its state until the call returns; in the browser,
until the 4 GB heap runs out (see Memory).

In deterministic simulation mode (`System::IsInDeterministicSimulationMode`, used
by `-simulate-tutorial`) it collects inline instead, in state order, because
finalizer timing on pool threads would otherwise depend on the scheduler.

## Memory

In the browser every state is made with `lua_newstate` and
`LuaStateWrapper::BrowserLuaAllocate`: `luaL_newstate`'s allocator (realloc and
free) that also counts the bytes each state holds. States normally hold 1–3 MB.
`?perf-debug` prints the counts every two seconds (`Browser memory:`), and a state
that passes 256 MB, and each doubling after, prints which one it is and the script
it is running (`Lua memory: threaded state 7 holds 512 MB, running …`), which is how
a runaway script names itself before the heap runs out. A failed allocation stops
the game with "OOM" (`BrowserOutOfMemory`, see [threads](threads.md)) rather than
raising "not enough memory": the heap is spent, and every script after it would fail
the same way. The native build keeps `luaL_newstate`, as upstream does.

## Bindings: what to watch for

- `luabind::object` is a **weak reference** to a stack slot. Storing one across
  anything that touches the state is a use-after-free. Persist through the
  registry (see the coroutine section) or a `LuabindObjectWrapper`.
- Overloaded engine methods need **explicit member-pointer casts** at the `.def`
  site, or the binding is ambiguous. `tools/audit_lua_bindings.py` compiles
  temporary typed versions of those casts against the real compile commands and
  fails if a cast no longer matches the method it names. It never edits engine
  sources. Run it after changing any bound signature.
- luabind has a compile-time arity limit; long parameter lists need the
  `LUABIND_MAX_ARITY` setting already configured in `CMakeLists.txt`.
- Changing an engine method means checking both C++ callers **and** the binding,
  and shipped content may call it. A signature change that compiles can still
  break every mod.

## Verification

`tests/runtime_check.cpp` (target `runtime_check`) exercises the interpreter
and the bit library under Wasm. Real content coverage comes from actually running
missions; the Tutorial and Dummy Assault both run their scripts, and
`-simulate-tutorial` runs 3600 steps of scripted AI and compares the result
between builds byte for byte.

Not covered: every shipped mission's scripts, the campaign, any mod, script error
paths and stack cleanup after an error, and coroutine-heavy content. When
investigating failing content, check first whether it uses `jit.*`, `ffi.*`, or
stores a `luabind::object` across a call.
