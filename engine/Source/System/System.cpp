#include "System.h"

#include "RTETools.h"
#ifdef SYSTEM_MINIZIP
#include <minizip/unzip.h>
#else
#include "unzip.h"
#endif

#include "RTEError.h"

// Convenience macro to not have to write this out.
#define _LINUX_OR_MACOSX_ (__unix__ || (__APPLE__ && __MACH__))

#ifdef _WIN32
#include "Windows.h"
#elif defined _LINUX_OR_MACOSX_
#include <unistd.h>
#include <sys/stat.h>
#endif

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <utility>
#include <vector>

using namespace RTE;

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <chrono>
#include <cstdio>

// emscripten_sleep(0) returns through setTimeout, which Chrome clamps to 4 ms once
// timers nest five deep, as a yield from inside a resumed timer callback always
// does; that left the page idle for a fifth of every data load. A message posted
// to a MessageChannel is an ordinary task with no clamp. scheduler.yield() would
// resume sooner still, but it outranks ordinary tasks, so page timers and the
// messages workers send to this thread would wait for the whole load.
EM_ASYNC_JS(void, BrowserYieldToEventLoop, (), {
	if (!Module['yieldChannel']) {
		const channel = new MessageChannel();
		channel.port1.onmessage = () => {
			const resume = Module['pendingResume'];
			Module['pendingResume'] = null;
			if (resume) {
				resume();
			}
		};
		Module['yieldChannel'] = channel;
	}
	await new Promise(resolve => {
		Module['pendingResume'] = resolve;
		Module['yieldChannel'].port2.postMessage(0);
	});
});

// A suspended WebAssembly stack is a garbage collection root in V8, and one left
// suspended when the page goes away is never released: after every reload or
// navigation the tab kept the whole previous page, 350 MB of game files and all,
// until it spent most of its time collecting garbage. So when the page is hidden
// for good the pending wait is resumed, nothing suspends any more, the loops quit,
// and main leaves (see main), which ends the stack.
EM_JS(int, BrowserPageUnloadingFlag, (), {
	if (!Module['pageHideHooked']) {
		Module['pageHideHooked'] = true;
		addEventListener('pagehide', () => {
			Module['pageUnloading'] = true;
			const resume = Module['pendingResume'];
			Module['pendingResume'] = null;
			if (resume) {
				resume();
			}
		});
	}
	return Module['pageUnloading'] ? 1 : 0;
});

bool RTE::BrowserPageUnloading() {
	return BrowserPageUnloadingFlag() != 0;
}

// The page owns the filesystem, whichever thread the engine runs on, so it writes
// the saves to IndexedDB itself when the program exits and then reloads (index.html).
void RTE::BrowserReturnToStartScreen() {
	MAIN_THREAD_EM_ASM({ Module['returnToStartScreen'] = true; });
}

void RTE::BrowserNoteTextInput(bool active) {
	MAIN_THREAD_ASYNC_EM_ASM({ Module['textInputActive'] = $0 !== 0; }, active ? 1 : 0);
}

void RTE::BrowserWaitForSoundFiles() {
	const auto waiting = [] { return MAIN_THREAD_EM_ASM_INT({ return Module['soundFilesComplete'] === false ? 1 : 0; }) != 0; };
	if (!waiting()) {
		return;
	}
	std::puts("Browser sounds: waiting for the last sound files before the Activity starts");
	MAIN_THREAD_EM_ASM({ Module['showSoundFileWait'] && Module['showSoundFileWait'](true); });
	while (waiting() && !BrowserPageUnloading()) {
		emscripten_sleep(100);
		BrowserNoteEventLoopTurn();
	}
	MAIN_THREAD_EM_ASM({ Module['showSoundFileWait'] && Module['showSoundFileWait'](false); });
}

namespace {
	thread_local bool t_EngineThread = false;
} // namespace

void RTE::BrowserMarkEngineThread() {
	t_EngineThread = true;
}

bool RTE::BrowserIsEngineThread() {
	return t_EngineThread;
}

namespace {
	using BrowserClock = std::chrono::steady_clock;
	// Only the engine thread yields, so these need no synchronisation. Unset
	// until the first yield, so the page's wait for the Play click is not reported.
	BrowserClock::time_point s_LastEventLoopTurn;
	int s_YieldCount = 0;
	double s_YieldMilliseconds = 0.0;
} // namespace

