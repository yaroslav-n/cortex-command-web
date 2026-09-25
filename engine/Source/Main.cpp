#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <deque>
#include <set>
#include <tuple>
#endif
#ifdef __EMSCRIPTEN__
static void BrowserStage(const char* stage) {
    static const bool enabled = MAIN_THREAD_EM_ASM_INT({ return Module['threadDiagnostics'] === true; });
    if (enabled) MAIN_THREAD_EM_ASM({ Module['engineStage'] = UTF8ToString($0); Module['engineStageVisits'] = (Module['engineStageVisits'] || 0) + 1; }, stage);
}
#else
static void BrowserStage(const char*) {}
#endif
/*          ______   ______   ______  ______  ______  __  __       ______   ______   __    __   __    __   ______   __   __   _____
           /\  ___\ /\  __ \ /\  == \/\__  _\/\  ___\/\_\_\_\     /\  ___\ /\  __ \ /\ "-./  \ /\ "-./  \ /\  __ \ /\ "-.\ \ /\  __-.
           \ \ \____\ \ \/\ \\ \  __<\/_/\ \/\ \  __\\/_/\_\/_    \ \ \____\ \ \/\ \\ \ \-./\ \\ \ \-./\ \\ \  __ \\ \ \-.  \\ \ \/\ \
            \ \_____\\ \_____\\ \_\ \_\ \ \_\ \ \_____\/\_\/\_\    \ \_____\\ \_____\\ \_\ \ \_\\ \_\ \ \_\\ \_\ \_\\ \_\\"\_\\ \____-
             \/_____/ \/_____/ \/_/ /_/  \/_/  \/_____/\/_/\/_/     \/_____/ \/_____/ \/_/  \/_/ \/_/  \/_/ \/_/\/_/ \/_/ \/_/ \/____/
   ______   ______   __    __   __    __   __  __   __   __   __   ______  __  __       ______  ______   ______      __   ______   ______   ______
  /\  ___\ /\  __ \ /\ "-./  \ /\ "-./  \ /\ \/\ \ /\ "-.\ \ /\ \ /\__  _\/\ \_\ \     /\  == \/\  == \ /\  __ \    /\ \ /\  ___\ /\  ___\ /\__  _\
  \ \ \____\ \ \/\ \\ \ \-./\ \\ \ \-./\ \\ \ \_\ \\ \ \-.  \\ \ \\/_/\ \/\ \____ \    \ \  _-/\ \  __< \ \ \/\ \  _\_\ \\ \  __\ \ \ \____\/_/\ \/
   \ \_____\\ \_____\\ \_\ \ \_\\ \_\ \ \_\\ \_____\\ \_\\"\_\\ \_\  \ \_\ \/\_____\    \ \_\   \ \_\ \_\\ \_____\/\_____\\ \_____\\ \_____\  \ \_\
    \/_____/ \/_____/ \/_/  \/_/ \/_/  \/_/ \/_____/ \/_/ \/_/ \/_/   \/_/  \/_____/     \/_/    \/_/ /_/ \/_____/\/_____/ \/_____/ \/_____/   \/_/

/////\\\\\/////\\\\\/////\\\\\/////\\\\\/////\\\\\/////\\\\\/////\\\\\/////\\\\\/////\\\\\/////\\\\\/////\\\\\/////\\\\\/////\\\\\/////\\\\\/////\\\\\*/

/// <summary>
/// Main driver implementation of the Retro Terrain Engine.
/// Data Realms, LLC - http://www.datarealms.com
/// Cortex Command Community Project - https://github.com/cortex-command-community
/// Cortex Command Community Project Discord - https://discord.gg/TSU6StNQUG
/// </summary>

#include "allegro.h"
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include "GUI.h"
#include "GUIInputWrapper.h"
#include "AllegroScreen.h"
#include "AllegroBitmap.h"

#include "MainMenuGUI.h"
#include "ScenarioGUI.h"
#include "PauseMenuGUI.h"
#include "TitleScreen.h"
#include "LoadingScreen.h"

#include "MenuMan.h"
#ifdef __EMSCRIPTEN__
#include "BrowserInputEventQueue.h"
#include "BrowserEditorSaveCheck.h"
#endif
#include "ConsoleMan.h"
#include "SettingsMan.h"
#include "PresetMan.h"
#include "UInputMan.h"
#include "PerformanceMan.h"
#include "FrameMan.h"
#include "PostProcessMan.h"
#include "SceneMan.h"
#include "MetaMan.h"
#include "WindowMan.h"
#include "GLResourceMan.h"
#include "CameraMan.h"
#include "ActivityMan.h"
#include "Activity.h"

#include <algorithm>
#include <cstring>
#include <cstdlib>
#include "PrimitiveMan.h"
#include "ThreadMan.h"
#include "LuaMan.h"
#include "MusicMan.h"
#include "System.h"
#include <chrono>

