// Is miniaudio's AudioWorklet backend capable of producing sound in this build's
// configuration at all? The engine using it emits pure silence with no error, and
// that could be the backend, the toolchain combination, or the engine's own mixer
// graph. This is the smallest program that can tell those apart: one device, one
// sine wave written directly by the data callback, and counters the page can read.
//
// Built with the same worklet flags the engine would use. If this makes sound, the
// backend works here and the fault is in the engine's integration; if it does not,
// the backend itself is unusable in this configuration.
#include "miniaudio.h"

#include <emscripten.h>
#include <emscripten/html5.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ma_device g_Device;
static double g_Phase = 0.0;

// Written by the audio thread, read by the page. Enough to tell "never ran" from
// "ran and wrote silence" from "ran and wrote a signal that never reached the
// speakers".
volatile int g_CallbackCount = 0;
volatile int g_FrameCount = 0;
volatile float g_LastPeak = 0.0f;
volatile int g_DeviceStateSeen = -1;

static void DataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
	(void)pInput;
	float* out = (float*)pOutput;
	const ma_uint32 channels = pDevice->playback.channels;
	const double step = 2.0 * 3.14159265358979323846 * 440.0 / (double)pDevice->sampleRate;
	float peak = 0.0f;
	for (ma_uint32 frame = 0; frame < frameCount; ++frame) {
		const float value = (float)(sin(g_Phase) * 0.25);
		g_Phase += step;
		for (ma_uint32 channel = 0; channel < channels; ++channel) {
			out[frame * channels + channel] = value;
		}
		if (value > peak) {
			peak = value;
		}
	}
	g_CallbackCount += 1;
	g_FrameCount += (int)frameCount;
	g_LastPeak = peak;
	g_DeviceStateSeen = (int)ma_device_get_state(pDevice);
}

EMSCRIPTEN_KEEPALIVE int probe_callback_count(void) { return g_CallbackCount; }
EMSCRIPTEN_KEEPALIVE int probe_frame_count(void) { return g_FrameCount; }
EMSCRIPTEN_KEEPALIVE float probe_last_peak(void) { return g_LastPeak; }
EMSCRIPTEN_KEEPALIVE int probe_device_state_seen(void) { return g_DeviceStateSeen; }

EMSCRIPTEN_KEEPALIVE int probe_start(void) {
	ma_device_config config = ma_device_config_init(ma_device_type_playback);
	config.playback.format = ma_format_f32;
	config.playback.channels = 2;
	config.sampleRate = 48000;
	config.dataCallback = DataCallback;

	ma_result result = ma_device_init(NULL, &config, &g_Device);
	if (result != MA_SUCCESS) {
		printf("probe: ma_device_init failed %d\n", result);
		return result;
	}
	printf("probe: device backend=%s rate=%u channels=%u period=%u state=%d\n",
	       ma_get_backend_name(g_Device.pContext->backend),
	       g_Device.sampleRate, g_Device.playback.channels,
	       g_Device.playback.internalPeriodSizeInFrames,
	       (int)ma_device_get_state(&g_Device));

	result = ma_device_start(&g_Device);
	printf("probe: ma_device_start -> %d, state=%d\n", result, (int)ma_device_get_state(&g_Device));
	return result;
}

// The engine does not drive a raw device; it builds a miniaudio node graph with
// ma_engine, exactly as below, and its sounds are ma_sound nodes. If the raw
// device makes sound and this does not, the fault is in the graph, not the backend.
static ma_engine g_Engine;
static ma_waveform g_Waveform;
static ma_sound g_Sound;

EMSCRIPTEN_KEEPALIVE int probe_start_engine(void) {
	ma_engine_config config = ma_engine_config_init();
	config.channels = 2;
	config.sampleRate = 48000;

	ma_result result = ma_engine_init(&config, &g_Engine);
	if (result != MA_SUCCESS) {
		printf("probe: ma_engine_init failed %d\n", result);
		return result;
	}

	ma_device* device = ma_engine_get_device(&g_Engine);
	printf("probe: engine device=%p state=%d backend=%s rate=%u channels=%u period=%u\n",
	       (void*)device, device ? (int)ma_device_get_state(device) : -1,
	       device ? ma_get_backend_name(device->pContext->backend) : "none",
	       device ? device->sampleRate : 0u,
	       device ? device->playback.channels : 0u,
	       device ? device->playback.internalPeriodSizeInFrames : 0u);

	ma_waveform_config waveformConfig = ma_waveform_config_init(ma_format_f32, 2, 48000, ma_waveform_type_sine, 0.25, 440);
	result = ma_waveform_init(&waveformConfig, &g_Waveform);
	if (result != MA_SUCCESS) {
		printf("probe: ma_waveform_init failed %d\n", result);
		return result;
	}

	result = ma_sound_init_from_data_source(&g_Engine, &g_Waveform, MA_SOUND_FLAG_NO_SPATIALIZATION, NULL, &g_Sound);
	if (result != MA_SUCCESS) {
		printf("probe: ma_sound_init_from_data_source failed %d\n", result);
		return result;
	}

	result = ma_sound_start(&g_Sound);
	printf("probe: ma_sound_start -> %d, engine device state=%d\n", result,
	       device ? (int)ma_device_get_state(device) : -1);
	return result;
}

EMSCRIPTEN_KEEPALIVE int probe_engine_frames(void) {
	return (int)ma_engine_get_time_in_pcm_frames(&g_Engine);
}

// The engine grows its heap by hundreds of megabytes while loading content, after
// the audio device exists. An AudioWorklet thread that cached a view of the old
// memory would then write into the wrong place, which sounds exactly like silence.
EMSCRIPTEN_KEEPALIVE int probe_grow_heap(int megabytes) {
	const size_t before = (size_t)EM_ASM_INT({ return HEAPU8.length; });
	void* block = malloc((size_t)megabytes * 1024 * 1024);
	if (block) {
		memset(block, 1, (size_t)megabytes * 1024 * 1024);
	}
	const size_t after = (size_t)EM_ASM_INT({ return HEAPU8.length; });
	printf("probe: heap %zu -> %zu (allocated %p)\n", before, after, block);
	return after > before;
}

int main(void) {
	printf("probe: ready\n");
	return 0;
}