void RTE::BrowserNoteEventLoopTurn() {
	s_LastEventLoopTurn = BrowserClock::now();
}

namespace {
	int s_SimUpdates = 0;
	int s_FramesWithNoSimUpdate = 0;
	int s_FramesWithSeveralSimUpdates = 0;
} // namespace

double RTE::BrowserTakeFrameAgeMs() {
	return EM_ASM_DOUBLE({
		if (!Module['frameTimestampFresh']) {
			return -1;
		}
		Module['frameTimestampFresh'] = false;
		return performance.now() - Module['frameTimestamp'];
	});
}

void RTE::BrowserNoteSimUpdates(int updates) {
	s_SimUpdates += updates;
	s_FramesWithNoSimUpdate += updates == 0 ? 1 : 0;
	s_FramesWithSeveralSimUpdates += updates > 1 ? 1 : 0;
}

void RTE::BrowserTakeSimUpdateStats(int& updates, int& framesWithNone, int& framesWithSeveral) {
	updates = s_SimUpdates;
	framesWithNone = s_FramesWithNoSimUpdate;
	framesWithSeveral = s_FramesWithSeveralSimUpdates;
	s_SimUpdates = s_FramesWithNoSimUpdate = s_FramesWithSeveralSimUpdates = 0;
}

void RTE::BrowserTakeYieldStats(int& count, double& milliseconds) {
	count = s_YieldCount;
	milliseconds = s_YieldMilliseconds;
	s_YieldCount = 0;
	s_YieldMilliseconds = 0.0;
}

void RTE::BrowserCooperativeYield() {
	// Only the engine thread, inside main, can suspend; a pool thread must never
	// try to yield this way.
	if (!BrowserIsEngineThread()) {
		return;
	}
	const BrowserClock::time_point now = BrowserClock::now();
	// Yielding is not free: the browser may paint or run timers before resuming.
	// Only yield when the event loop has not run for about a display frame, which
	// the per-frame wait already guarantees during ordinary play.
	const double gap = std::chrono::duration<double, std::milli>(now - s_LastEventLoopTurn).count();
	if (gap < 16.0 || BrowserPageUnloading()) {
		return;
	}
	// Report stretches with no yield at all, so a block that starves audio and
	// input can be located against the surrounding load phase logging.
	static const bool reportBlocks = MAIN_THREAD_EM_ASM_INT({ return Module['perfDiagnostics'] === true; });
	if (reportBlocks && gap > 250.0 && s_LastEventLoopTurn != BrowserClock::time_point()) {
		std::fprintf(stderr, "Browser long block: %.0f ms with no yield\n", gap);
	}
	BrowserYieldToEventLoop();
	s_LastEventLoopTurn = BrowserClock::now();
	++s_YieldCount;
	s_YieldMilliseconds += std::chrono::duration<double, std::milli>(s_LastEventLoopTurn - now).count();
}
#else
void RTE::BrowserMarkEngineThread() {}
bool RTE::BrowserIsEngineThread() {
	return false;
}
bool RTE::BrowserPageUnloading() {
	return false;
}
void RTE::BrowserReturnToStartScreen() {}
void RTE::BrowserNoteTextInput(bool active) {}
void RTE::BrowserWaitForSoundFiles() {}
double RTE::BrowserTakeFrameAgeMs() {
	return -1.0;
}
void RTE::BrowserNoteSimUpdates(int updates) {}
void RTE::BrowserTakeSimUpdateStats(int& updates, int& framesWithNone, int& framesWithSeveral) {
	updates = framesWithNone = framesWithSeveral = 0;
}
void RTE::BrowserCooperativeYield() {}
void RTE::BrowserNoteEventLoopTurn() {}
void RTE::BrowserTakeYieldStats(int& count, double& milliseconds) {
	count = 0;
	milliseconds = 0.0;
}
#endif


