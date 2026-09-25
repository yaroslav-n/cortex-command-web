cmake_minimum_required(VERSION 3.24)
project(CortexCommandWeb LANGUAGES C CXX)
if(NOT EMSCRIPTEN)
  message(FATAL_ERROR "Configure using the project-local emcmake")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 99)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CORTEX_DIST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dist" CACHE PATH "Where the page and its files are written")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CORTEX_DIST_DIR}")
# How the synchronous main loop suspends and how C++ exceptions are thrown.
# Asyncify rewrites the program so its stack can be unwound and rebuilt, and JS
# exceptions route every call inside a try block through a JavaScript trampoline.
# JSPI suspends in the engine instead and needs native Wasm exceptions, because it
# cannot suspend through JavaScript frames.
option(CORTEX_SIMD "Let the compiler use WebAssembly SIMD (-msimd128)" OFF)
if(CORTEX_SIMD)
  add_compile_options(-msimd128)
  add_link_options(-msimd128)
endif()
option(CORTEX_JSPI "Suspend through JSPI with native Wasm exceptions instead of Asyncify" ON)
if(CORTEX_JSPI)
  set(CORTEX_EXCEPTIONS -fwasm-exceptions)
  set(CORTEX_SUSPEND -sJSPI=1)
else()
  set(CORTEX_EXCEPTIONS -fexceptions)
  set(CORTEX_SUSPEND -sASYNCIFY=1 -sASYNCIFY_STACK_SIZE=1048576)
