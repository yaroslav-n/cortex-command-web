#include "internal.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

// Channels and channel groups: the graph wiring, the per-channel fader, 3D
// attenuation and panning, and the ChannelControl API the engine drives them with.
namespace FMOD {
	namespace {
		void ProcessControlFader(ma_node* node, const float** input, ma_uint32* inputFrames, float** output, ma_uint32* outputFrames) {
			auto* fader = reinterpret_cast<ControlFader*>(node);
			const ma_uint32 frames = std::min(*inputFrames, *outputFrames);
			const float target = fader->gain.load();
			const bool rampEnabled = fader->rampEnabled.load();
			for (ma_uint32 frame = 0; frame < frames; ++frame) {
				const float gain = fader->ramp.next(target, rampEnabled);
				output[0][frame * 2] = input[0][frame * 2] * gain;
				output[0][frame * 2 + 1] = input[0][frame * 2 + 1] * gain;
			}
			MeasureFader(input[0], output[0], frames);
			*inputFrames = frames;
			*outputFrames = frames;
		}

		const ma_node_vtable g_ControlFaderVtable = {ProcessControlFader, nullptr, 1, 1, 0};

		ma_node* Endpoint(ControlState& control) {
			if (control.parent && control.parent->state->voice) {
				return reinterpret_cast<ma_node*>(control.parent->state->voice.get());
			}
			return ma_engine_get_endpoint(&control.owner->state->engine);
		}

		ma_result InitializeStereoNode(ma_engine& engine, const ma_node_vtable* vtable, ma_node_base* node) {
			ma_node_config config = ma_node_config_init();
			ma_uint32 channels = 2;
			config.vtable = vtable;
			config.pInputChannels = &channels;
			config.pOutputChannels = &channels;
			return ma_node_init(ma_engine_get_node_graph(&engine), &config, nullptr, node);
		}
	} // namespace

	ControlFader::~ControlFader() {
		if (initialized) {
			ma_node_uninit(&node, nullptr);
		}
	}

	ControlState::~ControlState() {
		if (voice) {
			ma_sound_uninit(voice.get());
		}
	}

	FMOD_RESULT Wire(ControlState& control) {
		if (!control.voice) {
			return FMOD_ERR_INVALID_HANDLE;
		}
		ma_engine& engine = control.owner->state->engine;
		ma_node* last = reinterpret_cast<ma_node*>(control.voice.get());
		// FMOD's index zero is the output end, so walk from the input end.
		for (auto effectEntry = control.effects.rbegin(); effectEntry != control.effects.rend(); ++effectEntry) {
			DSP* effect = *effectEntry;
			if (!effect) {
				if (!control.fader.initialized) {
					const ma_result status = InitializeStereoNode(engine, &g_ControlFaderVtable, &control.fader.node);
					if (status != MA_SUCCESS) {
						return Result(status);
					}
					control.fader.initialized = true;
				}
				const ma_result status = ma_node_attach_output_bus(last, 0, &control.fader.node, 0);
				if (status != MA_SUCCESS) {
					return Result(status);
				}
				last = &control.fader.node;
				continue;
			}
			DSPState& effectState = *effect->state;
			if (!effectState.initialized) {
				const ma_result status = InitializeStereoNode(engine, &g_EffectVtable, &effectState.node);
				if (status != MA_SUCCESS) {
					return Result(status);
				}
				effectState.initialized = true;
				effectState.rate = ma_engine_get_sample_rate(&engine);
			}
			ma_node_attach_output_bus(last, 0, &effectState.node, 0);
			last = &effectState.node;
		}
		return Result(ma_node_attach_output_bus(last, 0, Endpoint(control), 0));
	}

	void Spatialize(ControlState& control) {
		if (!control.voice || control.group || !control.source) {
			return;
		}
		SystemState& system = *control.owner->state;
		float distance = INFINITY;
		FMOD_VECTOR listener{};
		for (int i = 0; i < system.listenerCount; ++i) {
			const FMOD_VECTOR& position = system.listeners[i];
			const float dx = control.position.x - position.x;
			const float dy = control.position.y - position.y;
			const float dz = control.position.z - position.z;
			const float listenerDistance = std::sqrt(dx * dx + dy * dy + dz * dz);
			if (listenerDistance < distance) {
				distance = listenerDistance;
				listener = position;
			}
		}
		float attenuation = 1;
		float pan = control.pan;
		if (!(control.source->state->mode & FMOD_2D)) {
			const SoundState& sound = *control.source->state;
			if (distance >= sound.maximum) {
				attenuation = 0;
			} else if (distance > sound.minimum) {
				attenuation = sound.minimum / (sound.minimum + system.rolloff * (distance - sound.minimum));
			}
			// AudioMan supplies the volume itself when the 3D blend is less than one.
			if (control.level < 1) {
				attenuation = 1;
			}
			if (control.pan == 0 && distance > 0) {
				pan = std::clamp((control.position.x - listener.x) / distance * control.level, -1.0F, 1.0F);
			}
		}
		control.fader.gain = (control.muted || control.virtualized) ? 0 : control.volume * attenuation;
		ma_sound_set_pan(control.voice.get(), pan);
	}