bool System::s_Quit = false;
bool System::s_LogToCLI = false;
bool System::s_SceneDumpMode = false;
bool System::s_DeterministicSimulation = false;
unsigned System::s_ParallelPhaseMask = 0;
bool System::s_ExternalModuleValidation = false;
std::string System::s_ThisExePathAndName = "";
std::string System::s_WorkingDirectory = ".";
std::vector<size_t> System::s_WorkingTree;
std::filesystem::file_time_type System::s_ProgramStartTime = std::filesystem::file_time_type::clock::now();
bool System::s_CaseSensitive = true;
const std::string System::s_DataDirectory = "Data/";
const std::string System::s_ScreenshotDirectory = "ScreenShots/";
const std::string System::s_ModDirectory = "Mods/";
const std::string System::s_UserdataDirectory = "Userdata/";
const std::string System::s_ModulePackageExtension = ".rte";
const std::string System::s_ZippedModulePackageExtension = ".zip";
const std::unordered_set<std::string> System::s_SupportedExtensions = {".ini", ".txt", ".lua", ".cfg", ".bmp", ".png", ".jpg", ".jpeg", ".wav", ".ogg", ".mp3", ".flac"};

void System::Initialize(const char* thisExePathAndName) {
	s_ThisExePathAndName = std::filesystem::path(thisExePathAndName).generic_string();

	s_WorkingDirectory = std::filesystem::current_path().generic_string();

#ifdef __APPLE__
	// Get a reference to the main bundle
	CFBundleRef mainBundle = CFBundleGetMainBundle();

	if (!mainBundle) {
		RTEAbort("Could not get a reference to the main App Bundle! This may be due to a missing argv[0] path.")
	}
	// Get the URL of the application bundle
	CFURLRef bundleURL = CFBundleCopyBundleURL(mainBundle);

	if (!bundleURL) {
		RTEAbort("Could not copy App Bundle URL, the bundle does not exist!")
	}

	// Convert the URL to a C string
	char pathBuffer[PATH_MAX];
	if (CFURLGetFileSystemRepresentation(bundleURL, true, (UInt8*)pathBuffer, sizeof(pathBuffer))) {
		// bundlePath now contains the path to the application bundle as a C string
		auto bundlePath = std::filesystem::path(pathBuffer);

		if (std::filesystem::exists(bundlePath) && bundlePath.extension() == ".app") {
			auto workingDirPath = bundlePath.parent_path();
			std::filesystem::current_path(workingDirPath);
			s_WorkingDirectory = workingDirPath.generic_string();
		}

	} else {
		CFRelease(bundleURL);
		RTEAbort("Could not write App Bundle URL to a readable representation! The bundle path may exceed the local PATH_MAX.")
	}
	// Release the CFURL object
	CFRelease(bundleURL);

#endif
	if (s_WorkingDirectory.back() != '/') {
		s_WorkingDirectory.append("/");
	}

	if (!PathExistsCaseSensitive(s_WorkingDirectory + s_ScreenshotDirectory)) {
		MakeDirectory(s_WorkingDirectory + s_ScreenshotDirectory);
	}
	if (!PathExistsCaseSensitive(s_WorkingDirectory + s_ModDirectory)) {
		MakeDirectory(s_WorkingDirectory + s_ModDirectory);
	}
	if (!PathExistsCaseSensitive(s_WorkingDirectory + s_UserdataDirectory)) {
		MakeDirectory(s_WorkingDirectory + s_UserdataDirectory);
	}

#ifdef _WIN32
	// Consider Settings.ini not existing as first time boot, then create quick launch files if they are missing.
	if (!std::filesystem::exists(s_WorkingDirectory + s_UserdataDirectory + "Settings.ini")) {
		std::array<std::pair<const std::string, const std::string>, 7> quickLaunchFiles = {{
		    {"Launch Actor Editor.bat", R"(start "" "Cortex Command.exe" -editor "ActorEditor")"},
		    {"Launch Area Editor.bat", R"(start "" "Cortex Command.exe" -editor "AreaEditor")"},
		    {"Launch Assembly Editor.bat", R"(start "" "Cortex Command.exe" -editor "AssemblyEditor")"},
		    {"Launch Gib Editor.bat", R"(start "" "Cortex Command.exe" -editor "GibEditor")"},
		    {"Launch Scene Editor.bat", R"(start "" "Cortex Command.exe" -editor "SceneEditor")"},
#ifdef TARGET_MACHINE_X86
		    {"Start Dedicated Server x86.bat", R"(start "" "Cortex Command x86.exe" -server 8000)"},
#else
		    {"Start Dedicated Server.bat", R"(start "" "Cortex Command.exe" -server 8000)"},
#endif
		}};
		for (const auto& [fileName, fileContent]: quickLaunchFiles) {
			if (std::filesystem::path filePath = s_WorkingDirectory + fileName; !std::filesystem::exists(filePath)) {
				std::ofstream fileStream(filePath);
				fileStream << fileContent;
				fileStream.close();
			}
		}
	}
#endif
}

