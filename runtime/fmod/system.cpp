#include "internal.hpp"

#include <algorithm>
#include <chrono>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/threading.h>

#include <cstdio>

namespace FMOD {
	/// An ma_engine_init call the page's thread makes on the engine's behalf.
	struct EngineStart {
		ma_engine* engine;
		const ma_engine_config* config;
		ma_result result = MA_ERROR;
		std::atomic<bool> done{false};
	};
} // namespace FMOD

// Web Audio exists only on the page's main thread: miniaudio creates the
// AudioContext and its worklet there, through `window`, and waits for the worklet
// with emscripten_sleep. When the engine runs in a worker, System::init has the
// page's thread start the engine through this export, which JSPI lets suspend
// (the game exports it; see CMakeLists.txt).
extern "C" void cortex_audio_start_engine(FMOD::EngineStart* start) {
	start->result = ma_engine_init(start->config, start->engine);
	start->done.store(true);
	start->done.notify_all();
}

extern "C" void cortex_audio_start_failed(FMOD::EngineStart* start) {
	start->done.store(true);
	start->done.notify_all();
}

namespace {
	void StopEngine(ma_engine* engine) {
		ma_engine_uninit(engine);
	}
} // namespace
#endif

// The System: the mixer itself, its settings, and the channels it plays.
namespace FMOD {
	FMOD_RESULT Result(ma_result status) {
		return status == MA_SUCCESS ? FMOD_OK : FMOD_ERR_INTERNAL;
	}

	System::System() :
	    state(std::make_unique<SystemState>()) {}

	System::~System() {
		state->channels.clear();
		state->groups.clear();
		state->effects.clear();
		if (state->initialized) {
#ifdef __EMSCRIPTEN__
			// The device goes away where it was made, on the page's thread.
			if (ma_engine_get_device(&state->engine) && !emscripten_is_main_browser_thread()) {
				emscripten_sync_run_in_main_runtime_thread(EM_FUNC_SIG_VI, StopEngine, &state->engine);
			} else
#endif
			{
				ma_engine_uninit(&state->engine);
			}
		}
	}

	FMOD_RESULT System_Create(System** system, unsigned int version) {
		if (!system || version != FMOD_VERSION) {
			return FMOD_ERR_INVALID_PARAM;
		}
		*system = new System;
		return FMOD_OK;
	}

	FMOD_RESULT System::release() {
		delete this;
		return FMOD_OK;
	}

	FMOD_RESULT System::init(int maximumChannels, FMOD_INITFLAGS flags, void*) {
		ma_engine_config config = ma_engine_config_init();
		config.channels = c_MixChannels;
		config.sampleRate = c_MixSampleRate;
		// FMOD_INIT_MIX_FROM_UPDATE has the caller pull the mix (the tests do); no device.
		config.noDevice = (flags & FMOD_INIT_MIX_FROM_UPDATE) != 0;
		config.onProcess = MeasureEngineOutput;
		ma_result status = MA_ERROR;
#ifdef __EMSCRIPTEN__
		if (!config.noDevice && !emscripten_is_main_browser_thread()) {
			EngineStart start{&state->engine, &config};
			MAIN_THREAD_ASYNC_EM_ASM({
				_cortex_audio_start_engine($0).catch(error => {
					err('Audio could not start: ' + error);
					_cortex_audio_start_failed($0);
				});
			}, &start);
			start.done.wait(false);
			status = start.result;
		} else
#endif
		{
			status = ma_engine_init(&config, &state->engine);
		}
		if (status != MA_SUCCESS) {
			return Result(status);
		}
		state->initialized = true;
		state->maxChannels = maximumChannels;
		state->decoded = std::make_unique<DecodedSounds>();
		LoadSoundFileList(*state);
#ifdef __EMSCRIPTEN__
		// Which Web Audio path miniaudio took, and whether its device started, is
		// otherwise invisible: a worklet that never runs sounds exactly like silence.
		if (AudioDiagnosticsEnabled()) {
			ma_device* device = ma_engine_get_device(&state->engine);
			std::fprintf(stderr, "Browser audio: device=%p state=%d backend=%s rate=%u channels=%u period=%u\n",
			             static_cast<void*>(device), device ? static_cast<int>(ma_device_get_state(device)) : -1,
			             device ? ma_get_backend_name(device->pContext->backend) : "none",
			             device ? device->sampleRate : 0u, device ? device->playback.channels : 0u,
			             device ? device->playback.internalPeriodSizeInFrames : 0u);
		}
#endif
		return createChannelGroup("Master", &state->master);
	}