#include "RenderTarget.h"
#include "tracy/Tracy.hpp"

#include "imgui_impl_sdl3.h"

#ifdef _WIN32
#include "windows.h"
#endif

extern "C" {
FILE __iob_func[3] = {*stdin, *stdout, *stderr};
}

using namespace RTE;

/// <summary>
/// Initializes all the essential managers.
/// </summary>
void InitializeManagers() {
	ThreadMan::Construct();
	TimerMan::Construct();
	PresetMan::Construct();
	SettingsMan::Construct();
	WindowMan::Construct();
	GLResourceMan::Construct();
	LuaMan::Construct();
	FrameMan::Construct();
	PerformanceMan::Construct();
	PostProcessMan::Construct();
	PrimitiveMan::Construct();
	AudioMan::Construct();
	GUISound::Construct();
	MusicMan::Construct();
	UInputMan::Construct();
	ConsoleMan::Construct();
	SceneMan::Construct();
	MovableMan::Construct();
	MetaMan::Construct();
	MenuMan::Construct();
	CameraMan::Construct();
	ActivityMan::Construct();
	LoadingScreen::Construct();

	#ifdef __EMSCRIPTEN__
	std::puts("Browser startup: g_ThreadMan");
	// Let the page paint and take input between managers; a few are slow to set up.
	BrowserCooperativeYield();
	#endif
	g_ThreadMan.Initialize();
	#ifdef __EMSCRIPTEN__
	std::puts("Browser startup: g_SettingsMan");
	BrowserCooperativeYield();
	#endif
	g_SettingsMan.Initialize();
	#ifdef __EMSCRIPTEN__
	std::puts("Browser startup: g_WindowMan");
	BrowserCooperativeYield();
	#endif
	g_WindowMan.Initialize();
	#ifdef __EMSCRIPTEN__
	std::puts("Browser startup: g_GLResourceMan");
	BrowserCooperativeYield();
	#endif
	g_GLResourceMan.Initialize();

	#ifdef __EMSCRIPTEN__
	std::puts("Browser startup: g_LuaMan");
	BrowserCooperativeYield();
	#endif
	g_LuaMan.Initialize();
	#ifdef __EMSCRIPTEN__
	std::puts("Browser startup: g_TimerMan");
	BrowserCooperativeYield();
	#endif
	g_TimerMan.Initialize();
	#ifdef __EMSCRIPTEN__
	std::puts("Browser startup: g_FrameMan");
	BrowserCooperativeYield();
	#endif
	g_FrameMan.Initialize();
	#ifdef __EMSCRIPTEN__
	std::puts("Browser startup: g_PostProcessMan");
	BrowserCooperativeYield();
	#endif
	g_PostProcessMan.Initialize();
	#ifdef __EMSCRIPTEN__
	std::puts("Browser startup: g_PerformanceMan");
	BrowserCooperativeYield();
	#endif
	g_PerformanceMan.Initialize();

	#ifdef __EMSCRIPTEN__
	std::puts("Browser startup: g_AudioMan");
	BrowserCooperativeYield();
	#endif
	if (g_AudioMan.Initialize()) {
		g_GUISound.Initialize();
		g_MusicMan.Initialize();
	}

	#ifdef __EMSCRIPTEN__
	std::puts("Browser startup: g_UInputMan");
	BrowserCooperativeYield();
	#endif
	g_UInputMan.Initialize();
	#ifdef __EMSCRIPTEN__
	std::puts("Browser startup: g_ConsoleMan");
	BrowserCooperativeYield();
	#endif
	g_ConsoleMan.Initialize();
	#ifdef __EMSCRIPTEN__
	std::puts("Browser startup: g_SceneMan");
	BrowserCooperativeYield();
	#endif
	g_SceneMan.Initialize();
	#ifdef __EMSCRIPTEN__
	std::puts("Browser startup: g_MovableMan");
	BrowserCooperativeYield();
	#endif
	g_MovableMan.Initialize();
	#ifdef __EMSCRIPTEN__
	std::puts("Browser startup: g_MetaMan");
	BrowserCooperativeYield();
	#endif
	g_MetaMan.Initialize();
	#ifdef __EMSCRIPTEN__
	std::puts("Browser startup: g_MenuMan");
	BrowserCooperativeYield();
	#endif
	g_MenuMan.Initialize();

	// Overwrite Settings.ini after all the managers are created to fully populate the file. Up until this moment Settings.ini is populated only with minimal required properties to run.
	// If Settings.ini already exists and is fully populated, this will deal with overwriting it to apply any overrides performed by the managers at boot (e.g resolution validation).
	if (g_SettingsMan.SettingsNeedOverwrite()) {
		g_SettingsMan.UpdateSettingsFile();
	}
}