bool System::MakeDirectory(const std::string& pathToMake) {
	bool createResult = std::filesystem::create_directory(pathToMake);
	if (createResult) {
		std::filesystem::permissions(pathToMake, std::filesystem::perms::owner_all | std::filesystem::perms::group_read | std::filesystem::perms::group_exec | std::filesystem::perms::others_read | std::filesystem::perms::others_exec, std::filesystem::perm_options::add);
	}
	return createResult;
}

bool System::PathExistsCaseSensitive(const std::string& pathToCheck) {
#ifdef __EMSCRIPTEN__
	// MEMFS/IDBFS already enforce case. Check the live filesystem, including
	// restored saves and newly installed mods, without native path hashing.
	return std::filesystem::exists(pathToCheck);
#endif
	// Use Hash for compiler independent hashing.
	if (s_CaseSensitive) {
		if (s_WorkingTree.empty()) {
			for (const std::filesystem::directory_entry& directoryEntry: std::filesystem::recursive_directory_iterator(s_WorkingDirectory, std::filesystem::directory_options::follow_directory_symlink)) {
				s_WorkingTree.emplace_back(Hash(directoryEntry.path().generic_string().substr(s_WorkingDirectory.length())));
			}
		}
		if (std::find(s_WorkingTree.begin(), s_WorkingTree.end(), Hash(pathToCheck)) != s_WorkingTree.end()) {
			return true;
		} else if (std::filesystem::exists(pathToCheck) && std::filesystem::last_write_time(pathToCheck) > s_ProgramStartTime) {
			s_WorkingTree.emplace_back(Hash(pathToCheck));
			return true;
		}
		return false;
	}
	return std::filesystem::exists(pathToCheck);
}

void System::EnableLoggingToCLI() {
#ifdef _WIN32
	// Create a console instance for the current process
	if (AllocConsole()) {
		CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
		GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &consoleInfo);
		consoleInfo.dwSize.X = 192;
		SetConsoleScreenBufferSize(GetStdHandle(STD_OUTPUT_HANDLE), consoleInfo.dwSize);

		static std::ofstream consoleOutStream("CONOUT$", std::ios::out);
		// Set std::cout stream buffer to consoleOut's buffer to redirect the output
		std::cout.rdbuf(consoleOutStream.rdbuf());
	} else {
		MessageBox(nullptr, "Failed to allocate a console instance for this process, game console output will not be printed to CLI!", "RTE Warning! (>_<)", MB_OK);
		return;
	}
#endif
	s_LogToCLI = true;
}

void System::PrintLoadingToCLI(const std::string& reportString, bool newItem) {
	if (newItem) {
		std::cout << std::endl;
	}
	// Overwrite current line
	std::cout << "\r";
	size_t startPos = 0;
	// Just make sure to really overwrite all old output, " - done! ✓" is shorter than "reading line 700"
	std::string unicodedOutput = reportString + "            ";

#if _LINUX_OR_MACOSX_
	// Colorize output with ANSI escape code
	std::string greenTick = "\033[1;32m✓\033[0;0m";
	std::string yellowDot = "\033[1;33m•\033[0;0m";
#elif _WIN32
	// Fancy colors don't work for the Windows console so just replace with blanks
	std::string greenTick = "";
	std::string yellowDot = "";
	// Also replace tab with 4 spaces because tab is super wide in the Windows console
	size_t tabPos = 0;
	while ((tabPos = unicodedOutput.find("\t")) != std::string::npos) {
		unicodedOutput.replace(tabPos, 1, "    ");
	}
#endif
	// Convert all ✓ characters to unicode, it's the 42th from last character in CC's custom font
	while ((startPos = unicodedOutput.find(-42, startPos)) != std::string::npos) {
		unicodedOutput.replace(startPos, 1, greenTick);
		// We don't have to check indices we just overwrote
		startPos += greenTick.length();
	}
	startPos = 0;

	// Convert all • characters to unicode
	while ((startPos = unicodedOutput.find(-43, startPos)) != std::string::npos) {
		unicodedOutput.replace(startPos, 1, yellowDot);
		startPos += yellowDot.length();
	}
	std::cout << unicodedOutput << std::flush;
}