endif()
# Match the native build's strict IEEE arithmetic; see engine/meson.build.
add_compile_options(-pthread ${CORTEX_EXCEPTIONS} -ffp-contract=off)
add_link_options(-pthread ${CORTEX_EXCEPTIONS} -sPTHREAD_POOL_SIZE=16 -sALLOW_MEMORY_GROWTH=1)
set(LUA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/lua-5.1.5/src")
file(GLOB LUA_SOURCES CONFIGURE_DEPENDS "${LUA_DIR}/*.c")
list(REMOVE_ITEM LUA_SOURCES "${LUA_DIR}/lua.c" "${LUA_DIR}/luac.c" "${LUA_DIR}/print.c")
add_library(cortex_lua STATIC ${LUA_SOURCES})
target_include_directories(cortex_lua PUBLIC "${LUA_DIR}")
target_compile_definitions(cortex_lua PRIVATE LUA_USE_POSIX)

# Build the repository's exact SDL version, including its browser event and WebGL backends.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
# The port's copy of the engine, sources and game data, lives in engine/ so this
# project builds and runs from this repository alone; the untouched original game
# it is compared against is a separate checkout (see notes/original-game.md).
set(ENGINE "${CMAKE_CURRENT_SOURCE_DIR}/engine")
add_subdirectory("${ENGINE}/external/sources/SDL3-3.2.10" "sdl3" EXCLUDE_FROM_ALL)

# A dependency integration test, not a replacement for the game.
add_executable(runtime_check tests/runtime_check.cpp)
target_link_libraries(runtime_check PRIVATE cortex_lua SDL3::SDL3-static)
target_link_options(runtime_check PRIVATE -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sEXIT_RUNTIME=1)
set_target_properties(runtime_check PROPERTIES SUFFIX ".js")
configure_file(tests/runtime-check.html "${CORTEX_DIST_DIR}/runtime-check.html" COPYONLY)
# Pages that need no compiled program: raw browser keyboard events and pointer lock.
configure_file(tests/input-check.html "${CORTEX_DIST_DIR}/input-check.html" COPYONLY)
configure_file(tests/pointer-lock-check.html "${CORTEX_DIST_DIR}/pointer-lock-check.html" COPYONLY)


file(GLOB LUABIND_SOURCES CONFIGURE_DEPENDS "${ENGINE}/external/sources/luabind-0.7.1/src/*.cpp")
add_library(cortex_luabind STATIC ${LUABIND_SOURCES})
target_include_directories(cortex_luabind PUBLIC
  "${ENGINE}/external/sources/luabind-0.7.1"
  "${ENGINE}/external/sources/luabind-0.7.1/luabind"
  "${ENGINE}/external/include/boost_1_75")
target_compile_definitions(cortex_luabind PUBLIC
  _LIBCPP_ENABLE_CXX17_REMOVED_AUTO_PTR _LIBCPP_ENABLE_CXX17_REMOVED_UNARY_BINARY_FUNCTION
  BOOST_BIND_GLOBAL_PLACEHOLDERS=1)
target_compile_options(cortex_luabind PRIVATE -Wno-deprecated-declarations)
target_link_libraries(cortex_luabind PUBLIC cortex_lua)

target_link_options(runtime_check PRIVATE -sFORCE_FILESYSTEM=1 ${CORTEX_SUSPEND} -lidbfs.js "--pre-js=${CMAKE_CURRENT_SOURCE_DIR}/runtime/persistence-journal.js" "--pre-js=${CMAKE_CURRENT_SOURCE_DIR}/runtime/filesystem.js")

include(engine-sources.cmake)
add_library(cortex_engine STATIC EXCLUDE_FROM_ALL ${CORTEX_ENGINE_SOURCES})
target_include_directories(cortex_engine BEFORE PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/runtime/include")
target_include_directories(cortex_engine PRIVATE ${CORTEX_ENGINE_INCLUDES}
  "${ENGINE}/external/sources/minizip-ng-4.0.0/src")
target_compile_definitions(cortex_engine PRIVATE RELEASE_BUILD NDEBUG
  IMGUI_IMPL_OPENGL_ES3 SYSTEM_MINIZIP)
target_compile_options(cortex_engine PRIVATE -Wno-deprecated-declarations -Wno-deprecated-builtins)
target_link_libraries(cortex_engine PUBLIC cortex_luabind SDL3::SDL3-static)
target_precompile_headers(cortex_engine PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${ENGINE}/Source/System/StandardIncludes.h>")

target_sources(cortex_lua PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/vendor/LuaBitOp-1.0.4/bit.c")

target_include_directories(cortex_engine PRIVATE "${ENGINE}")

target_include_directories(runtime_check PRIVATE "${ENGINE}/Source/Renderer")
target_link_options(runtime_check PRIVATE "--preload-file=${ENGINE}/Data/Base.rte/Shaders@/shaders")

target_include_directories(cortex_engine PRIVATE "${ENGINE}/external/sources/libpng-1.6.40/include")

file(GLOB GAME_SHADERS "${ENGINE}/Data/Base.rte/Shaders/*")
set_property(TARGET runtime_check APPEND PROPERTY LINK_DEPENDS ${GAME_SHADERS})

include(dependencies.cmake)

add_executable(cortex EXCLUDE_FROM_ALL runtime/entry.cpp)
target_link_libraries(cortex PRIVATE "-Wl,--whole-archive" cortex_engine "-Wl,--no-whole-archive"
  cortex_loadpng cortex_sdl3_image cortex_minizip)
target_link_options(cortex PRIVATE
  -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sFULL_ES3=1
  ${CORTEX_SUSPEND} -sSTACK_SIZE=8388608
  # 4 GB is WebAssembly's limit. A 2 GB cap ran out during a campaign battle; the
  # original, a 64-bit program, uses 1.5 GB at its title screen alone.
  -sINITIAL_MEMORY=536870912 -sMAXIMUM_MEMORY=4294967296
  -sFORCE_FILESYSTEM=1 -lidbfs.js
  "--pre-js=${CMAKE_CURRENT_SOURCE_DIR}/runtime/persistence-journal.js" "--pre-js=${CMAKE_CURRENT_SOURCE_DIR}/runtime/filesystem.js"
  "--pre-js=${CMAKE_CURRENT_SOURCE_DIR}/runtime/sound-files.js" "--pre-js=${CMAKE_CURRENT_SOURCE_DIR}/runtime/thread-errors.js"
  "--preload-file=${ENGINE}/Data@/Data"
  "--exclude-file=*.flac" "--exclude-file=*.ogg" "--exclude-file=*.wav" "--exclude-file=*.mp3"
  # A returning player's browser keeps the package in IndexedDB and uses it while its
  # SHA-256 matches the build's (Emscripten's file packager, EM_PRELOAD_CACHE).
  --use-preload-cache)
set_target_properties(cortex PROPERTIES SUFFIX ".js")
# The linker copies these into cortex.js; an edit to one must relink the game.
set_property(TARGET cortex APPEND PROPERTY LINK_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/runtime/persistence-journal.js" "${CMAKE_CURRENT_SOURCE_DIR}/runtime/filesystem.js"
  "${CMAKE_CURRENT_SOURCE_DIR}/runtime/sound-files.js" "${CMAKE_CURRENT_SOURCE_DIR}/runtime/engine-in-worker.js"
  "${CMAKE_CURRENT_SOURCE_DIR}/runtime/thread-errors.js")
# The page that loads the game. dist/ is build output; its sources live in site/.
configure_file(site/index.html "${CORTEX_DIST_DIR}/index.html" COPYONLY)

# Audio mixes on miniaudio's AudioWorklet backend, on its own thread, so a long
# synchronous block on the page's main thread no longer stalls playback. The
# ScriptProcessorNode fallback runs on the main thread and cannot avoid that.
# `worklet_probe` is the isolated check that this backend works in this exact
# flag combination; see notes/audio.md.
option(CORTEX_AUDIO_WORKLET "Mix audio on an AudioWorklet thread instead of the main thread" ON)
add_library(cortex_audio_backend STATIC runtime/miniaudio_backend.c)
target_include_directories(cortex_audio_backend PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/vendor/miniaudio")
set_target_properties(cortex_audio_backend PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
if(CORTEX_AUDIO_WORKLET)
  target_compile_definitions(cortex_audio_backend PUBLIC MA_ENABLE_AUDIO_WORKLETS)
  target_compile_options(cortex_audio_backend PRIVATE -sAUDIO_WORKLET=1 -sWASM_WORKERS=1)
  set(CORTEX_AUDIO_LINK_OPTIONS -sAUDIO_WORKLET=1 -sWASM_WORKERS=1)
else()
  # The ScriptProcessorNode fallback mixes on the page's main thread, so a long
  # synchronous block stalls playback. Kept switchable so the two can be compared.
  set(CORTEX_AUDIO_LINK_OPTIONS "")
endif()
# The smallest program that can say whether miniaudio's AudioWorklet backend can
# make sound in this flag combination at all, independent of the engine's mixer.
add_executable(worklet_probe EXCLUDE_FROM_ALL tests/worklet_probe.c)
target_link_libraries(worklet_probe PRIVATE cortex_audio_backend)
# The engine's suspend mechanism because miniaudio's worklet path waits on
# emscripten_sleep; that combination is the thing under suspicion.
target_link_options(worklet_probe PRIVATE ${CORTEX_AUDIO_LINK_OPTIONS}
  ${CORTEX_SUSPEND}
  -pthread -sPTHREAD_POOL_SIZE=16 -sALLOW_MEMORY_GROWTH=1
  -sEXPORTED_RUNTIME_METHODS=['ccall','cwrap'] -sEXPORTED_FUNCTIONS=['_main','_probe_start','_probe_callback_count','_probe_frame_count','_probe_last_peak','_probe_device_state_seen','_probe_start_engine','_probe_engine_frames','_probe_grow_heap']
  -g2 -sASSERTIONS=1)
if(CORTEX_JSPI)
  # The page calls these two with ccall({async: true}); under JSPI an export can
  # only suspend if it is declared.
  target_link_options(worklet_probe PRIVATE "-sJSPI_EXPORTS=['probe_start','probe_start_engine']")
endif()
set_target_properties(worklet_probe PROPERTIES SUFFIX ".js")
configure_file(tests/worklet-probe.html "${CORTEX_DIST_DIR}/worklet-probe.html" COPYONLY)

add_executable(audio_check tests/audio_check.cpp)
target_link_libraries(audio_check PRIVATE cortex_audio_backend)
target_link_options(audio_check PRIVATE ${CORTEX_AUDIO_LINK_OPTIONS} ${CORTEX_SUSPEND} -sEXIT_RUNTIME=1
  -sSTACK_SIZE=8388608 "-sEXPORTED_RUNTIME_METHODS=['callMain']"
  "--preload-file=${ENGINE}/Data/Base.rte/Devices/Weapons/Pistol/Sounds/PistolFire.flac@/audio/PistolFire.flac"
  "--preload-file=${ENGINE}/Data/Base.rte/Music/dBSoundworks/uwinfinal.ogg@/audio/uwinfinal.ogg")
set_target_properties(audio_check PROPERTIES SUFFIX ".js")
configure_file(tests/audio-check.html "${CORTEX_DIST_DIR}/audio-check.html" COPYONLY)

target_link_options(cortex PRIVATE -Wl,--error-limit=0 "-sEXPORTED_RUNTIME_METHODS=['callMain']"
  ${CORTEX_AUDIO_LINK_OPTIONS})
if(CORTEX_JSPI)
  # A C++ exception nothing catches reaches the page as a bare WebAssembly.Exception,
  # "[object WebAssembly.Exception]". With stack traces every throw carries its type,
  # its what() and the stack it was thrown from, which the page shows.
  target_link_options(cortex PRIVATE -sEXCEPTION_STACK_TRACES=1)
endif()

# The sounds, 85% of the game's data, are not in its data package: the page fetches
# them after the game starts (runtime/sound-files.js), so Play is ready once the rest
# has arrived. audio_manifest lists them with the length and sample rate FMOD needs
# before their bytes arrive, and copies each into audio/ under a name made from its
# contents (tools/audio_manifest.c; notes/files-and-saves.md).
add_executable(audio_manifest tools/audio_manifest.c)
target_link_libraries(audio_manifest PRIVATE cortex_audio_backend)
target_link_options(audio_manifest PRIVATE -sNODERAWFS=1 -sEXIT_RUNTIME=1 -sPTHREAD_POOL_SIZE=0)
set_target_properties(audio_manifest PROPERTIES SUFFIX ".js" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
file(GLOB_RECURSE GAME_SOUNDS CONFIGURE_DEPENDS
  "${ENGINE}/Data/*.flac" "${ENGINE}/Data/*.ogg" "${ENGINE}/Data/*.wav" "${ENGINE}/Data/*.mp3")
add_custom_command(OUTPUT "${CORTEX_DIST_DIR}/audio/manifest.tsv"
  COMMAND audio_manifest "${ENGINE}/Data" "${CORTEX_DIST_DIR}/audio" "Data/"
  DEPENDS audio_manifest ${GAME_SOUNDS} VERBATIM)
add_custom_target(game_sounds DEPENDS "${CORTEX_DIST_DIR}/audio/manifest.tsv")
add_dependencies(cortex game_sounds)

# The engine can run on a worker thread (PROXY_TO_PTHREAD) and draw to the canvas
# through an OffscreenCanvas, which leaves the page's own thread free: it only
# relays input and the calls that need the page (files, audio start-up, pointer
# lock). runtime/engine-in-worker.js keeps the browser's default key and wheel
# actions away from the game as SDL does on the page's thread. See notes/threads.md.
option(CORTEX_WORKER "Run the engine on a worker thread instead of the page's main thread" OFF)
if(CORTEX_WORKER)
  if(NOT CORTEX_JSPI)
    message(FATAL_ERROR "CORTEX_WORKER needs CORTEX_JSPI")
  endif()
  target_link_options(cortex PRIVATE -sPROXY_TO_PTHREAD=1 -sOFFSCREENCANVAS_SUPPORT=1
    "--pre-js=${CMAKE_CURRENT_SOURCE_DIR}/runtime/engine-in-worker.js")
endif()
if(CORTEX_JSPI)
  # Lets the page's thread start the audio engine for a worker (runtime/fmod/system.cpp).
  target_link_options(cortex PRIVATE "-sJSPI_EXPORTS=['cortex_audio_start_engine']"
    "-sEXPORTED_FUNCTIONS=['_main','_cortex_audio_start_engine','_cortex_audio_start_failed']")
endif()

# The FMOD API the engine uses, implemented on miniaudio (runtime/fmod/internal.hpp).
add_library(cortex_fmod_browser STATIC runtime/fmod/system.cpp runtime/fmod/sound.cpp
  runtime/fmod/channel.cpp runtime/fmod/dsp.cpp runtime/fmod/playback.cpp runtime/fmod/decoded.cpp
  runtime/fmod/diagnostics.cpp)
target_include_directories(cortex_fmod_browser BEFORE PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/runtime/include")
target_include_directories(cortex_fmod_browser PUBLIC "${ENGINE}/external/include/fmod-2.2.13")
target_link_libraries(cortex_fmod_browser PUBLIC cortex_audio_backend)
target_link_libraries(cortex PRIVATE cortex_fmod_browser)

target_link_libraries(cortex_engine PUBLIC cortex_fmod_browser)

add_executable(audio_contract tests/audio_contract.cpp)
target_link_libraries(audio_contract PRIVATE cortex_fmod_browser)
target_link_options(audio_contract PRIVATE ${CORTEX_AUDIO_LINK_OPTIONS} -sEXIT_RUNTIME=1 -sSTACK_SIZE=8388608
  "--preload-file=${ENGINE}/Data/Base.rte/Devices/Weapons/Pistol/Sounds/PistolFire.flac@/audio/PistolFire.flac")
set_target_properties(audio_contract PROPERTIES SUFFIX ".js")
configure_file(tests/audio-mixer-check.html "${CORTEX_DIST_DIR}/audio-mixer-check.html" COPYONLY)

target_link_options(audio_contract PRIVATE -g2 -sASSERTIONS=1)

# Function names stay in the release build: crash reports and profiles then name
# the C++ functions, at no runtime cost. The diagnostic build (./build.sh --debug)
# adds Emscripten's runtime assertions.
target_link_options(cortex PRIVATE --profiling-funcs)
option(CORTEX_WEB_DIAGNOSTICS "Add Emscripten's runtime assertions (./build.sh --debug)" OFF)
if(CORTEX_WEB_DIAGNOSTICS)
  target_link_options(cortex PRIVATE -g2 -sASSERTIONS=1)
endif()

target_link_options(audio_contract PRIVATE "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/limiter@/audio/limiter")

target_link_options(audio_contract PRIVATE "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/compressor@/audio/compressor")

target_link_options(audio_contract PRIVATE "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/volume@/audio/volume")

# The real UInputMan and GUI wrapper: Return/keypad Enter and SDL3 text input.
add_executable(gui_input_contract EXCLUDE_FROM_ALL tests/gui_input_contract.cpp)
target_include_directories(gui_input_contract BEFORE PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/runtime/include")
target_include_directories(gui_input_contract PRIVATE ${CORTEX_ENGINE_INCLUDES} "${ENGINE}")
target_compile_definitions(gui_input_contract PRIVATE RELEASE_BUILD NDEBUG IMGUI_IMPL_OPENGL_ES3 SYSTEM_MINIZIP)
target_link_libraries(gui_input_contract PRIVATE cortex_engine cortex_loadpng cortex_sdl3_image cortex_minizip)
target_link_options(gui_input_contract PRIVATE -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sFULL_ES3=1 -sEXIT_RUNTIME=1 -sSTACK_SIZE=8388608 -sINITIAL_MEMORY=536870912)
set_target_properties(gui_input_contract PROPERTIES SUFFIX ".js")
configure_file(tests/gui-input-check.html "${CORTEX_DIST_DIR}/gui-input-check.html" COPYONLY)

# The main thread's wait for pool work: short waits stay off the event loop, long
# ones let the page run, and calls workers proxy to the main thread are serviced.
add_executable(thread_wait EXCLUDE_FROM_ALL tests/thread_wait.cpp)
target_include_directories(thread_wait PRIVATE ${CORTEX_ENGINE_INCLUDES} "${ENGINE}")
target_link_libraries(thread_wait PRIVATE cortex_engine cortex_loadpng cortex_sdl3_image cortex_minizip)
target_link_options(thread_wait PRIVATE ${CORTEX_AUDIO_LINK_OPTIONS} ${CORTEX_SUSPEND} -sPTHREAD_POOL_SIZE=4 -sEXIT_RUNTIME=1
  -sSTACK_SIZE=8388608 -sINITIAL_MEMORY=536870912 -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -g2 -sASSERTIONS=1)
set_target_properties(thread_wait PROPERTIES SUFFIX ".js")
configure_file(tests/thread-wait-check.html "${CORTEX_DIST_DIR}/thread-wait-check.html" COPYONLY)

add_executable(frame_readback_contract tests/frame_readback_contract.cpp)
target_include_directories(frame_readback_contract BEFORE PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/runtime/include" "${ENGINE}/Source/Renderer")
target_link_libraries(frame_readback_contract PRIVATE SDL3::SDL3-static)
target_link_options(frame_readback_contract PRIVATE -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sEXIT_RUNTIME=1)
set_target_properties(frame_readback_contract PROPERTIES SUFFIX ".js")
configure_file(tests/frame-readback-check.html "${CORTEX_DIST_DIR}/frame-readback-check.html" COPYONLY)

add_executable(texture_tile_contract tests/texture_tile_contract.cpp)
target_include_directories(texture_tile_contract PRIVATE "${ENGINE}/Source/Renderer")
target_link_options(texture_tile_contract PRIVATE -sEXIT_RUNTIME=1)
set_target_properties(texture_tile_contract PROPERTIES SUFFIX ".js")
configure_file(tests/texture-tile-check.html "${CORTEX_DIST_DIR}/texture-tile-check.html" COPYONLY)

add_executable(texture_gpu_contract EXCLUDE_FROM_ALL tests/texture_gpu_contract.cpp)
target_include_directories(texture_gpu_contract BEFORE PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/runtime/include")
target_include_directories(texture_gpu_contract PRIVATE ${CORTEX_ENGINE_INCLUDES} "${ENGINE}")
target_compile_definitions(texture_gpu_contract PRIVATE RELEASE_BUILD NDEBUG IMGUI_IMPL_OPENGL_ES3 SYSTEM_MINIZIP)
target_link_libraries(texture_gpu_contract PRIVATE cortex_engine cortex_loadpng cortex_sdl3_image cortex_minizip)
# The game's memory cap, so uploads take the game's path through Emscripten's WebGL glue: above
# 2 GB it hands WebGL a view sized from the unpack row length and alignment it tracks.
target_link_options(texture_gpu_contract PRIVATE -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sFULL_ES3=1 -sEXIT_RUNTIME=1 -sSTACK_SIZE=8388608 -sINITIAL_MEMORY=536870912 -sMAXIMUM_MEMORY=4294967296)
set_target_properties(texture_gpu_contract PROPERTIES SUFFIX ".js")
configure_file(tests/texture-gpu-check.html "${CORTEX_DIST_DIR}/texture-gpu-check.html" COPYONLY)

add_executable(storage_runtime EXCLUDE_FROM_ALL tests/storage_runtime.cpp)
target_link_options(storage_runtime PRIVATE "--pre-js=${CMAKE_CURRENT_SOURCE_DIR}/runtime/persistence-journal.js" -lidbfs.js -sFORCE_FILESYSTEM=1 -sMODULARIZE=1 -sEXPORT_NAME=StorageRuntime "-sEXPORTED_RUNTIME_METHODS=['FS','IDBFS']")
set_target_properties(storage_runtime PROPERTIES SUFFIX ".js")
configure_file(tests/storage-race-check.html "${CORTEX_DIST_DIR}/storage-race-check.html" COPYONLY)
configure_file(tests/editor-storage-check.html "${CORTEX_DIST_DIR}/editor-storage-check.html" COPYONLY)

# Changes to pre-JS files must relink even when compiler inputs are unchanged.
set_property(TARGET cortex runtime_check storage_runtime APPEND PROPERTY LINK_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/runtime/persistence-journal.js")
set_property(TARGET cortex runtime_check APPEND PROPERTY LINK_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/runtime/filesystem.js")

# The game bundles Data at link time, so an edited shader only reaches the
# published build when the link, and with it the file packager, reruns.
set_property(TARGET cortex APPEND PROPERTY LINK_DEPENDS ${GAME_SHADERS})

# The simulation's random sequence must be identical to the native build's, so
# this prints it for a direct diff against the same file compiled natively.
add_executable(random_contract EXCLUDE_FROM_ALL tests/random_contract.cpp)
target_include_directories(random_contract PRIVATE "${ENGINE}/Source/System")
target_link_options(random_contract PRIVATE -sEXIT_RUNTIME=1)
set_target_properties(random_contract PROPERTIES SUFFIX ".js")

# Arithmetic the simulation depends on must agree with the native build.
add_executable(float_contract EXCLUDE_FROM_ALL tests/float_contract.cpp)
target_include_directories(float_contract PRIVATE "${ENGINE}/Source/System")
target_link_options(float_contract PRIVATE -sEXIT_RUNTIME=1)
set_target_properties(float_contract PROPERTIES SUFFIX ".js")

add_executable(browser_input_queue_contract EXCLUDE_FROM_ALL tests/browser_input_queue_contract.cpp)
target_include_directories(browser_input_queue_contract PRIVATE "${ENGINE}/Source/System")
target_link_libraries(browser_input_queue_contract PRIVATE SDL3::SDL3-static)
target_link_options(browser_input_queue_contract PRIVATE -sEXIT_RUNTIME=1)
set_target_properties(browser_input_queue_contract PROPERTIES SUFFIX ".js")
configure_file(tests/browser-input-queue-check.html "${CORTEX_DIST_DIR}/browser-input-queue-check.html" COPYONLY)

# Everything tools/run-checks.mjs needs: the game and every test program.
configure_file(tests/check-report.js "${CORTEX_DIST_DIR}/check-report.js" COPYONLY)
add_custom_target(checks DEPENDS cortex runtime_check audio_check audio_contract worklet_probe
  gui_input_contract thread_wait frame_readback_contract texture_tile_contract texture_gpu_contract
  storage_runtime random_contract float_contract browser_input_queue_contract)