	FMOD_RESULT System::getAdvancedSettings(FMOD_ADVANCEDSETTINGS* settings) {
		if (!settings) {
			return FMOD_ERR_INVALID_PARAM;
		}
		*settings = state->advanced;
		settings->cbSize = sizeof(*settings);
		return FMOD_OK;
	}

	FMOD_RESULT System::setAdvancedSettings(FMOD_ADVANCEDSETTINGS* settings) {
		if (!settings) {
			return FMOD_ERR_INVALID_PARAM;
		}
		state->advanced = *settings;
		return FMOD_OK;
	}

	FMOD_RESULT System::setSoftwareChannels(int count) {
		if (count < 1) {
			return FMOD_ERR_INVALID_PARAM;
		}
		state->softwareChannels = count;
		return FMOD_OK;
	}

	FMOD_RESULT System::getSoftwareChannels(int* count) {
		if (!count) {
			return FMOD_ERR_INVALID_PARAM;
		}
		*count = state->softwareChannels;
		return FMOD_OK;
	}

	FMOD_RESULT System::getSoftwareFormat(int* sampleRate, FMOD_SPEAKERMODE* speakerMode, int* rawSpeakers) {
		if (sampleRate) {
			*sampleRate = c_MixSampleRate;
		}
		if (speakerMode) {
			*speakerMode = FMOD_SPEAKERMODE_STEREO;
		}
		if (rawSpeakers) {
			*rawSpeakers = c_MixChannels;
		}
		return FMOD_OK;
	}

	FMOD_RESULT System::set3DSettings(float doppler, float distanceFactor, float rolloff) {
		if (distanceFactor <= 0 || rolloff < 0) {
			return FMOD_ERR_INVALID_PARAM;
		}
		state->distanceFactor = distanceFactor;
		state->doppler = doppler;
		state->rolloff = rolloff;
		return FMOD_OK;
	}

	FMOD_RESULT System::set3DNumListeners(int count) {
		if (count < 1 || count > 8) {
			return FMOD_ERR_INVALID_PARAM;
		}
		state->listenerCount = count;
		return FMOD_OK;
	}

	FMOD_RESULT System::set3DListenerAttributes(int listener, const FMOD_VECTOR* position, const FMOD_VECTOR*, const FMOD_VECTOR*, const FMOD_VECTOR*) {
		if (listener < 0 || listener >= 8 || !position) {
			return FMOD_ERR_INVALID_PARAM;
		}
		state->listeners[listener] = *position;
		return FMOD_OK;
	}

	FMOD_RESULT System::getMasterChannelGroup(ChannelGroup** group) {
		if (!group) {
			return FMOD_ERR_INVALID_PARAM;
		}
		*group = state->master;
		return FMOD_OK;
	}

	FMOD_RESULT System::createChannelGroup(const char*, ChannelGroup** group) {
		if (!group || !state->initialized) {
			return FMOD_ERR_INVALID_PARAM;
		}
		auto created = std::make_unique<ChannelGroup>();
		ControlState& control = *created->state;
		control.owner = this;
		control.group = true;
		control.parent = state->master;
		control.voice = std::make_unique<ma_sound>();
		const ma_result status = ma_sound_group_init(&state->engine, MA_SOUND_FLAG_NO_SPATIALIZATION, control.parent ? control.parent->state->voice.get() : nullptr, control.voice.get());
		if (status != MA_SUCCESS) {
			control.voice.reset();
			return Result(status);
		}
		control.active = true;
		control.paused = false;
		control.effects.push_back(nullptr);
		const FMOD_RESULT wired = Wire(control);
		if (wired != FMOD_OK) {
			return wired;
		}
		ma_sound_group_start(control.voice.get());
		*group = created.get();
		state->groups.push_back(std::move(created));
		return FMOD_OK;
	}

