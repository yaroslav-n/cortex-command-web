#include "internal.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// Sounds: a file's path and properties. Nothing is decoded until a channel plays it.
namespace FMOD {
	std::string EnginePath(const std::string& path) {
		size_t start = 0;
		while (start < path.size() && (path[start] == '/' || path.compare(start, 2, "./") == 0)) {
			start += path[start] == '/' ? 1 : 2;
		}
		return path.substr(start);
	}

	void LoadSoundFileList(SystemState& system) {
		std::FILE* list = std::fopen(c_SoundFileListPath, "r");
		if (!list) {
			return;
		}
		char line[4096];
		while (std::fgets(line, sizeof(line), list)) {
			// path, bytes, frames, sample rate, site file name
			char* fields[5] = {};
			char* cursor = line;
			for (int field = 0; field < 5 && cursor; ++field) {
				fields[field] = cursor;
				cursor = std::strpbrk(cursor, "\t\n");
				if (cursor) {
					*cursor++ = '\0';
				}
			}
			if (!fields[3]) {
				continue;
			}
			SoundFileInfo info;
			info.frames = std::strtoull(fields[2], nullptr, 10);
			info.sampleRate = static_cast<ma_uint32>(std::strtoul(fields[3], nullptr, 10));
			if (info.sampleRate > 0) {
				system.soundFiles[EnginePath(fields[0])] = info;
			}
		}
		std::fclose(list);
	}

	const SoundFileInfo* FindSoundFile(const SystemState& system, const std::string& path) {
		if (system.soundFiles.empty()) {
			return nullptr;
		}
		const auto found = system.soundFiles.find(EnginePath(path));
		return found == system.soundFiles.end() ? nullptr : &found->second;
	}

	ma_uint64 MixLength(const SoundState& sound) {
		// The frames the decoder produces when it converts the sound to the mixer's rate,
		// rounded up; the length it reports can be a frame shorter. audio_contract compares
		// the two at every sample rate the game's sounds use.
		return sound.sampleRate == 0 ? 0 : (sound.frames * c_MixSampleRate + sound.sampleRate - 1) / sound.sampleRate;
	}

	Sound::Sound() :
	    state(std::make_unique<SoundState>()) {}

	Sound::~Sound() = default;

	FMOD_RESULT System::createSound(const char* path, FMOD_MODE mode, FMOD_CREATESOUNDEXINFO* info, Sound** sound) {
		if (!path || !sound || info) {
			return FMOD_ERR_INVALID_PARAM;
		}
		auto created = std::make_unique<Sound>();
		created->state->path = path;
		created->state->mode = mode;
		// A file the page delivers after the start is listed with its length and
		// sample rate, which is all a Sound needs until it plays.
		if (const SoundFileInfo* listed = FindSoundFile(*state, path)) {
			created->state->frames = listed->frames;
			created->state->sampleRate = listed->sampleRate;
			std::lock_guard lock(state->mutex);
			*sound = created.get();
			state->sounds.push_back(std::move(created));
			return FMOD_OK;
		}
		// Otherwise only the header is read here, for the length and sample rate.
		ma_decoder decoder;
		ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
		if (ma_decoder_init_file(path, &config, &decoder) != MA_SUCCESS) {
			return FMOD_ERR_FILE_BAD;
		}
		created->state->sampleRate = decoder.outputSampleRate;
		const ma_result status = ma_decoder_get_length_in_pcm_frames(&decoder, &created->state->frames);
		ma_decoder_uninit(&decoder);
		if (status != MA_SUCCESS) {
			return FMOD_ERR_FORMAT;
		}
		std::lock_guard lock(state->mutex);
		*sound = created.get();
		state->sounds.push_back(std::move(created));
		return FMOD_OK;
	}

	FMOD_RESULT Sound::getLength(unsigned int* length, FMOD_TIMEUNIT unit) {
		if (!length) {
			return FMOD_ERR_INVALID_PARAM;
		}
		if (unit == FMOD_TIMEUNIT_MS) {
			*length = state->frames * 1000 / state->sampleRate;
		} else if (unit == FMOD_TIMEUNIT_PCM) {
			*length = state->frames;
		} else {
			return FMOD_ERR_UNSUPPORTED;
		}
		return FMOD_OK;
	}

	FMOD_RESULT Sound::setMode(FMOD_MODE mode) {
		state->mode = mode;
		return FMOD_OK;
	}

	FMOD_RESULT Sound::setLoopCount(int count) {
		if (count < -1) {
			return FMOD_ERR_INVALID_PARAM;
		}
		state->loops = count;
		return FMOD_OK;
	}

	FMOD_RESULT Sound::set3DMinMaxDistance(float minimum, float maximum) {
		if (minimum < 0 || maximum < minimum) {
			return FMOD_ERR_INVALID_PARAM;
		}
		state->minimum = minimum;
		state->maximum = maximum;
		return FMOD_OK;
	}
} // namespace FMOD
