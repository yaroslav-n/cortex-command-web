// Does the port draw the same random numbers as the original game?
//
// The original maps std::mt19937 onto ranges with std::uniform_int_distribution
// and std::uniform_real_distribution, whose algorithms belong to the standard
// library: the original is built with GCC 13 (libstdc++), the browser with libc++.
// The port's RandomGenerator reproduces libstdc++'s algorithms by hand.
//
// OriginalGenerator below is the original's class, copied with its std::
// distributions and call shapes unchanged. Built natively with g++-13 at the
// original's settings (-O3, default floating-point contraction), it prints what
// the original draws. The port's RandomGenerator, built for Wasm, must print the
// same lines. The native build also checks the two against each other directly.
#include "RandomGenerator.h"

#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string_view>
#include <type_traits>

namespace RTE {
	RandomGenerator g_RandomGenerator;
}

#ifndef __EMSCRIPTEN__
// The original's RandomGenerator (Source/System/RTETools.h at 20dfb3ea5), verbatim
// apart from the class name.
class OriginalGenerator {
	std::mt19937 m_RNG;

public:
	void Seed(uint64_t seed) { m_RNG.seed(seed); };

	template <typename floatType = float>
	typename std::enable_if<std::is_floating_point<floatType>::value, floatType>::type RandomNormalNum() {
		return std::uniform_real_distribution<floatType>(floatType(-1.0), std::nextafter(floatType(1.0), std::numeric_limits<floatType>::max()))(m_RNG);
	}

	template <typename intType>
	typename std::enable_if<std::is_integral<intType>::value, intType>::type RandomNormalNum() {
		return std::uniform_int_distribution<intType>(intType(-1), intType(1))(m_RNG);
	}

	template <typename floatType = float>
	typename std::enable_if<std::is_floating_point<floatType>::value, floatType>::type RandomNum() {
		return std::uniform_real_distribution<floatType>(floatType(0.0), std::nextafter(floatType(1.0), std::numeric_limits<floatType>::max()))(m_RNG);
	}

	template <typename intType>
	typename std::enable_if<std::is_integral<intType>::value, intType>::type RandomNum() {
		return std::uniform_int_distribution<intType>(intType(0), intType(1))(m_RNG);
	}

	template <typename floatType = float>
	typename std::enable_if<std::is_floating_point<floatType>::value, floatType>::type RandomNum(floatType min, floatType max) {
		if (max < min) {
			std::swap(min, max);
		}
		return (std::uniform_real_distribution<floatType>(floatType(0.0), std::nextafter(max - min, std::numeric_limits<floatType>::max()))(m_RNG) + min);
	}

	template <typename intType>
	typename std::enable_if<std::is_integral<intType>::value, intType>::type RandomNum(intType min, intType max) {
		if (max < min) {
			std::swap(min, max);
		}
		return (std::uniform_int_distribution<intType>(intType(0), max - min)(m_RNG) + min);
	}
};
#endif

static uint32_t EngineSeed() {
	constexpr uint32_t constSeed = []() {
		std::string_view seedString = "Bubble";
		const uint64_t hugePrime = 18446744073709551557ULL;
		uint64_t seedResult = 0;
		for (char c: seedString) {
			seedResult += static_cast<uint64_t>(c) * hugePrime;
		}
		return static_cast<uint32_t>(seedResult);
	}();
	return constSeed;
}

// Folds each result's exact bit pattern into a running hash.
struct Digest {
	uint64_t hash = 1469598103934665603ULL;
	template <typename T>
	void Add(T value) {
		// Integers are hashed at 64 bits: `unsigned long` and `size_t` are 4 bytes in
		// wasm32 and 8 natively, and the question is whether the values agree.
		if constexpr (std::is_integral<T>::value) {
			AddBytes(static_cast<uint64_t>(value));
		} else {
			AddBytes(value);
		}
	}
	template <typename T>
	void AddBytes(T value) {
		unsigned char bytes[sizeof(T)];
		std::memcpy(bytes, &value, sizeof(T));
		for (unsigned char byte: bytes) {
			hash = (hash ^ byte) * 1099511628211ULL;
		}
	}
};

