#include "internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

// Under ?perf-debug the mixer reports what it produced every two seconds. A graph
// that is never pulled, one that is pulled and yields zero, and one whose output
// never reaches the speakers all sound exactly the same from the page; the engine
// clock, the mixed frames and the peaks either side of the channel faders tell
// them apart (see notes/audio.md).
namespace FMOD {
#ifdef __EMSCRIPTEN__
	namespace {
		std::atomic<unsigned long long> g_MixedFrames{0};
		std::atomic<unsigned int> g_MixedPeakMilli{0};
		std::atomic<unsigned int> g_FaderInPeakMilli{0};
		std::atomic<unsigned int> g_FaderOutPeakMilli{0};
		std::atomic<unsigned long long> g_FaderCalls{0};
		// Read on the audio thread, which cannot ask the page itself; set when the
		// mixer starts. Measuring costs two passes over every channel's samples.
		std::atomic<bool> g_Measuring{false};

		float Peak(const float* samples, ma_uint64 count) {
			float peak = 0;
			for (ma_uint64 i = 0; i < count; ++i) {
				peak = std::max(peak, std::fabs(samples[i]));
			}
			return peak;
		}

		void RecordPeak(std::atomic<unsigned int>& slot, float peak) {
			const unsigned int milli = static_cast<unsigned int>(peak * 1000.0F);
			unsigned int previous = slot.load(std::memory_order_relaxed);
			while (milli > previous && !slot.compare_exchange_weak(previous, milli, std::memory_order_relaxed)) {
			}
		}
	} // namespace

	bool AudioDiagnosticsEnabled() {
		static const bool enabled = MAIN_THREAD_EM_ASM_INT({ return Module['perfDiagnostics'] === true; });
		g_Measuring.store(enabled, std::memory_order_relaxed);
		return enabled;
	}

	void MeasureEngineOutput(void*, float* frames, ma_uint64 frameCount) {
		if (!g_Measuring.load(std::memory_order_relaxed)) {
			return;
		}
		g_MixedFrames.fetch_add(frameCount, std::memory_order_relaxed);
		RecordPeak(g_MixedPeakMilli, Peak(frames, frameCount * 2));
	}

	void MeasureFader(const float* input, const float* output, ma_uint32 frames) {
		if (!g_Measuring.load(std::memory_order_relaxed)) {
			return;
		}
		RecordPeak(g_FaderInPeakMilli, Peak(input, frames * 2));
		g_FaderCalls.fetch_add(1, std::memory_order_relaxed);
		RecordPeak(g_FaderOutPeakMilli, Peak(output, frames * 2));
	}

	void ReportMixer(SystemState& system, const std::vector<Channel*>& activeChannels) {
		if (!AudioDiagnosticsEnabled()) {
			return;
		}
		static double lastReport = 0;
		const double now = emscripten_get_now();
		if (now - lastReport <= 2000) {
			return;
		}
		lastReport = now;
		ma_device* device = ma_engine_get_device(&system.engine);
		std::fprintf(stderr, "Browser audio: engine %llu frames, device state %d, mixed %llu frames, peak %.3f, channels %zu active of %d, master volume %.3f, engine volume %.3f\n",
		             static_cast<unsigned long long>(ma_engine_get_time_in_pcm_frames(&system.engine)),
		             device ? static_cast<int>(ma_device_get_state(device)) : -1,
		             g_MixedFrames.load(std::memory_order_relaxed),
		             g_MixedPeakMilli.exchange(0, std::memory_order_relaxed) / 1000.0,
		             activeChannels.size(), system.softwareChannels,
		             system.master && system.master->state->voice ? ma_sound_get_volume(system.master->state->voice.get()) : -1.0,
		             ma_engine_get_volume(&system.engine));
		const DecodedSounds::Statistics decoded = system.decoded->GetStatistics();
		std::fprintf(stderr, "  decoded sounds: %zu, %.1f MB; plays from memory %llu, decoding as they play %llu, waiting for their file %llu, silent without it %llu; channels %zu of %d, %llu stolen\n",
		             decoded.sounds, decoded.bytes / 1048576.0, decoded.playsFromMemory, decoded.playsDecoding, system.waitingPlays,
		             system.silentPlays, system.channels.size(), system.maxChannels, system.stolenChannels);
		std::fprintf(stderr, "  fader: %llu calls, in peak %.3f, out peak %.3f\n",
		             g_FaderCalls.exchange(0, std::memory_order_relaxed),
		             g_FaderInPeakMilli.exchange(0, std::memory_order_relaxed) / 1000.0,
		             g_FaderOutPeakMilli.exchange(0, std::memory_order_relaxed) / 1000.0);
		int reported = 0;
		for (Channel* channel : activeChannels) {
			if (++reported > 3) {
				break;
			}
			const ControlState& control = *channel->state;
			ma_uint64 cursor = 0;
			if (control.voice) {
				ma_sound_get_cursor_in_pcm_frames(control.voice.get(), &cursor);
			}
			std::fprintf(stderr, "  channel %d: volume %.3f gain %.3f muted %d paused %d virtual %d playing %d atEnd %d soundVolume %.3f cursor %llu\n",
			             control.index, control.volume, control.fader.gain.load(), static_cast<int>(control.muted), static_cast<int>(control.paused),
			             static_cast<int>(control.virtualized),
			             control.voice ? static_cast<int>(ma_sound_is_playing(control.voice.get())) : -1,
			             control.voice ? static_cast<int>(ma_sound_at_end(control.voice.get())) : -1,
			             control.voice ? ma_sound_get_volume(control.voice.get()) : -1.0,
			             static_cast<unsigned long long>(cursor));
		}
	}
#else
	bool AudioDiagnosticsEnabled() {
		return false;
	}
	void MeasureEngineOutput(void*, float*, ma_uint64) {}
	void MeasureFader(const float*, const float*, ma_uint32) {}
	void ReportMixer(SystemState&, const std::vector<Channel*>&) {}
#endif
} // namespace FMOD
