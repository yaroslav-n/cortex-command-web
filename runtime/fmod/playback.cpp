#include "internal.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace FMOD {
	SoundBytes ReadWholeFile(const std::string& path) {
		std::FILE* file = std::fopen(path.c_str(), "rb");
		if (!file) {
			return nullptr;
		}
		std::fseek(file, 0, SEEK_END);
		const long size = std::ftell(file);
		if (size < 0) {
			std::fclose(file);
			return nullptr;
		}
		std::fseek(file, 0, SEEK_SET);
		auto bytes = std::make_shared<std::vector<unsigned char>>(static_cast<size_t>(size));
		const size_t read = std::fread(bytes->data(), 1, bytes->size(), file);
		std::fclose(file);
		if (read != bytes->size()) {
			return nullptr;
		}
		return bytes;
	}

	bool SoundFileArrived(const SoundBytes& bytes) {
		constexpr size_t length = sizeof(c_SoundFileOnItsWay) - 1;
		return bytes && !bytes->empty() && !(bytes->size() == length && std::memcmp(bytes->data(), c_SoundFileOnItsWay, length) == 0);
	}

	namespace {
		/// The next frames of the sound, from the decoder or from the decoded samples.
		/// The decoded samples behave as the decoder does: a read that reaches the end
		/// returns the frames that remain, and only a read with none left reports the end.
		ma_result ReadFrames(PlaybackSource& source, float* output, ma_uint64 requested, ma_uint64* count) {
			if (!source.pcm) {
				return ma_decoder_read_pcm_frames(&source.decoder, output, requested, count);
			}
			*count = std::min(requested, source.pcm->frames - source.cursor);
			std::memcpy(output, source.pcm->samples.data() + source.cursor * c_MixChannels, *count * c_MixChannels * sizeof(float));
			source.cursor += *count;
			return *count == 0 ? MA_AT_END : MA_SUCCESS;
		}

		ma_result SeekFrames(PlaybackSource& source, ma_uint64 frame) {
			if (!source.pcm) {
				return ma_decoder_seek_to_pcm_frame(&source.decoder, frame);
			}
			source.cursor = std::min(frame, source.pcm->frames);
			return MA_SUCCESS;
		}

		ma_result ReadPlayback(ma_data_source* dataSource, void* output, ma_uint64 requested, ma_uint64* read) {
			auto& source = *static_cast<PlaybackSource*>(dataSource);
			if (source.waiting.load(std::memory_order_acquire)) {
				std::fill_n(static_cast<float*>(output), requested * c_MixChannels, 0.0F);
				*read = requested;
				return MA_SUCCESS;
			}
			*read = 0;
			ma_result status = MA_SUCCESS;
			while (*read < requested) {
				ma_uint64 count = 0;
				status = ReadFrames(source, static_cast<float*>(output) + *read * c_MixChannels, requested - *read, &count);
				*read += count;
				if (status == MA_AT_END) {
					const int loops = source.loops.load();
					if (loops == 0) {
						// Supply one zero lookahead frame at the final end so the engine's
						// resampler can emit the last real frame; never at a loop boundary.
						if (!source.tailRead && *read < requested) {
							float* samples = static_cast<float*>(output) + *read * c_MixChannels;
							samples[0] = samples[1] = 0;
							++*read;
							source.tailRead = true;
						}
						break;
					}
					if (loops > 0) {
						source.loops.fetch_sub(1);
					}
					const ma_result seek = SeekFrames(source, 0);
					if (seek != MA_SUCCESS) {
						return seek;
					}
				} else if (status != MA_SUCCESS || count == 0) {
					break;
				}
			}
			return *read == requested ? MA_SUCCESS : status;
		}

		ma_result SeekPlayback(ma_data_source* dataSource, ma_uint64 frame) {
			auto& source = *static_cast<PlaybackSource*>(dataSource);
			source.tailRead = false;
			return SeekFrames(source, frame);
		}

		ma_result GetPlaybackFormat(ma_data_source* dataSource, ma_format* format, ma_uint32* channels, ma_uint32* sampleRate, ma_channel* channelMap, size_t channelMapCapacity) {
			const auto& source = *static_cast<PlaybackSource*>(dataSource);
			if (source.waiting.load(std::memory_order_acquire)) {
				// What the decoder will report: it converts every sound to the mixer's format.
				if (format) {
					*format = ma_format_f32;
				}
				if (channels) {
					*channels = c_MixChannels;
				}
				if (sampleRate) {
					*sampleRate = c_MixSampleRate;
				}
				if (channelMap) {
					ma_channel_map_init_standard(ma_standard_channel_map_default, channelMap, channelMapCapacity, c_MixChannels);
				}
				return MA_SUCCESS;
			}
			if (!source.pcm) {
				return ma_data_source_get_data_format(&static_cast<PlaybackSource*>(dataSource)->decoder, format, channels, sampleRate, channelMap, channelMapCapacity);
			}
			if (format) {
				*format = ma_format_f32;
			}
			if (channels) {
				*channels = c_MixChannels;
			}
			if (sampleRate) {
				*sampleRate = c_MixSampleRate;
			}
			if (channelMap) {
				std::copy_n(source.pcm->channelMap.begin(), std::min<size_t>(channelMapCapacity, c_MixChannels), channelMap);
			}
			return MA_SUCCESS;
		}

		ma_result GetPlaybackCursor(ma_data_source* dataSource, ma_uint64* cursor) {
			auto& source = *static_cast<PlaybackSource*>(dataSource);
			if (source.waiting.load(std::memory_order_acquire)) {
				*cursor = 0;
				return MA_SUCCESS;
			}
			if (!source.pcm) {
				return ma_decoder_get_cursor_in_pcm_frames(&source.decoder, cursor);
			}
			*cursor = source.cursor;
			return MA_SUCCESS;
		}

		ma_result GetPlaybackLength(ma_data_source* dataSource, ma_uint64* length) {
			auto& source = *static_cast<PlaybackSource*>(dataSource);
			if (source.waiting.load(std::memory_order_acquire)) {
				return MA_NOT_IMPLEMENTED;
			}
			if (!source.pcm) {
				return ma_decoder_get_length_in_pcm_frames(&source.decoder, length);
			}
			*length = source.pcm->frames;
			return MA_SUCCESS;
		}

		const ma_data_source_vtable g_PlaybackVtable = {ReadPlayback, SeekPlayback, GetPlaybackFormat, GetPlaybackCursor, GetPlaybackLength, nullptr, MA_DATA_SOURCE_SELF_MANAGED_RANGE_AND_LOOP_POINT};
	} // namespace

	PlaybackSource::~PlaybackSource() {
		if (ready) {
			ma_data_source_uninit(&base);
		}
		if (decoding) {
			ma_decoder_uninit(&decoder);
		}
	}

	FMOD_RESULT PlaybackSource::Initialize(SoundBytes compressed) {
		bytes = std::move(compressed);
		ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_f32, c_MixChannels, c_MixSampleRate);
		if (ma_decoder_init_memory(bytes->data(), bytes->size(), &decoderConfig, &decoder) != MA_SUCCESS) {
			return FMOD_ERR_FILE_BAD;
		}
		decoding = true;
		ma_data_source_config dataConfig = ma_data_source_config_init();
		dataConfig.vtable = &g_PlaybackVtable;
		const ma_result status = ma_data_source_init(&dataConfig, &base);
		if (status != MA_SUCCESS) {
			return Result(status);
		}
		ready = true;
		return FMOD_OK;
	}

	FMOD_RESULT PlaybackSource::InitializeWaiting() {
		waiting.store(true, std::memory_order_release);
		ma_data_source_config dataConfig = ma_data_source_config_init();
		dataConfig.vtable = &g_PlaybackVtable;
		const ma_result status = ma_data_source_init(&dataConfig, &base);
		if (status != MA_SUCCESS) {
			return Result(status);
		}
		ready = true;
		return FMOD_OK;
	}

	FMOD_RESULT PlaybackSource::Arrive(SoundBytes compressed) {
		bytes = std::move(compressed);
		ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_f32, c_MixChannels, c_MixSampleRate);
		if (ma_decoder_init_memory(bytes->data(), bytes->size(), &decoderConfig, &decoder) != MA_SUCCESS) {
			return FMOD_ERR_FILE_BAD;
		}
		decoding = true;
		// Only now may the audio thread read the decoder.
		waiting.store(false, std::memory_order_release);
		return FMOD_OK;
	}

	FMOD_RESULT PlaybackSource::Initialize(DecodedPCM decoded) {
		pcm = std::move(decoded);
		ma_data_source_config dataConfig = ma_data_source_config_init();
		dataConfig.vtable = &g_PlaybackVtable;
		const ma_result status = ma_data_source_init(&dataConfig, &base);
		if (status != MA_SUCCESS) {
			return Result(status);
		}
		ready = true;
		return FMOD_OK;
	}
} // namespace FMOD
