#include "internal.hpp"

namespace FMOD {
	DecodedPCM DecodeWholeSound(const SoundBytes& bytes) {
		ma_decoder_config config = ma_decoder_config_init(ma_format_f32, c_MixChannels, c_MixSampleRate);
		ma_decoder decoder;
		if (!bytes || ma_decoder_init_memory(bytes->data(), bytes->size(), &config, &decoder) != MA_SUCCESS) {
			return nullptr;
		}
		auto sound = std::make_shared<DecodedSound>();
		ma_decoder_get_data_format(&decoder, nullptr, nullptr, nullptr, sound->channelMap.data(), sound->channelMap.size());
		// The length is known for most formats, and a close estimate after resampling.
		ma_uint64 expected = 0;
		if (ma_decoder_get_length_in_pcm_frames(&decoder, &expected) == MA_SUCCESS && expected > 0) {
			sound->samples.reserve((expected + 64) * c_MixChannels);
		}
		// Read exactly as a playing channel does, until the decoder reports the end.
		constexpr ma_uint64 c_ChunkFrames = 4096;
		ma_result status = MA_SUCCESS;
		ma_uint64 count = 0;
		do {
			const size_t start = sound->samples.size();
			sound->samples.resize(start + c_ChunkFrames * c_MixChannels);
			count = 0;
			status = ma_decoder_read_pcm_frames(&decoder, sound->samples.data() + start, c_ChunkFrames, &count);
			sound->samples.resize(start + count * c_MixChannels);
		} while (status == MA_SUCCESS && count > 0);
		ma_decoder_uninit(&decoder);
		if (status != MA_SUCCESS && status != MA_AT_END) {
			return nullptr;
		}
		sound->samples.shrink_to_fit();
		sound->frames = sound->samples.size() / c_MixChannels;
		return sound;
	}

	DecodedSounds::DecodedSounds() :
	    m_Worker([this] { Run(); }) {}

	DecodedSounds::~DecodedSounds() {
		{
			std::lock_guard lock(m_Mutex);
			m_Stopping = true;
		}
		m_Wake.notify_all();
		m_Worker.join();
	}

	DecodedPCM DecodedSounds::Find(const std::string& path) {
		std::lock_guard lock(m_Mutex);
		auto found = m_Decoded.find(path);
		if (found == m_Decoded.end()) {
			return nullptr;
		}
		found->second.lastUse = ++m_Clock;
		return found->second.pcm;
	}

	void DecodedSounds::Request(const std::string& path, SoundBytes bytes) {
		{
			std::lock_guard lock(m_Mutex);
			if (m_Decoded.count(path) || m_Pending.count(path) || m_Undecodable.count(path)) {
				return;
			}
			m_Pending.insert(path);
			m_Queue.emplace_back(path, std::move(bytes));
		}
		m_Wake.notify_one();
	}

	void DecodedSounds::WaitUntilIdle() {
		std::unique_lock lock(m_Mutex);
		m_Idle.wait(lock, [this] { return m_Pending.empty(); });
	}

	DecodedSounds::Statistics DecodedSounds::GetStatistics() {
		std::lock_guard lock(m_Mutex);
		return {m_Decoded.size(), m_Bytes, m_PlaysFromMemory, m_PlaysDecoding};
	}

	void DecodedSounds::CountPlay(bool fromMemory) {
		std::lock_guard lock(m_Mutex);
		++(fromMemory ? m_PlaysFromMemory : m_PlaysDecoding);
	}

	void DecodedSounds::Run() {
		std::unique_lock lock(m_Mutex);
		while (true) {
			m_Wake.wait(lock, [this] { return m_Stopping || !m_Queue.empty(); });
			if (m_Stopping) {
				return;
			}
			auto [path, bytes] = std::move(m_Queue.front());
			m_Queue.pop_front();
			lock.unlock();
			DecodedPCM decoded;
			try {
				decoded = DecodeWholeSound(bytes);
			} catch (const std::exception&) {
				// Out of memory, most likely: this sound keeps decoding as it plays.
			}
			bytes.reset();
			lock.lock();
			try {
				if (decoded) {
					const size_t size = decoded->samples.size() * sizeof(float);
					m_Decoded[path] = {std::move(decoded), size, ++m_Clock};
					m_Bytes += size;
					EvictBeyondBudget(path);
				} else {
					m_Undecodable.insert(path);
				}
			} catch (const std::exception&) {
			}
			m_Pending.erase(path);
			if (m_Pending.empty()) {
				m_Idle.notify_all();
			}
		}
	}

	void DecodedSounds::EvictBeyondBudget(const std::string& keep) {
		while (m_Bytes > c_BudgetBytes && m_Decoded.size() > 1) {
			auto oldest = m_Decoded.end();
			for (auto entry = m_Decoded.begin(); entry != m_Decoded.end(); ++entry) {
				if (entry->first != keep && (oldest == m_Decoded.end() || entry->second.lastUse < oldest->second.lastUse)) {
					oldest = entry;
				}
			}
			if (oldest == m_Decoded.end()) {
				return;
			}
			m_Bytes -= oldest->second.bytes;
			m_Decoded.erase(oldest);
		}
	}
} // namespace FMOD