// Every call shape the engine uses, over enough draws to reach the rare paths
// (rejection in the integer mapping, the clamp below 1 in the real mapping).
// Shapes run interleaved, as they do in the game, so the stream state is shared.
template <typename Generator>
static void Battery(Generator& generator, const char* label) {
	constexpr int rounds = 20000;
	Digest digests[20];
	for (int i = 0; i < rounds; ++i) {
		digests[0].Add(generator.template RandomNum<int>(0, 1000));
		digests[1].Add(generator.template RandomNum<int>(-7, 7));
		digests[2].Add(generator.template RandomNum<int>());
		digests[3].Add(generator.template RandomNormalNum<int>());
		digests[4].Add(generator.template RandomNum<int>(0, INT_MAX));
		digests[5].Add(generator.template RandomNum<int>(-1000000000, 1000000000));
		digests[6].Add(generator.template RandomNum<uint64_t>(0, UINT64_MAX));
		digests[7].Add(generator.template RandomNum<uint64_t>(0, (1ULL << 40) + 12345));
		digests[8].Add(generator.template RandomNum<unsigned long>(0, 12345));
		digests[9].Add(generator.template RandomNum<float>());
		digests[10].Add(generator.template RandomNum<float>(-3.5F, 7.25F));
		digests[11].Add(generator.template RandomNum<float>(0.0F, 360.0F));
		digests[12].Add(generator.template RandomNum<float>(5.0F, -5.0F));
		digests[13].Add(generator.template RandomNormalNum<float>());
		digests[14].Add(generator.template RandomNum<double>());
		digests[15].Add(generator.template RandomNum<double>(-100.0, 100.0));
		digests[16].Add(generator.template RandomNormalNum<double>());
		digests[17].Add(generator.template RandomNum<short>(-100, 100));
		digests[18].Add(generator.template RandomNum<int>(3, 3));
		digests[19].Add(generator.template RandomNum<float>(0.001F, 0.002F));
	}
	std::printf("%s:", label);
	for (const Digest& digest: digests) {
		std::printf(" %016llx", static_cast<unsigned long long>(digest.hash));
	}
	std::printf("\n");
}

int main() {
	// Draw into variables first: function arguments are evaluated in an unspecified
	// order, and GCC and Clang pick differently.
	RTE::g_RandomGenerator.Seed(EngineSeed());
	{
		const int a = RTE::g_RandomGenerator.RandomNum<int>(0, 1000);
		const int b = RTE::g_RandomGenerator.RandomNum<int>(0, 1000);
		const int c = RTE::g_RandomGenerator.RandomNum<int>(-7, 7);
		const float d = RTE::g_RandomGenerator.RandomNum<float>();
		const float e = RTE::g_RandomGenerator.RandomNum<float>(-3.5F, 7.25F);
		std::printf("first draws: %d %d %d %.9g %.9g\n", a, b, c, d, e);
	}

	RTE::g_RandomGenerator.Seed(EngineSeed());
	Battery(RTE::g_RandomGenerator, "port");

#ifndef __EMSCRIPTEN__
	OriginalGenerator original;
	original.Seed(EngineSeed());
	{
		const int a = original.RandomNum<int>(0, 1000);
		const int b = original.RandomNum<int>(0, 1000);
		const int c = original.RandomNum<int>(-7, 7);
		const float d = original.RandomNum<float>();
		const float e = original.RandomNum<float>(-3.5F, 7.25F);
		std::printf("original first draws: %d %d %d %.9g %.9g\n", a, b, c, d, e);
	}
	original.Seed(EngineSeed());
	Battery(original, "original");
#endif
	return 0;
}