void System::PrintToCLI(const std::string& stringToPrint) {
#if _LINUX_OR_MACOSX_
	std::string outputString = stringToPrint;
	// Color the words ERROR: and SYSTEM: red
	std::regex regexError("(ERROR|SYSTEM):");
	outputString = std::regex_replace(outputString, regexError, "\033[1;31m$&\033[0;0m");

	// Color .rte-paths green
	std::regex regexPath("\\w*\\.rte\\/(\\w| |\\.|\\/)*(\\/|\\.bmp|\\.png|\\.wav|\\.ogg|\\.flac||\\.lua|\\.ini)");
	outputString = std::regex_replace(outputString, regexPath, "\033[1;32m$&\033[0;0m");

	// Color names in quotes yellow, they have to start with an upper case letter to sort out apostrophes
	std::regex regexName("(\"[A-Z].*\"|\'[A-Z].*\')");
	outputString = std::regex_replace(outputString, regexName, "\033[1;33m$&\033[0;0m");

	std::cout << "\r" << outputString << std::endl;
#elif _WIN32
	// All the fancy formatting doesn't work with the Windows console so just print the string as it is
	std::cout << "\r" << stringToPrint << std::endl;
#endif
}

std::string System::ExtractZippedDataModule(const std::string& zippedModulePath) {
	std::string zippedModuleName = System::GetModDirectory() + std::filesystem::path(zippedModulePath).filename().generic_string();

	unzFile zippedModule = unzOpen(zippedModuleName.c_str());
	std::stringstream extractionProgressReport;
	bool abortExtract = false;

	if (!zippedModule) {
		bool makeDirResult = false;
		if (!std::filesystem::exists(s_WorkingDirectory + "_FailedExtract")) {
			makeDirResult = MakeDirectory(s_WorkingDirectory + "_FailedExtract");
		}
		if (makeDirResult) {
			extractionProgressReport << "Failed to extract Data module from: " + zippedModuleName + " - Moving zip file to failed extract directory!\n";
			std::filesystem::rename(s_WorkingDirectory + zippedModuleName, s_WorkingDirectory + "_FailedExtract/" + zippedModuleName);
		} else {
			extractionProgressReport << "Failed to extract Data module from: " + zippedModuleName + " - Failed to create directory to move zip file into, deleting zip file!\n";
			std::remove((s_WorkingDirectory + zippedModuleName).c_str());
		}
		return extractionProgressReport.str();
	}

	unz_global_info zippedModuleInfo;
	if (unzGetGlobalInfo(zippedModule, &zippedModuleInfo) != UNZ_OK) {
		extractionProgressReport << "\tSkipped: " + zippedModuleName + " - Could not read global file info!\n";
		abortExtract = true;
	}
	std::array<char, s_FileBufferSize> fileBuffer;

	// Go through and extract every file inside this zip, overwriting every colliding file that already exists in the install directory.
	for (size_t i = 0; i < zippedModuleInfo.number_entry && !abortExtract; ++i) {
		unz_file_info currentFileInfo;
		std::array<char, s_MaxFileName> outputFileInfoData;
		if (unzGetCurrentFileInfo(zippedModule, &currentFileInfo, outputFileInfoData.data(), s_MaxFileName, nullptr, 0, nullptr, 0) != UNZ_OK) {
			extractionProgressReport << "\tSkipped: " + std::string(outputFileInfoData.data()) + " - Could not read file info!\n";
			continue;
		}
		std::string outputFileName = System::GetModDirectory() + outputFileInfoData.data();
#ifdef _WIN32
		// TODO: Windows 10 adds support for paths over 260 characters so investigate how to get Windows version and whether the setting is enabled at runtime.
		// Windows doesn't support paths over 260 characters long.
		if ((s_WorkingDirectory + outputFileName).length() >= MAX_PATH) {
			extractionProgressReport << "\tSkipped file: " + outputFileName + " - Full path to file exceeds 260 characters!\n";
			continue;
		}
#endif
		// Check if the directory we are trying to extract into exists, and if not, create it.
		std::string outputFileDirectory = outputFileName.substr(0, outputFileName.find_last_of("/\\") + 1);
		if (!std::filesystem::exists(outputFileDirectory)) {
			if (!MakeDirectory(s_WorkingDirectory + outputFileDirectory)) {
				extractionProgressReport << "\tFailed to create directory: " + outputFileName + " - Extraction aborted!\n";
				abortExtract = true;
				continue;
			} else {
				extractionProgressReport << "\tCreated directory: " + outputFileName + "\n";
			}
		}
		// If the output file is a directly, go the next entry listed in the zip file.
		if (std::filesystem::is_directory(outputFileName)) {
			unzCloseCurrentFile(zippedModule);
			if ((i + 1) < zippedModuleInfo.number_entry && unzGoToNextFile(zippedModule) != UNZ_OK) {
				extractionProgressReport << "\tCould not read next file inside zip - Extraction aborted!\n";
				abortExtract = true;
			}
			continue;
		}

		// Validate so only certain file types are extracted.
		std::string fileExtension = std::filesystem::path(outputFileName).extension().generic_string();
		std::transform(fileExtension.begin(), fileExtension.end(), fileExtension.begin(), tolower);

		if (s_SupportedExtensions.find(fileExtension) == s_SupportedExtensions.end()) {
			extractionProgressReport << "\tSkipped file: " + outputFileName + " - Bad extension!\n";
			unzCloseCurrentFile(zippedModule);

			if ((i + 1) < zippedModuleInfo.number_entry && unzGoToNextFile(zippedModule) != UNZ_OK) {
				extractionProgressReport << "\tCould not read next file inside zip - Extraction aborted!\n";
				abortExtract = true;
			}
			continue;
		}

		if (unzOpenCurrentFile(zippedModule) != UNZ_OK) {
			extractionProgressReport << "\tSkipped file: " + zippedModuleName + " - Could not open file!\n";
		} else {
			FILE* outputFile = fopen(outputFileName.c_str(), "wb");
			if (outputFile == nullptr) {
				extractionProgressReport << "\tSkipped file: " + outputFileName + " - Could not open/create destination file!\n";
			} else {
				// Write the entire file out, reading in buffer size chunks and spitting them out to the output stream.
				bool abortWrite = false;
				int bytesRead = 0;
				int totalBytesRead = 0;
				do {
					bytesRead = unzReadCurrentFile(zippedModule, fileBuffer.data(), s_FileBufferSize);
					totalBytesRead += bytesRead;

					if (bytesRead < 0) {
						extractionProgressReport << "\tSkipped file: " + outputFileName + " - File is empty or corrupt!\n";
						abortWrite = true;
						// Sanity check how damn big this file we're writing is becoming. could prevent zip bomb exploits: http://en.wikipedia.org/wiki/Zip_bomb
					} else if (totalBytesRead >= s_MaxUnzippedFileSize) {
						extractionProgressReport << "\tSkipped file: " + outputFileName + " - File is too large, extract it manually!\n";
						abortWrite = true;
					}
					if (abortWrite) {
						break;
					}
					fwrite(fileBuffer.data(), bytesRead, 1, outputFile);
					// Keep going while bytes are still being read (0 means end of file).
				} while (bytesRead > 0 && outputFile);

				fclose(outputFile);
			}

			unzCloseCurrentFile(zippedModule);

			extractionProgressReport << "\tExtracted file: " + outputFileName + "\n";
		}

		if ((i + 1) < zippedModuleInfo.number_entry && unzGoToNextFile(zippedModule) != UNZ_OK) {
			extractionProgressReport << "\tCould not read next file inside zip - Extraction aborted!\n";
			abortExtract = true;
		}
	}
	unzClose(zippedModule);

	if (!abortExtract) {
		extractionProgressReport << "Successfully extracted Data Module from: " + zippedModuleName + " - Deleting zip file!\n";
		std::remove((s_WorkingDirectory + zippedModuleName).c_str());
	}

	return extractionProgressReport.str();
}

int System::ASCIIFileContainsString(const std::string& filePath, const std::string_view& findString) {
	std::ifstream inputStream(filePath, std::ios::binary);
	if (!inputStream.is_open()) {
		return -1;
	} else {
		size_t fileSize = static_cast<size_t>(std::filesystem::file_size(filePath));
		std::vector<unsigned char> rawData(fileSize);
		inputStream.read(reinterpret_cast<char*>(&rawData[0]), fileSize);
		inputStream.close();

		return (std::search(rawData.begin(), rawData.end(), findString.begin(), findString.end()) != rawData.end()) ? 0 : 1;
	}
}