	ChannelControl::ChannelControl() :
	    state(std::make_unique<ControlState>()) {}

	ChannelControl::~ChannelControl() = default;

	FMOD_RESULT ChannelControl::setUserData(void* userData) {
		state->user = userData;
		return FMOD_OK;
	}

	FMOD_RESULT ChannelControl::getUserData(void** userData) {
		if (!userData) {
			return FMOD_ERR_INVALID_PARAM;
		}
		*userData = state->user;
		return FMOD_OK;
	}

	FMOD_RESULT ChannelControl::setCallback(FMOD_CHANNELCONTROL_CALLBACK callback) {
		state->callback = callback;
		return FMOD_OK;
	}

	FMOD_RESULT ChannelControl::setVolume(float volume) {
		if (!std::isfinite(volume)) {
			return FMOD_ERR_INVALID_PARAM;
		}
		state->volume = volume;
		Spatialize(*state);
		if (state->group) {
			state->fader.gain = state->muted ? 0 : volume;
		}
		return FMOD_OK;
	}

	FMOD_RESULT ChannelControl::setVolumeRamp(bool enabled) {
		state->fader.rampEnabled = enabled;
		return FMOD_OK;
	}

	FMOD_RESULT ChannelControl::getVolume(float* volume) {
		if (!volume) {
			return FMOD_ERR_INVALID_PARAM;
		}
		*volume = state->volume;
		return FMOD_OK;
	}

	FMOD_RESULT ChannelControl::setMute(bool mute) {
		state->muted = mute;
		return setVolume(state->volume);
	}

	FMOD_RESULT ChannelControl::getPitch(float* pitch) {
		if (!pitch) {
			return FMOD_ERR_INVALID_PARAM;
		}
		*pitch = state->pitch;
		return FMOD_OK;
	}

	FMOD_RESULT ChannelControl::setPitch(float pitch) {
		if (pitch <= 0 || !std::isfinite(pitch)) {
			return FMOD_ERR_INVALID_PARAM;
		}
		state->pitch = pitch;
		if (state->voice) {
			ma_sound_set_pitch(state->voice.get(), pitch);
		}
		return FMOD_OK;
	}

	FMOD_RESULT ChannelControl::setPan(float pan) {
		state->pan = std::clamp(pan, -1.0F, 1.0F);
		Spatialize(*state);
		return FMOD_OK;
	}

	FMOD_RESULT ChannelControl::setPaused(bool paused) {
		state->paused = paused;
		if (!state->voice) {
			return FMOD_ERR_INVALID_HANDLE;
		}
		return Result(paused ? ma_sound_stop(state->voice.get()) : ma_sound_start(state->voice.get()));
	}

	FMOD_RESULT ChannelControl::stop() {
		if (state->group) {
			// Stopping a group stops every channel under it, however deep.
			for (auto& channel : state->owner->state->channels) {
				for (ChannelGroup* group = channel->state->parent; group; group = group->state->parent) {
					if (group == this) {
						channel->stop();
						break;
					}
				}
			}
			return FMOD_OK;
		}
		state->active = false;
		if (state->voice) {
			ma_sound_stop(state->voice.get());
		}
		return FMOD_OK;
	}

	FMOD_RESULT ChannelControl::isPlaying(bool* playing) {
		if (!playing) {
			return FMOD_ERR_INVALID_PARAM;
		}
		if (state->group) {
			*playing = false;
			for (auto& channel : state->owner->state->channels) {
				if (channel->state->active && channel->state->parent == this) {
					*playing = true;
				}
			}
		} else {
			*playing = state->active;
		}
		return FMOD_OK;
	}

	FMOD_RESULT ChannelControl::getAudibility(float* audibility) {
		if (!audibility) {
			return FMOD_ERR_INVALID_PARAM;
		}
		*audibility = state->voice ? state->fader.gain.load() * ma_sound_get_current_fade_volume(state->voice.get()) : 0;
		for (ChannelGroup* group = state->parent; group; group = group->state->parent) {
			*audibility *= group->state->muted ? 0 : group->state->volume;
		}
		return FMOD_OK;
	}

	FMOD_RESULT ChannelControl::getMode(FMOD_MODE* mode) {
		if (!mode) {
			return FMOD_ERR_INVALID_PARAM;
		}
		*mode = state->source ? state->source->state->mode : FMOD_2D;
		return FMOD_OK;
	}

	FMOD_RESULT ChannelControl::set3DAttributes(const FMOD_VECTOR* position, const FMOD_VECTOR* velocity) {
		if (position) {
			state->position = *position;
		}
		if (velocity) {
			state->velocity = *velocity;
		}
		Spatialize(*state);
		return FMOD_OK;
	}