	FMOD_RESULT System::createDSPByType(FMOD_DSP_TYPE type, DSP** effect) {
		if (!effect) {
			return FMOD_ERR_INVALID_PARAM;
		}
		if (type != FMOD_DSP_TYPE_MULTIBAND_EQ && type != FMOD_DSP_TYPE_COMPRESSOR && type != FMOD_DSP_TYPE_LIMITER) {
			return FMOD_ERR_UNSUPPORTED;
		}
		auto created = std::make_unique<DSP>();
		DSPState& effectState = *created->state;
		effectState.type = type;
		// FMOD's defaults for the parameters the engine leaves alone.
		if (type == FMOD_DSP_TYPE_MULTIBAND_EQ) {
			effectState.parameters[1] = 8000;
			effectState.parameters[2] = 0.707F;
		} else if (type == FMOD_DSP_TYPE_LIMITER) {
			effectState.parameters[0] = 10;
		} else {
			effectState.parameters[0] = 0;
			effectState.parameters[1] = 2.5F;
			effectState.parameters[2] = 20;
			effectState.parameters[3] = 200;
		}
		*effect = created.get();
		state->effects.push_back(std::move(created));
		return FMOD_OK;
	}

	FMOD_RESULT System::playSound(Sound* sound, ChannelGroup* group, bool paused, Channel** channel) {
		if (!sound || !channel) {
			return FMOD_ERR_INVALID_PARAM;
		}
		std::lock_guard lock(state->mutex);
		// Reuse a channel whose sound has ended and been reported, or add one.
		Channel* playing = nullptr;
		for (auto& candidate : state->channels) {
			if (candidate->state->notified && !candidate->state->voice) {
				playing = candidate.get();
				break;
			}
		}
		if (!playing && state->channels.size() < static_cast<size_t>(state->maxChannels)) {
			state->channels.push_back(std::make_unique<Channel>());
			playing = state->channels.back().get();
			playing->state->index = static_cast<int>(state->channels.size()) - 1;
		}
		if (!playing) {
			// Every channel is in use. FMOD does not refuse the new sound: it steals the
			// least important channel (the highest priority number), the oldest of those,
			// and reports the stolen one as ended.
			for (auto& candidate : state->channels) {
				const ControlState& control = *candidate->state;
				if (!playing || control.priority > playing->state->priority || (control.priority == playing->state->priority && control.started < playing->state->started)) {
					playing = candidate.get();
				}
			}
			ControlState& stolen = *playing->state;
			stolen.active = false;
			if (!stolen.notified) {
				stolen.notified = true;
				if (stolen.callback) {
					stolen.callback(reinterpret_cast<FMOD_CHANNELCONTROL*>(playing), FMOD_CHANNELCONTROL_CHANNEL, FMOD_CHANNELCONTROL_CALLBACK_END, nullptr, nullptr);
				}
			}
			ReleaseChannel(*state, stolen);
			++state->stolenChannels;
		}
		const int index = playing->state->index;
		playing->state = std::make_unique<ControlState>();
		ControlState& control = *playing->state;
		control.owner = this;
		control.index = index;
		control.started = ++state->plays;
		control.source = sound;
		control.parent = group ? group : state->master;
		control.loops = sound->state->loops;
		control.voice = std::make_unique<ma_sound>();
		control.data = std::make_unique<PlaybackSource>();
		control.data->loops = control.loops;

		const SoundState& soundState = *sound->state;
		FMOD_RESULT initialized = FMOD_OK;
		if (DecodedPCM decoded = state->decoded->Find(soundState.path)) {
			initialized = control.data->Initialize(std::move(decoded));
			state->decoded->CountPlay(true);
		} else {
			// Read the file here, on the thread that called play, never from the mixer.
			SoundBytes bytes = soundState.compressed.lock();
			bool failed = false;
			if (!bytes) {
				bytes = ReadWholeFile(soundState.path);
				failed = SoundFileFailed(bytes);
				if (!SoundFileArrived(bytes)) {
					bytes.reset();
				}
				sound->state->compressed = bytes;
			}
			if (bytes) {
				if (soundState.sampleRate > 0 && static_cast<double>(soundState.frames) / soundState.sampleRate <= DecodedSounds::c_MaxSeconds) {
					state->decoded->Request(soundState.path, bytes);
				}
				initialized = control.data->Initialize(std::move(bytes));
				state->decoded->CountPlay(false);
			} else if (failed && control.loops >= 0 && FindSoundFile(*state, soundState.path)) {
				// The page could not download it (it keeps trying): silence as long as the
				// sound, which then ends as the sound would have.
				initialized = control.data->InitializeSilent(MixLength(soundState));
				++state->silentPlays;
				RequestSoundFile(*state, soundState.path);
			} else if (FindSoundFile(*state, soundState.path)) {
				// Still on its way: play silence until it arrives, then start from the
				// beginning (StartArrivedChannels), and ask for it ahead of the rest. So does
				// a sound that loops forever and could not be downloaded: silent or waiting,
				// it never ends, and this way it is heard once its file arrives.
				initialized = control.data->InitializeWaiting();
				control.waiting = true;
				++state->waitingPlays;
				RequestSoundFile(*state, soundState.path);
			} else {
				control.voice.reset();
				control.data.reset();
				control.notified = true;
				return FMOD_ERR_FILE_BAD;
			}
		}
		if (initialized != FMOD_OK) {
			control.voice.reset();
			control.data.reset();
			control.notified = true;
			return initialized;
		}
		const ma_result status = ma_sound_init_from_data_source(&state->engine, &control.data->base, MA_SOUND_FLAG_NO_SPATIALIZATION, control.parent->state->voice.get(), control.voice.get());
		if (status != MA_SUCCESS) {
			control.voice.reset();
			control.notified = true;
			return Result(status);
		}
		control.effects.push_back(nullptr);
		const FMOD_RESULT wired = Wire(control);
		if (wired != FMOD_OK) {
			control.voice.reset();
			control.notified = true;
			return wired;
		}
		control.active = true;
		*channel = playing;
		return playing->setPaused(paused);
	}