/// <summary>
/// Destroys all the managers and frees all loaded data before termination.
/// </summary>
void DestroyManagers() {
	g_MetaMan.Destroy();
	g_PerformanceMan.Destroy();
	g_MovableMan.Destroy();
	g_SceneMan.Destroy();
	g_ActivityMan.Destroy();
	g_GUISound.Destroy();
	g_AudioMan.Destroy();
	g_MusicMan.Destroy();
	g_PresetMan.Destroy();
	g_UInputMan.Destroy();
	g_PostProcessMan.Destroy();
	g_FrameMan.Destroy();
	g_TimerMan.Destroy();
	g_LuaMan.Destroy();
	ContentFile::FreeAllLoaded();
	g_ConsoleMan.Destroy();
	g_GLResourceMan.Destroy();
	g_WindowMan.Destroy();

#ifdef DEBUG_BUILD
	Entity::ClassInfo::DumpPoolMemoryInfo(Writer("MemCleanupInfo.txt"));
#endif
}

/// <summary>
/// Command-line argument handling.
/// </summary>
/// <param name="argCount">Argument count.</param>
/// <param name="argValue">Argument values.</param>
static std::string s_SceneToDumpPreview; //!< Scene whose preview to render before exiting, from -dump-scene-preview.
static bool s_DumpFullWorld = false; //!< Write the whole terrain at scene resolution rather than the small preview.
static int s_SimulationSteps = 0; //!< Simulation updates to run before dumping, from -simulate-tutorial.
static std::string s_ActivityToSimulate; //!< Activity preset to run instead of the Tutorial, from -simulate-activity.

/// The rest of the argument handling needs the managers, but initializing them
/// creates a window and presents frames to it, which a session with no awake
/// display cannot complete. Detect a preview dump run before any of that happens.
void DetectSceneDumpMode(int argCount, char** argValue) {
	for (int i = 1; i < argCount - 1; ++i) {
		const std::string argument = argValue[i];
		if (argument == "-dump-scene-preview" || argument == "-dump-scene-world" || argument == "-simulate-tutorial" || argument == "-simulate-activity") {
			System::EnableSceneDumpMode();
			return;
		}
	}
}

/// A hash of everything the simulation moved, in MOID order.
///
/// Scene generation being identical between two builds says nothing about whether
/// they still agree after the objects start moving, colliding and running scripts.
/// This folds every object's identity, position, velocity and rotation into one
/// number so a fixed number of simulation steps can be compared directly.
static unsigned long long HashSimulationState() {
	unsigned long long hash = 1469598103934665603ULL;
	auto fold = [&hash](unsigned int value) {
		hash = (hash ^ value) * 1099511628211ULL;
	};
	auto foldFloat = [&fold](float value) {
		unsigned int bits;
		std::memcpy(&bits, &value, sizeof(bits));
		fold(bits);
	};

	const int moidCount = g_MovableMan.GetMOIDCount();
	fold(static_cast<unsigned int>(moidCount));
	for (int moid = 1; moid < moidCount; ++moid) {
		const MovableObject* mo = g_MovableMan.GetMOFromID(static_cast<MOID>(moid));
		if (!mo) {
			fold(0xFFFFFFFFu);
			continue;
		}
		fold(static_cast<unsigned int>(mo->GetUniqueID()));
		foldFloat(mo->GetPos().m_X);
		foldFloat(mo->GetPos().m_Y);
		foldFloat(mo->GetVel().m_X);
		foldFloat(mo->GetVel().m_Y);
		foldFloat(mo->GetRotAngle());
		foldFloat(mo->GetAngularVel());
		foldFloat(mo->GetMass());
	}
	return hash;
}

