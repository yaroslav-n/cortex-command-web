#include "internal.hpp"

#include <algorithm>
#include <cmath>

// The three effects the engine inserts: the low-pass it uses for muffled sound
// (FMOD's multiband EQ), and the limiter and compressor on the master bus. Each is
// a node in the mixer graph; its parameters are atomics, set by the API thread and
// read by the audio thread.
namespace FMOD {
	namespace {
		void ProcessEffect(ma_node* node, const float** input, ma_uint32* inputFrames, float** output, ma_uint32* outputFrames) {
			auto* effect = reinterpret_cast<DSPState*>(node);
			const ma_uint32 frames = std::min(*inputFrames, *outputFrames);
			if (effect->type == FMOD_DSP_TYPE_MULTIBAND_EQ) {
				effect->lowpass.configure(effect->parameters[1].load(), effect->parameters[2].load(), effect->rate);
			}
			if (effect->type == FMOD_DSP_TYPE_LIMITER) {
				effect->limiter.configure(effect->parameters[0].load(), effect->parameters[1].load(), effect->parameters[2].load(), effect->parameters[3].load() != 0, effect->rate);
				for (ma_uint32 frame = 0; frame < frames; ++frame) {
					effect->limiter.process(input[0][frame * 2], input[0][frame * 2 + 1], output[0][frame * 2], output[0][frame * 2 + 1]);
				}
				*inputFrames = frames;
				*outputFrames = frames;
				return;
			}
			if (effect->type == FMOD_DSP_TYPE_COMPRESSOR) {
				effect->compressor.configure(effect->parameters[0].load(), effect->parameters[1].load(), effect->parameters[2].load(), effect->parameters[3].load(), effect->parameters[4].load(), effect->rate);
				for (ma_uint32 frame = 0; frame < frames; ++frame) {
					effect->compressor.process(input[0][frame * 2], input[0][frame * 2 + 1], output[0][frame * 2], output[0][frame * 2 + 1]);
				}
				*inputFrames = frames;
				*outputFrames = frames;
				return;
			}
			for (ma_uint32 frame = 0; frame < frames; ++frame) {
				for (int channel = 0; channel < 2; ++channel) {
					float value = input[0][frame * 2 + channel];
					if (effect->type == FMOD_DSP_TYPE_MULTIBAND_EQ) {
						value = effect->lowpass.process(value, channel);
					}
					output[0][frame * 2 + channel] = value;
				}
			}
			*inputFrames = frames;
			*outputFrames = frames;
		}
	} // namespace

	const ma_node_vtable g_EffectVtable = {ProcessEffect, nullptr, 1, 1, 0};

	DSP::DSP() :
	    state(std::make_unique<DSPState>()) {}

	DSP::~DSP() {
		if (state->initialized) {
			ma_node_uninit(&state->node, nullptr);
		}
	}

	FMOD_RESULT DSP::setParameterBool(int index, bool value) {
		if (state->type != FMOD_DSP_TYPE_LIMITER || index != 3) {
			return FMOD_ERR_INVALID_PARAM;
		}
		state->parameters[3] = value ? 1.0F : 0.0F;
		return FMOD_OK;
	}

	FMOD_RESULT DSP::setParameterFloat(int index, float value) {
		if (index < 0 || index >= 8 || !std::isfinite(value)) {
			return FMOD_ERR_INVALID_PARAM;
		}
		state->parameters[index] = value;
		return FMOD_OK;
	}
} // namespace FMOD