	FMOD_RESULT System::getChannel(int index, Channel** channel) {
		if (!channel || index < 0 || static_cast<size_t>(index) >= state->channels.size()) {
			return FMOD_ERR_INVALID_PARAM;
		}
		*channel = state->channels[index].get();
		return FMOD_OK;
	}

	FMOD_RESULT System::getChannelsPlaying(int* count, int* real) {
		int playing = 0;
		for (auto& channel : state->channels) {
			if (channel->state->active) {
				++playing;
			}
		}
		if (count) {
			*count = playing;
		}
		if (real) {
			*real = 0;
			for (auto& channel : state->channels) {
				if (channel->state->active && !channel->state->virtualized) {
					++*real;
				}
			}
		}
		return FMOD_OK;
	}

	void ReleaseChannel(SystemState& system, ControlState& control) {
		if (control.voice) {
			ma_sound_uninit(control.voice.get());
			control.voice.reset();
		}
		control.data.reset();
		for (DSP* effect : control.effects) {
			system.effects.erase(std::remove_if(system.effects.begin(), system.effects.end(), [effect](const std::unique_ptr<DSP>& owned) { return owned.get() == effect; }), system.effects.end());
		}
		control.effects.clear();
	}

	void RequestSoundFile(SystemState& system, const std::string& path) {
		const std::string listed = EnginePath(path);
		if (!system.requestedFiles.insert(listed).second) {
			return;
		}
#ifdef __EMSCRIPTEN__
		MAIN_THREAD_EM_ASM({
			if (Module['requestSoundFile']) {
				Module['requestSoundFile'](UTF8ToString($0));
			}
		}, listed.c_str());
#endif
	}