/// Run the Tutorial mission for a fixed number of simulation steps, with no input,
/// no rendering and a fixed time step, then report what the simulation produced.
/// Point ActivityMan at a named Activity preset on a named Scene, so any mission can
/// be driven by the deterministic harness rather than only the Tutorial. The Scene
/// has to be given: an Activity preset's own SceneName does not survive into the
/// stored preset (Activity::Create(const Activity&) does not copy it), and some
/// name no Scene at all (Signal Hunt's "Zombie Cave" is a terrain object). The menu
/// makes the player pick; the parity probe's missions.txt lists working pairs.
/// @param activityAndScene "<Activity preset>|<Scene preset>".
static bool SetStartActivityByName(const std::string& activityAndScene) {
	const size_t separator = activityAndScene.find('|');
	if (separator == std::string::npos) {
		std::printf("Simulation: expected \"<Activity>|<Scene>\", got \"%s\"\n", activityAndScene.c_str());
		return false;
	}
	const std::string presetName = activityAndScene.substr(0, separator);
	const std::string sceneName = activityAndScene.substr(separator + 1);
	const Entity* preset = g_PresetMan.GetEntityPreset("GAScripted", presetName);
	if (!preset) {
		preset = g_PresetMan.GetEntityPreset("GATutorial", presetName);
	}
	if (!preset) {
		std::printf("Simulation: no Activity preset named \"%s\"\n", presetName.c_str());
		return false;
	}
	Activity* startActivity = dynamic_cast<Activity*>(preset->Clone());
	if (!startActivity) {
		std::printf("Simulation: preset \"%s\" is not an Activity\n", presetName.c_str());
		return false;
	}
	g_ActivityMan.SetStartActivity(startActivity);
	if (GameActivity* gameActivity = dynamic_cast<GameActivity*>(g_ActivityMan.GetStartActivity())) {
		gameActivity->SetStartingGold(10000);
	}
	if (g_SceneMan.SetSceneToLoad(sceneName) < 0) {
		std::printf("Simulation: no Scene named \"%s\"\n", sceneName.c_str());
		return false;
	}
	std::printf("Simulation: activity \"%s\" on scene \"%s\"\n", presetName.c_str(), sceneName.c_str());
	return true;
}

static bool RunDeterministicSimulation(int steps) {
	if (!s_ActivityToSimulate.empty()) {
		if (!SetStartActivityByName(s_ActivityToSimulate)) {
			return false;
		}
	} else {
		g_ActivityMan.SetStartTutorialActivity();
	}
	if (!g_ActivityMan.GetStartActivity()) {
		std::printf("Simulation: no activity to start\n");
		return false;
	}
	// StartActivity deletes and re-sets the start Activity, so it has to be given a
	// clone rather than the stored one. RestartActivity does the same thing.
	g_TimerMan.ResetTime();
	Activity* activityToStart = dynamic_cast<Activity*>(g_ActivityMan.GetStartActivity()->Clone());
	activityToStart->SetActivityState(Activity::ActivityState::NotStarted);
	if (g_ActivityMan.StartActivity(activityToStart) < 0) {
		std::printf("Simulation: could not start the activity\n");
		return false;
	}
	g_TimerMan.PauseSim(false);
	g_TimerMan.SetDeltaTimeSecs(1.0F / 60.0F);

	// The serial run does the same work every time, which makes its duration a
	// benchmark for the simulation code (printed on its own line; the result line
	// below must stay exactly as it is, since tests compare it).
	const auto simulationStart = std::chrono::steady_clock::now();
	for (int step = 0; step < steps; ++step) {
		g_TimerMan.ForceOneSimUpdate();
		g_LuaMan.Update();
		g_FrameMan.Update();
		g_MovableMan.CompleteQueuedMOIDDrawings();
		g_ActivityMan.Update();
		if (g_SceneMan.GetScene()) {
			g_SceneMan.GetScene()->Update();
		}
		g_LuaMan.ClearScriptTimings();
		g_MovableMan.Update();
		g_ActivityMan.LateUpdateGlobalScripts();
	}

	// MOIDs are rebuilt on a pool thread when that phase is left parallel, and the
	// hash reads objects by MOID. Drain the pool first, or the harness measures a
	// half-built index rather than anything about the simulation.
	g_ThreadMan.GetPriorityThreadPool().wait_for_tasks();
	g_ThreadMan.GetBackgroundThreadPool().wait_for_tasks();
	const double simulationMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - simulationStart).count();
	std::printf("Simulation time: %d steps in %.0f ms, %.2f ms per step\n", steps, simulationMs, simulationMs / steps);

	const unsigned long long draws = g_RandomGenerator.GetDrawCount();
	const unsigned long long streamHash = g_RandomGenerator.GetDrawHash();
	std::printf("Simulation: %d steps, parallel mask %u, %d objects, state hash %016llx, RNG %llu draws hash %016llx\n",
	            steps, System::GetParallelPhaseMask(), g_MovableMan.GetMOIDCount(), HashSimulationState(),
	            draws, streamHash);
	return true;
}

