#pragma once
// The browser's FMOD: the part of FMOD's API that the engine's AudioMan and sound
// classes use, implemented on miniaudio. The engine compiles against the public
// surface in include/fmod/fmod.hpp; this header is what the implementation files
// share. See notes/audio.md.
//
// The mixer is a miniaudio node graph, pulled by the audio thread (an AudioWorklet
// in the browser). Each channel is an ma_sound reading a PlaybackSource, followed
// by its effects and a ControlFader; each channel group is an ma_sound_group; the
// master group feeds the engine's endpoint. Everything outside the node callbacks
// and PlaybackSource's read runs on the thread that calls the API.
#include "fmod/fmod.hpp"
#include "miniaudio.h"
#include "../compressor.hpp"
#include "../limiter.hpp"
#include "../lowpass.hpp"
#include "../volume_ramp.hpp"

#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace FMOD {
	/// The format every channel is converted to: the mixer's.
	constexpr ma_uint32 c_MixChannels = 2;
	constexpr ma_uint32 c_MixSampleRate = 48000;

	/// A sound file's compressed bytes.
	using SoundBytes = std::shared_ptr<const std::vector<unsigned char>>;

	/// What FMOD needs to know about a sound file before its bytes are there: in the
	/// browser the game's sounds arrive after it starts (runtime/sound-files.js), and
	/// the page lists them in advance (tools/audio_manifest.c).
	struct SoundFileInfo {
		ma_uint64 frames = 0; //!< Length at the file's own sample rate.
		ma_uint32 sampleRate = 0;
	};

	/// Where the page writes that list: one line per file, "path, bytes, frames,
	/// sample rate, site file name", separated by tabs.
	constexpr const char* c_SoundFileListPath = "/audio-manifest.tsv";

	/// What a listed sound's file holds until its bytes arrive. Not empty, because the
	/// engine refuses to load a sound from an empty file (ContentFile::LoadAndReleaseSound).
	constexpr char c_SoundFileOnItsWay[] = "cortex: this sound file is on its way\n";

	/// Whether these are a sound file's bytes rather than the stand-in for them.
	bool SoundFileArrived(const SoundBytes& bytes);

	/// The whole file, or null if it cannot be read.
	SoundBytes ReadWholeFile(const std::string& path);

	FMOD_RESULT Result(ma_result status);

	/// A whole sound decoded to the mixer's format: exactly the frames a decoder
	/// streaming the file would produce, in the same order.
	struct DecodedSound {
		std::vector<float> samples; //!< Interleaved stereo.
		ma_uint64 frames = 0;
		std::array<ma_channel, c_MixChannels> channelMap{};
	};
	using DecodedPCM = std::shared_ptr<const DecodedSound>;

	/// Decodes a whole sound from its compressed bytes; null if it cannot be decoded.
	DecodedPCM DecodeWholeSound(const SoundBytes& bytes);

	/// Short sounds, decoded once and shared by every channel that plays them.
	///
	/// Decoding FLAC or Vorbis as a sound plays happens on the audio thread, once for
	/// every channel playing it, every time it plays; a battle's gunfire does that for
	/// dozens of channels at once. The first play of a short sound still decodes as it
	/// goes, while this decodes it in full on a thread of its own; later plays copy the
	/// decoded samples. Least recently used sounds are dropped beyond the budget
	/// (channels still playing one keep it alive until they end).
	class DecodedSounds {
	public:
		static constexpr double c_MaxSeconds = 10; //!< Longer sounds (music, ambience) are always decoded as they play.
		static constexpr size_t c_BudgetBytes = 192 * 1024 * 1024;

		DecodedSounds();
		~DecodedSounds();

		/// The decoded sound, or null if it is not decoded (yet).
		DecodedPCM Find(const std::string& path);

		/// Queues the sound for decoding unless it is decoded, queued or undecodable.
		void Request(const std::string& path, SoundBytes bytes);

		/// Blocks until nothing is queued or being decoded (for tests).
		void WaitUntilIdle();

		struct Statistics {
			size_t sounds = 0;
			size_t bytes = 0;
			unsigned long long playsFromMemory = 0;
			unsigned long long playsDecoding = 0;
		};
		Statistics GetStatistics();
		void CountPlay(bool fromMemory);

	private:
		struct Entry {
			DecodedPCM pcm;
			size_t bytes = 0;
			unsigned long long lastUse = 0;
		};

		void Run();
		void EvictBeyondBudget(const std::string& keep);

		std::mutex m_Mutex;
		std::condition_variable m_Wake;
		std::condition_variable m_Idle;
		std::unordered_map<std::string, Entry> m_Decoded;
		std::deque<std::pair<std::string, SoundBytes>> m_Queue;
		std::unordered_set<std::string> m_Pending; //!< Queued or being decoded.
		std::unordered_set<std::string> m_Undecodable;
		size_t m_Bytes = 0;
		unsigned long long m_Clock = 0;
		unsigned long long m_PlaysFromMemory = 0;
		unsigned long long m_PlaysDecoding = 0;
		bool m_Stopping = false;
		std::thread m_Worker;
	};

	struct SoundState {
		std::string path;
		FMOD_MODE mode = FMOD_DEFAULT;
		int loops = 0;
		float minimum = 1;
		float maximum = 10000;
		ma_uint64 frames = 0; //!< Length at the file's own sample rate.
		ma_uint32 sampleRate = 0;
		/// Shared by the channels decoding this sound as they play, and released with the last of them.
		std::weak_ptr<const std::vector<unsigned char>> compressed;
	};

	struct DSPState {
		ma_node_base node{};
		bool initialized = false;
		FMOD_DSP_TYPE type = FMOD_DSP_TYPE_UNKNOWN;
		std::array<std::atomic<float>, 8> parameters{};
		CortexAudio::Lowpass lowpass;
		CortexAudio::Limiter limiter;
		CortexAudio::Compressor compressor;
		float rate = 48000;
	};

	/// A channel's samples, read by the audio thread: either the sound decoded as it
	/// plays, or a copy of its already decoded samples (DecodedSounds).
	///
	/// A decoder that reads from a file reaches the filesystem on whichever thread
	/// pulls it. In the browser that filesystem is Emscripten's JavaScript glue, which
	/// does not exist on the AudioWorklet thread, so a file-backed decoder returns no
	/// frames there and the sound is silent. Decoding from memory keeps the audio
	/// thread free of any host call.
	///
	/// It also counts FMOD's loops: at the end of the sound it starts again while
	/// loops remain (-1 is forever), and after the last one supplies a single zero
	/// frame, because the pitch-capable engine resampler delays by one source frame
	/// and needs that lookahead to emit the last real one.
	///
	/// A sound whose file has not arrived yet plays silence from its first frame
	/// until it does, and then starts (Arrive). The audio thread only sees the
	/// decoder once `waiting` is cleared, after the decoder is complete.
	struct PlaybackSource {
		ma_data_source_base base{};
		ma_decoder decoder{};
		DecodedPCM pcm; //!< Set when playing from decoded samples rather than decoding.
		ma_uint64 cursor = 0; //!< The next frame of pcm.
		bool ready = false;
		bool decoding = false;
		bool tailRead = false;
		std::atomic<bool> waiting{false};
		std::atomic<int> loops{0};
		SoundBytes bytes; //!< The decoder's input.

		~PlaybackSource();

		/// Starts decoding the compressed bytes, converted to the mixer's format.
		FMOD_RESULT Initialize(SoundBytes compressed);
		/// Plays already decoded samples.
		FMOD_RESULT Initialize(DecodedPCM decoded);
		/// Plays silence until Arrive.
		FMOD_RESULT InitializeWaiting();
		/// Starts decoding a waiting sound's bytes, now that they are here.
		FMOD_RESULT Arrive(SoundBytes compressed);
	};

	/// The per-channel volume stage: the channel's gain, ramped as FMOD ramps it.
	struct ControlFader {
		ma_node_base node{};
		std::atomic<float> gain{1};
		std::atomic<bool> rampEnabled{true};
		CortexAudio::VolumeRamp ramp;
		bool initialized = false;

		~ControlFader();
	};

	/// The state behind a Channel or a ChannelGroup.
	struct ControlState {
		System* owner = nullptr;
		ChannelGroup* parent = nullptr;
		Sound* source = nullptr;
		std::unique_ptr<PlaybackSource> data;
		std::unique_ptr<ma_sound> voice;
		bool group = false;
		bool active = false;
		bool paused = true;
		bool muted = false;
		bool notified = false;
		bool virtualized = false;
		int index = -1;
		int loops = 0;
		int priority = 128;
		float volume = 1;
		float pitch = 1;
		float pan = 0;
		float level = 1;
		FMOD_VECTOR position{};
		FMOD_VECTOR velocity{};
		void* user = nullptr;
		FMOD_CHANNELCONTROL_CALLBACK callback = nullptr;
		bool waiting = false; //!< Playing silence until its sound's file arrives.
		unsigned long long started = 0; //!< When it was played, counted in plays; the oldest is stolen first.
		ControlFader fader;
		/// The effect chain; index 0 is the output end, and a null entry is the fader.
		std::vector<DSP*> effects;
		std::map<unsigned long long, float> fades;

		~ControlState();
	};

	struct SystemState {
		ma_engine engine{};
		bool initialized = false;
		int softwareChannels = 256;
		int maxChannels = 4096;
		int listenerCount = 1;
		float distanceFactor = 1;
		float doppler = 1;
		float rolloff = 1;
		FMOD_ADVANCEDSETTINGS advanced{};
		std::array<FMOD_VECTOR, 8> listeners{};
		std::vector<std::unique_ptr<Channel>> channels;
		std::vector<std::unique_ptr<ChannelGroup>> groups;
		std::vector<std::unique_ptr<Sound>> sounds;
		std::vector<std::unique_ptr<DSP>> effects;
		ChannelGroup* master = nullptr;
		std::recursive_mutex mutex;
		std::unique_ptr<DecodedSounds> decoded;
		/// The sound files the page will deliver after the start, by the engine's path.
		std::unordered_map<std::string, SoundFileInfo> soundFiles;
		/// Files already asked for ahead of the rest.
		std::unordered_set<std::string> requestedFiles;
		unsigned long long lastArrivalCheckMs = 0;
		unsigned long long waitingPlays = 0;
		unsigned long long plays = 0;
		unsigned long long stolenChannels = 0;
	};

	// sound.cpp
	/// A path as the list names it: relative to the working directory, "/" in the browser.
	std::string EnginePath(const std::string& path);
	/// Reads the page's list of sound files that arrive after the start, if there is one.
	void LoadSoundFileList(SystemState& system);
	/// The listed file for this engine path, or null.
	const SoundFileInfo* FindSoundFile(const SystemState& system, const std::string& path);

	// system.cpp
	/// Frees a channel whose sound has ended or was stopped: its voice, source and effects.
	void ReleaseChannel(SystemState& system, ControlState& control);
	/// Asks the page to fetch this sound's file before the others, once.
	void RequestSoundFile(SystemState& system, const std::string& path);
	/// Starts the channels whose sound files have arrived.
	void StartArrivedChannels(SystemState& system);

	// channel.cpp
	/// Connects a channel's voice through its effects and fader to its parent group.
	FMOD_RESULT Wire(ControlState& control);
	/// Applies distance attenuation, panning, volume and mute to a channel's fader.
	void Spatialize(ControlState& control);

	// dsp.cpp
	extern const ma_node_vtable g_EffectVtable;

	// diagnostics.cpp: what the mixer produced, reported under ?perf-debug.
	bool AudioDiagnosticsEnabled();
	void MeasureEngineOutput(void* userData, float* frames, ma_uint64 frameCount);
	void MeasureFader(const float* input, const float* output, ma_uint32 frames);
	void ReportMixer(SystemState& system, const std::vector<Channel*>& activeChannels);
} // namespace FMOD