	void StartArrivedChannels(SystemState& system) {
		// Looking costs a few file calls for each waiting sound; every 50 ms is soon enough.
		const auto now = static_cast<unsigned long long>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
		if (now - system.lastArrivalCheckMs < 50) {
			return;
		}
		system.lastArrivalCheckMs = now;
		std::lock_guard lock(system.mutex);
		struct File {
			SoundBytes bytes; //!< Null until they arrive.
			bool failed = false;
		};
		std::unordered_map<std::string, File> files;
		for (auto& channel : system.channels) {
			ControlState& control = *channel->state;
			if (!control.waiting || !control.voice || !control.source) {
				continue;
			}
			SoundState& sound = *control.source->state;
			auto file = files.find(sound.path);
			if (file == files.end()) {
				File found{sound.compressed.lock()};
				if (!found.bytes) {
					found.bytes = ReadWholeFile(sound.path);
				}
				found.failed = SoundFileFailed(found.bytes);
				if (!SoundFileArrived(found.bytes)) {
					found.bytes.reset();
				}
				file = files.emplace(sound.path, std::move(found)).first;
			}
			if (file->second.failed) {
				// The page stopped waiting for it: silence as long as the sound, from its
				// beginning, as if the file had arrived now. A sound that loops forever
				// would never end either way, so it goes on waiting, until it arrives or is
				// told to stop looping (AudioMan::FinishIngameLoopingSounds).
				if (control.loops < 0) {
					continue;
				}
				control.waiting = false;
				control.data->Fail(MixLength(sound));
				++system.silentPlays;
				continue;
			}
			if (!file->second.bytes) {
				continue;
			}
			sound.compressed = file->second.bytes;
			control.waiting = false;
			if (control.data->Arrive(file->second.bytes) != FMOD_OK) {
				control.active = false;
				continue;
			}
			if (sound.sampleRate > 0 && static_cast<double>(sound.frames) / sound.sampleRate <= DecodedSounds::c_MaxSeconds) {
				system.decoded->Request(sound.path, file->second.bytes);
			}
			system.decoded->CountPlay(false);
		}
	}

	FMOD_RESULT System::update() {
		StartArrivedChannels(*state);
		// Only the highest-priority channels are audible, as FMOD virtualizes the rest.
		std::vector<Channel*> ended;
		std::vector<Channel*> ranked;
		for (auto& channel : state->channels) {
			if (channel->state->active) {
				ranked.push_back(channel.get());
			}
		}
		std::stable_sort(ranked.begin(), ranked.end(), [](const Channel* a, const Channel* b) { return a->state->priority < b->state->priority; });
		for (size_t i = 0; i < ranked.size(); ++i) {
			ranked[i]->state->virtualized = i >= static_cast<size_t>(state->softwareChannels);
		}
		for (auto& channel : state->channels) {
			ControlState& control = *channel->state;
			if (!control.voice) {
				continue;
			}
			if (control.active && !control.paused && ma_sound_at_end(control.voice.get())) {
				control.active = false;
			}
			if (!control.active && !control.notified) {
				control.notified = true;
				ended.push_back(channel.get());
			} else {
				Spatialize(control);
			}
		}
		// Report ended channels and release their voices, effects and sources.
		for (Channel* channel : ended) {
			ControlState& control = *channel->state;
			if (control.callback) {
				control.callback(reinterpret_cast<FMOD_CHANNELCONTROL*>(channel), FMOD_CHANNELCONTROL_CHANNEL, FMOD_CHANNELCONTROL_CALLBACK_END, nullptr, nullptr);
			}
			ReleaseChannel(*state, control);
		}
		ReportMixer(*state, ranked);
		return FMOD_OK;
	}
} // namespace FMOD

// Renders the mix into a buffer, for the tests: the same graph the game plays through.
extern "C" int cortex_audio_render_test(FMOD::System* system, float* output, unsigned long long frames) {
	return ma_engine_read_pcm_frames(&system->state->engine, output, frames, nullptr);
}