void HandleMainArgs(int argCount, char** argValue) {
	// Discard the first argument because it's always the executable path/name
	argCount--;
	argValue++;
	if (argCount == 0) {
		return;
	}
	bool launchModeSet = false;
	bool singleModuleSet = false;

	for (int i = 0; i < argCount;) {
		std::string currentArg = argValue[i];
		bool lastArg = i + 1 == argCount;

		if (currentArg == "-cout") {
			System::EnableLoggingToCLI();
		}

		if (currentArg == "-ext-validate") {
			System::EnableExternalModuleValidationMode();
		}

		if (!lastArg && !singleModuleSet && currentArg == "-module") {
			std::string moduleToLoad = argValue[++i];
			if (moduleToLoad.find(System::GetModulePackageExtension()) == moduleToLoad.length() - System::GetModulePackageExtension().length()) {
				g_PresetMan.SetSingleModuleToLoad(moduleToLoad);
				singleModuleSet = true;
			}
		}
		// Render a scene's preview image and exit. This is how the native and
		// browser builds are compared without a display: both write the same
		// picture from the same code, so the files can be diffed directly.
		if (!lastArg && currentArg == "-simulate-activity") {
			s_ActivityToSimulate = argValue[++i];
			s_SimulationSteps = (i + 1 < argCount) ? std::max(1, std::atoi(argValue[++i])) : 600;
			System::EnableSceneDumpMode();
			System::EnableDeterministicSimulationMode();
			System::SetParallelPhaseMask(System::AllParallelPhases);
			if (i + 1 < argCount && argValue[i + 1][0] != '-') {
				System::SetParallelPhaseMask(static_cast<unsigned>(std::strtoul(argValue[++i], nullptr, 0)));
			}
			continue;
		}
		if (!lastArg && currentArg == "-simulate-tutorial") {
			s_SimulationSteps = std::max(1, std::atoi(argValue[++i]));
			System::EnableSceneDumpMode();
			System::EnableDeterministicSimulationMode();
			// Simulate the way the game actually runs, on the thread pool. An optional
			// second number serialises individual phases, which is how a phase that
			// stops a run reproducing itself gets found; 0 serialises everything.
			System::SetParallelPhaseMask(System::AllParallelPhases);
			if (i + 1 < argCount && argValue[i + 1][0] != '-') {
				System::SetParallelPhaseMask(static_cast<unsigned>(std::strtoul(argValue[++i], nullptr, 0)));
			}
			continue;
		}
		if (!lastArg && (currentArg == "-dump-scene-preview" || currentArg == "-dump-scene-world")) {
			s_SceneToDumpPreview = argValue[++i];
			// The preview is a roughly twelvefold downscale of the terrain. The full
			// world dump compares the same content at scene resolution, which is a
			// far more sensitive check of terrain generation between two builds.
			s_DumpFullWorld = currentArg == "-dump-scene-world";
			System::EnableSceneDumpMode();
		}

		if (!launchModeSet) {
			if (!lastArg && currentArg == "-editor") {
				g_ActivityMan.SetEditorToLaunch(argValue[++i]);
				launchModeSet = true;
			}
		}
		++i;
	}
	if (launchModeSet) {
		g_SettingsMan.SetSkipIntro(true);
	}
}

/// <summary>
/// Polls the SDL event queue and passes events to be handled by the relevant managers.
/// </summary>
#ifdef __EMSCRIPTEN__
static BrowserInputEventQueue s_BrowserInputEvents;
#endif
static void EndInputFrame() {
 g_UInputMan.EndFrame();
#ifdef __EMSCRIPTEN__
 s_BrowserInputEvents.Consumed();
#endif
}