	FMOD_RESULT ChannelControl::get3DAttributes(FMOD_VECTOR* position, FMOD_VECTOR* velocity) {
		if (position) {
			*position = state->position;
		}
		if (velocity) {
			*velocity = state->velocity;
		}
		return FMOD_OK;
	}

	FMOD_RESULT ChannelControl::set3DLevel(float level) {
		state->level = std::clamp(level, 0.0F, 1.0F);
		Spatialize(*state);
		return FMOD_OK;
	}

	FMOD_RESULT ChannelControl::get3DLevel(float* level) {
		if (!level) {
			return FMOD_ERR_INVALID_PARAM;
		}
		*level = state->level;
		return FMOD_OK;
	}

	FMOD_RESULT ChannelControl::get3DMinMaxDistance(float* minimum, float* maximum) {
		if (!state->source) {
			return FMOD_ERR_INVALID_HANDLE;
		}
		if (minimum) {
			*minimum = state->source->state->minimum;
		}
		if (maximum) {
			*maximum = state->source->state->maximum;
		}
		return FMOD_OK;
	}

	FMOD_RESULT ChannelControl::addDSP(int index, DSP* effect) {
		if (!effect || index < 0) {
			return FMOD_ERR_INVALID_PARAM;
		}
		std::vector<DSP*>& effects = state->effects;
		effects.insert(effects.begin() + std::min<size_t>(index, effects.size()), effect);
		return Wire(*state);
	}

	FMOD_RESULT ChannelControl::getDSP(int index, DSP** effect) {
		if (!effect || index < 0 || static_cast<size_t>(index) >= state->effects.size()) {
			return FMOD_ERR_INVALID_PARAM;
		}
		*effect = state->effects[index];
		return *effect ? FMOD_OK : FMOD_ERR_UNSUPPORTED;
	}

	FMOD_RESULT ChannelControl::getDSPClock(unsigned long long* local, unsigned long long* parent) {
		const ma_uint64 time = ma_engine_get_time_in_pcm_frames(&state->owner->state->engine);
		if (local) {
			*local = time;
		}
		if (parent) {
			*parent = time;
		}
		return FMOD_OK;
	}

	FMOD_RESULT ChannelControl::addFadePoint(unsigned long long clock, float value) {
		if (!state->voice) {
			return FMOD_ERR_INVALID_HANDLE;
		}
		state->fades[clock] = value;
		if (state->fades.size() >= 2) {
			// miniaudio fades between two points; use the last two, relative to the volume.
			auto last = std::prev(state->fades.end());
			auto first = std::prev(last);
			const float base = state->volume == 0 ? 1 : state->volume;
			ma_sound_set_fade_start_in_pcm_frames(state->voice.get(), first->second / base, last->second / base, last->first - first->first, first->first);
		}
		return FMOD_OK;
	}

	FMOD_RESULT Channel::getIndex(int* index) {
		if (!index) {
			return FMOD_ERR_INVALID_PARAM;
		}
		*index = state->index;
		return FMOD_OK;
	}

	FMOD_RESULT Channel::getCurrentSound(Sound** sound) {
		if (!sound) {
			return FMOD_ERR_INVALID_PARAM;
		}
		*sound = state->source;
		return FMOD_OK;
	}

	FMOD_RESULT Channel::setPriority(int priority) {
		if (priority < 0 || priority > 256) {
			return FMOD_ERR_INVALID_PARAM;
		}
		state->priority = priority;
		return FMOD_OK;
	}

	FMOD_RESULT Channel::setLoopCount(int count) {
		if (count < -1) {
			return FMOD_ERR_INVALID_PARAM;
		}
		state->loops = count;
		if (state->data) {
			state->data->loops = count;
		}
		return FMOD_OK;
	}

	FMOD_RESULT ChannelGroup::addGroup(ChannelGroup* child, bool, DSPConnection** connection) {
		if (!child || child == this) {
			return FMOD_ERR_INVALID_PARAM;
		}
		for (ChannelGroup* ancestor = this; ancestor; ancestor = ancestor->state->parent) {
			if (ancestor == child) {
				return FMOD_ERR_INVALID_PARAM;
			}
		}
		child->state->parent = this;
		if (connection) {
			*connection = nullptr;
		}
		return Wire(*child->state);
	}

	FMOD_RESULT ChannelGroup::getNumChannels(int* count) {
		if (!count) {
			return FMOD_ERR_INVALID_PARAM;
		}
		*count = 0;
		for (auto& channel : state->owner->state->channels) {
			if (channel->state->active && channel->state->parent == this) {
				++*count;
			}
		}
		return FMOD_OK;
	}

	FMOD_RESULT ChannelGroup::getChannel(int index, Channel** channel) {
		if (!channel || index < 0) {
			return FMOD_ERR_INVALID_PARAM;
		}
		for (auto& candidate : state->owner->state->channels) {
			if (candidate->state->active && candidate->state->parent == this) {
				if (index-- == 0) {
					*channel = candidate.get();
					return FMOD_OK;
				}
			}
		}
		return FMOD_ERR_INVALID_PARAM;
	}
} // namespace FMOD