void PollSDLEvents() {
	SDL_Event sdlEvent;
#ifdef __EMSCRIPTEN__
	while (SDL_PollEvent(&sdlEvent)) s_BrowserInputEvents.Push(sdlEvent);
	while (s_BrowserInputEvents.Pop(sdlEvent)) {
#else
	while (SDL_PollEvent(&sdlEvent)) {
#endif
		switch (sdlEvent.type) {
			case SDL_EVENT_QUIT :
				System::SetQuit(true);
				return;
			case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
				System::SetQuit(true);
				return;
			case SDL_EVENT_KEY_UP :
			case SDL_EVENT_KEY_DOWN :
			case SDL_EVENT_TEXT_INPUT :
			case SDL_EVENT_MOUSE_MOTION :
			case SDL_EVENT_MOUSE_BUTTON_UP :
			case SDL_EVENT_MOUSE_BUTTON_DOWN :
			case SDL_EVENT_MOUSE_WHEEL :
			case SDL_EVENT_GAMEPAD_AXIS_MOTION :
			case SDL_EVENT_GAMEPAD_BUTTON_DOWN :
			case SDL_EVENT_GAMEPAD_BUTTON_UP :
			case SDL_EVENT_JOYSTICK_AXIS_MOTION :
			case SDL_EVENT_JOYSTICK_BUTTON_DOWN :
			case SDL_EVENT_JOYSTICK_BUTTON_UP :
			case SDL_EVENT_JOYSTICK_ADDED :
			case SDL_EVENT_JOYSTICK_REMOVED :
				g_UInputMan.HandleInputEvent(sdlEvent);
				break;
			default:
				break;
		}
		ImGui_ImplSDL3_ProcessEvent(&sdlEvent);
		if (sdlEvent.type >= SDL_EVENT_WINDOW_FIRST && sdlEvent.type <= SDL_EVENT_WINDOW_LAST) {
			g_WindowMan.QueueWindowEvent(sdlEvent);
		}
	}
}

/// <summary>
/// Game menus loop.
/// </summary>
void RunMenuLoop() {
#ifdef __EMSCRIPTEN__
	std::puts("Browser menu: entered");
#endif
	g_MenuMan.SetIsInMenuScreen(true);
	g_UInputMan.DisableKeys(false);
	g_UInputMan.TrapMousePos(false);

	while (!System::IsSetToQuit()) {
		g_WindowMan.ClearBackbuffer();
		PollSDLEvents();

		g_WindowMan.Update();

		g_UInputMan.Update();
		g_TimerMan.Update();
		g_TimerMan.UpdateSim();
		g_AudioMan.Update();
		g_MusicMan.Update();

#ifdef __EMSCRIPTEN__
		// A window resize during an Activity was held back; the menu loop with no
		// live Activity is where upstream applies resolution changes.
		g_WindowMan.ApplyPendingWindowResolution();
#endif
		if (g_WindowMan.ResolutionChanged()) {
			g_MenuMan.Reinitialize();
			g_ConsoleMan.Destroy();
			g_ConsoleMan.Initialize();
			g_LoadingScreen.CreateLoadingSplash();
			g_WindowMan.CompleteResolutionChange();
		}

		if (g_MenuMan.Update()) {
			EndInputFrame();
			break;
		}

		g_ConsoleMan.Update();

		EndInputFrame();
		g_WindowMan.GetScreenBuffer()->Begin();
		g_MenuMan.Draw();
		g_ConsoleMan.Draw(g_FrameMan.GetBackBuffer32());
		g_WindowMan.GetScreenBuffer()->End();
		g_WindowMan.UploadFrame();
#ifdef __EMSCRIPTEN__
		BrowserEditorRestoreCheck();
#endif
	}

	g_MenuMan.SetIsInMenuScreen(false);
}

/// <summary>
/// Game simulation loop.
/// </summary>
void RunGameLoop() {
	if (System::IsSetToQuit()) {
		return;
	}
	g_TimerMan.PauseSim(false);

	if (g_ActivityMan.ActivitySetToRestart()) {
		g_LoadingScreen.DrawLoadingSplash();
		g_WindowMan.UploadFrame();
		if (!g_ActivityMan.RestartActivity()) {
			// This doesn't work.
			// Somewhat related to https://github.com/cortex-command-community/Cortex-Command-Community-Project-Source/issues/472
			// Deal with later.
			// g_MenuMan.GetTitleScreen()->SetTitleTransitionState(TitleScreen::TitleTransition::ScrollingFadeIn);
		}
	}

	long long updateStartTime = 0;
	long long updateTotalTime = 0;
	long long updateEndAndDrawStartTime = 0;
	long long drawStartTime = 0;
	long long drawTotalTime = 0;

	while (!System::IsSetToQuit()) {
		updateStartTime = g_TimerMan.GetAbsoluteTime();

		BrowserStage("PollSDLEvents();"); PollSDLEvents();
		g_WindowMan.Update();
		g_WindowMan.ClearBackbuffer();

		BrowserStage("g_TimerMan.Update();"); g_TimerMan.Update();
#ifdef __EMSCRIPTEN__
		int browserSimUpdates = 0;
#endif

		// Simulation update, as many times as the fixed update step allows in the span since last frame draw.
		while (g_TimerMan.TimeForSimUpdate()) {
			ZoneScopedN("Simulation Update");
#ifdef __EMSCRIPTEN__
			++browserSimUpdates;
#endif

			g_PerformanceMan.NewPerformanceSample();
			g_PerformanceMan.UpdateMSPSU();
			g_TimerMan.UpdateSim();

			g_PerformanceMan.StartPerformanceMeasurement(PerformanceMan::SimTotal);

			BrowserStage("g_LuaMan.Update();"); g_LuaMan.Update();

			g_UInputMan.Update();

			g_FrameMan.Update();

			BrowserStage("g_MovableMan.CompleteQueuedMOIDDrawings();"); g_MovableMan.CompleteQueuedMOIDDrawings();

			g_ConsoleMan.Update();
			BrowserStage("g_ActivityMan.Update();"); g_ActivityMan.Update();

			if (g_SceneMan.GetScene()) {
				g_SceneMan.GetScene()->Update();
			}

			g_LuaMan.ClearScriptTimings();
			BrowserStage("g_MovableMan.Update();"); g_MovableMan.Update();
			g_PerformanceMan.UpdateSortedScriptTimings(g_LuaMan.GetScriptTimings());

			g_AudioMan.Update();
			g_MusicMan.Update();

			g_ActivityMan.LateUpdateGlobalScripts();

			// This is to support hot reloading entities in SceneEditorGUI. It's a bit hacky to put it in Main like this, but PresetMan has no update in which to clear the value, and I didn't want to set up a listener for the job.
			// It's in this spot to allow it to be set by UInputMan update and ConsoleMan update, and read from ActivityMan update.
			g_PresetMan.ClearReloadEntityPresetCalledThisUpdate();

			g_PerformanceMan.StopPerformanceMeasurement(PerformanceMan::SimTotal);
			EndInputFrame();

			if (!g_ActivityMan.IsInActivity()) {
				g_TimerMan.PauseSim(true);

				if (!g_ActivityMan.ActivitySetToRestart()) {
					g_MenuMan.HandleTransitionIntoMenuLoop();
					BrowserStage("RunMenuLoop();"); RunMenuLoop();
				}
			}
			if (g_ActivityMan.ActivitySetToRestart()) {
				g_LoadingScreen.DrawLoadingSplash();
				BrowserStage("g_WindowMan.UploadFrame();"); g_WindowMan.UploadFrame();
				if (!g_ActivityMan.RestartActivity()) {
					break;
				}
			}
			if (g_ActivityMan.ActivitySetToResume()) {
				BrowserStage("g_ActivityMan.ResumeActivity();"); g_ActivityMan.ResumeActivity();
				g_PerformanceMan.ResetSimUpdateTimer();
				updateStartTime = g_TimerMan.GetAbsoluteTime();
			}
		}

#ifdef __EMSCRIPTEN__
		// Quitting from a menu (Exit, or Conquest's Quit Program) takes the page back
		// to its start screen (see main). Natively the loop draws one more game frame
		// first; on Conquest's map no scene is loaded, and that frame trips
		// CameraMan::CheckOffset's assertion. There is nothing to show on the way out.
		if (System::IsSetToQuit()) {
			break;
		}
		BrowserNoteSimUpdates(browserSimUpdates);
#endif
		updateEndAndDrawStartTime = g_TimerMan.GetAbsoluteTime();
		updateTotalTime = updateEndAndDrawStartTime - updateStartTime;
		drawStartTime = updateEndAndDrawStartTime;

		BrowserStage("g_FrameMan.Draw();"); g_FrameMan.Draw();
		BrowserStage("g_WindowMan.DrawPostProcessBuffer();"); g_WindowMan.DrawPostProcessBuffer();
		BrowserStage("g_WindowMan.UploadFrame();"); g_WindowMan.UploadFrame();
#ifdef __EMSCRIPTEN__
		BrowserEditorRestoreCheck();
#endif

		drawTotalTime = g_TimerMan.GetAbsoluteTime() - drawStartTime;
		g_PerformanceMan.UpdateMSPF(updateTotalTime, drawTotalTime);
	}
}

/// <summary>
/// Self-invoking lambda that installs exception handlers before Main is executed.
/// </summary>
static const bool RTESetExceptionHandlers = []() {
	RTEError::SetExceptionHandlers();
	return true;
}();

/// <summary>
/// Implementation of the main function.
/// </summary>
int main(int argc, char** argv) {
#ifdef __EMSCRIPTEN__
	BrowserMarkEngineThread();
	BrowserStopOnFailedNew();
#endif
	install_allegro(SYSTEM_NONE, &errno, std::atexit);
	loadpng_init();

	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD );

	SDL_SetHint(SDL_HINT_MOUSE_AUTO_CAPTURE, "0");
	SDL_SetHint("SDL_ALLOW_TOPMOST", "0");
	SDL_HideCursor();

	if (std::filesystem::exists("Base.rte/gamecontrollerdb.txt")) {
		SDL_AddGamepadMappingsFromFile("Base.rte/gamecontrollerdb.txt");
	}

#ifdef WIN32
	// Stops framespiking from our child threads being sat on for too long
	// TODO: use a better thread system that'll do what we want ASAP instead of letting the OS schedule all over us
	// Disabled for now because windows is great and this means when the game lags out it freezes the entire computer. Which we wouldn't expect with anything but REALTIME priority.
	// Because apparently high priority class is preferred over "processing mouse input"?!
	// SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
#endif // WIN32

	// argv[0] actually unreliable for exe path and name, because of course, why would it be, why would anything be simple and make sense.
	// Just use it anyway until some dumb edge case pops up and it becomes a problem.
	System::Initialize(argv[0]);
	SeedRNG();

	DetectSceneDumpMode(argc, argv);

	InitializeManagers();

	HandleMainArgs(argc, argv);

	#ifdef __EMSCRIPTEN__
	std::puts("Browser startup: loading data modules");
	BrowserCooperativeYield();
	#endif
	g_PresetMan.LoadAllDataModules();
#ifdef __EMSCRIPTEN__
	std::puts("Browser startup: all modules loaded");
	BrowserCooperativeYield();
#endif

	if (s_SimulationSteps > 0) {
		const bool simulated = RunDeterministicSimulation(s_SimulationSteps);
		// Dumping a world that never loaded dereferences a null terrain.
		const int saved = simulated ? g_FrameMan.SaveWorldToPNG("SimulationDump") : -1;
		std::printf("Simulation dump: %s\n", saved == 0 ? "written" : "failed");
		std::fflush(stdout);
#ifdef __EMSCRIPTEN__
		MAIN_THREAD_EM_ASM({ if (Module['onExit']) Module['onExit'](0); });
#endif
		return EXIT_SUCCESS;
	}

	if (!s_SceneToDumpPreview.empty()) {
		std::printf("RNG before scene load: %llu draws, hash %016llx\n",
		            static_cast<unsigned long long>(g_RandomGenerator.GetDrawCount()),
		            static_cast<unsigned long long>(g_RandomGenerator.GetDrawHash()));
		// Reseed exactly as Activity::Start does before it loads a Scene, so the dump
		// shows the world the game itself generates for this Scene, independent of
		// whatever drew from the stream during startup.
		SeedRNG();
		int loaded = g_SceneMan.SetSceneToLoad(s_SceneToDumpPreview, true, false);
		if (loaded >= 0) {
			loaded = g_SceneMan.LoadScene();
		}
		if (loaded < 0) {
			std::printf("Could not load scene \"%s\"\n", s_SceneToDumpPreview.c_str());
			return EXIT_FAILURE;
		}
		std::printf("RNG after scene load: %llu draws, hash %016llx\n",
		            static_cast<unsigned long long>(g_RandomGenerator.GetDrawCount()),
		            static_cast<unsigned long long>(g_RandomGenerator.GetDrawHash()));
		const int saved = s_DumpFullWorld ? g_FrameMan.SaveWorldToPNG("SceneWorldDump") : g_FrameMan.SaveWorldPreviewToPNG("ScenePreviewDump");
		std::printf("Screenshot directory: %s\n", System::GetScreenshotDirectory().c_str());
		std::printf("Scene preview dump for \"%s\": %s\n", s_SceneToDumpPreview.c_str(), saved == 0 ? "written" : "failed");
		return saved == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
	}

	if (!System::IsInExternalModuleValidationMode()) {
		// Load the different input device icons. This can't be done during UInputMan::Create() because the icon presets don't exist so we need to do this after modules are loaded.
		g_UInputMan.LoadDeviceIcons();

		if (g_ConsoleMan.LoadWarningsExist()) {
			g_ConsoleMan.PrintString("WARNING: Encountered non-fatal errors during module loading!\nSee \"LogLoadingWarning.txt\" for information.");
			g_ConsoleMan.SaveLoadWarningLog("LogLoadingWarning.txt");
			// Open the console so the user is aware there are loading warnings.
			g_ConsoleMan.SetEnabled(true);
		} else {
			// Delete an existing log if there are no warnings so there's less junk in the root folder.
			if (std::filesystem::exists(System::GetWorkingDirectory() + "LogLoadingWarning.txt")) {
				std::remove("LogLoadingWarning.txt");
			}
		}

		if (!g_ActivityMan.Initialize()) {
			RunMenuLoop();
		}

		RunGameLoop();
	}

#ifdef __EMSCRIPTEN__
	// The page is going away: end main's stack now rather than shut down in order,
	// which would only hold up the navigation. See BrowserPageUnloading.
	if (BrowserPageUnloading()) {
		emscripten_force_exit(EXIT_SUCCESS);
	}
#endif
	g_ThreadMan.GetPriorityThreadPool().wait_for_tasks();
	g_ThreadMan.GetBackgroundThreadPool().wait_for_tasks();
#ifdef __EMSCRIPTEN__
	// A page cannot close its own tab, so quitting returns to the page's start
	// screen (index.html reloads it). Anything a save just wrote is persisted to
	// IndexedDB first; the native teardown is skipped, since the reload frees it all.
	BrowserReturnToStartScreen();
	emscripten_force_exit(EXIT_SUCCESS);
#endif

	DestroyManagers();

	allegro_exit();
	SDL_Quit();

#ifdef __EMSCRIPTEN__
	// Returning from main leaves the page running with worker threads alive, so
	// the runtime never reports an exit. Destroying the SDL window also resizes
	// the canvas to zero, which would otherwise leave the player looking at a
	// blank page with no indication that the game closed.
	MAIN_THREAD_EM_ASM({
		if (Module['onExit']) {
			Module['onExit'](0);
		}
	});
#endif

	return EXIT_SUCCESS;
}

#ifdef _WIN32
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) { return main(__argc, __argv); }
#endif
