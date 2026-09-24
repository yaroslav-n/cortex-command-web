#pragma once

// The simulation's random number generator, kept free of engine dependencies so
// its sequence can be compared directly between builds.
//
// The generator is seeded with a constant on purpose — once at startup and again
// in Activity::Start, before the Scene loads — so a given mission generates the
// same debris, gib velocities and weapon spread every time. std::mt19937 is
// specified exactly, but std::uniform_int_distribution and
// std::uniform_real_distribution are not: each standard library maps the same
// engine output onto a range with its own algorithm, and libc++ (Emscripten)
// differs from libstdc++ (the original game's GCC build).
//
// So the mapping is written out here, and it reproduces libstdc++'s exactly — the
// GCC 13 headers the original is built with (bits/uniform_int_dist.h and
// generate_canonical in bits/random.tcc). With it the browser draws the same
// values as the original, and every map the original generates is generated
// identically here. tests/random_contract.cpp compares this class with the real
// std:: distributions, using the original's own call shapes.

#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <type_traits>
#include <utility>

namespace RTE {

	class RandomGenerator {
		std::mt19937 m_RNG; //!< The random number generator used for all random functions.

		// Two builds that agree on the generator can still diverge by drawing from
		// it a different number of times, or in a different order. Counting the
		// draws and folding them into a hash tells the two cases apart without
		// disturbing the sequence itself.
		uint64_t m_DrawCount = 0;
		uint64_t m_DrawHash = 1469598103934665603ULL;

		uint32_t Next() {
			const uint32_t value = static_cast<uint32_t>(m_RNG());
			++m_DrawCount;
			m_DrawHash = (m_DrawHash ^ value) * 1099511628211ULL;
			return value;
		}

		/// libstdc++'s std::uniform_int_distribution for std::mt19937, returning a
		/// value in [0, urange]. GCC 13, bits/uniform_int_dist.h: ranges narrower than
		/// the generator use Lemire's multiply-and-shift with rejection (_S_nd), a
		/// range equal to it takes one draw as is, and wider ranges combine a
		/// recursive draw for the high part with one draw for the low part.
		uint64_t LibstdcxxUniformUnsigned(uint64_t urange) {
			constexpr uint64_t urngrange = 0xFFFFFFFFULL;
			if (urngrange > urange) {
				const uint32_t range = static_cast<uint32_t>(urange + 1);
				uint64_t product = static_cast<uint64_t>(Next()) * range;
				uint32_t low = static_cast<uint32_t>(product);
				if (low < range) {
					const uint32_t threshold = static_cast<uint32_t>(0u - range) % range;
					while (low < threshold) {
						product = static_cast<uint64_t>(Next()) * range;
						low = static_cast<uint32_t>(product);
					}
				}
				return product >> 32;
			}
			if (urngrange < urange) {
				constexpr uint64_t uerngrange = urngrange + 1;
				uint64_t ret;
				uint64_t high;
				do {
					high = uerngrange * LibstdcxxUniformUnsigned(urange / uerngrange);
					ret = high + Next();
				} while (ret > urange || ret < high);
				return ret;
			}
			return Next();
		}

		/// std::uniform_int_distribution<intType>(a, b)(m_RNG), as libstdc++ computes it.
		template <typename intType>
		intType LibstdcxxUniformInt(intType a, intType b) {
			using Unsigned = typename std::make_unsigned<intType>::type;
			const uint64_t urange = static_cast<uint64_t>(static_cast<Unsigned>(static_cast<Unsigned>(b) - static_cast<Unsigned>(a)));
			return static_cast<intType>(static_cast<Unsigned>(static_cast<Unsigned>(LibstdcxxUniformUnsigned(urange)) + static_cast<Unsigned>(a)));
		}

		/// std::generate_canonical<floatType, digits>(m_RNG), as libstdc++ computes it
		/// for a 32 bit generator: one draw for float, two for double (the second
		/// scaled by 2^32), divided by 2^32 or 2^64, and pulled below 1 if rounding
		/// reached it.
		template <typename floatType>
		floatType LibstdcxxCanonical() {
			floatType sum;
			floatType scale;
			if constexpr (std::numeric_limits<floatType>::digits <= 32) {
				sum = static_cast<floatType>(Next());
				scale = static_cast<floatType>(4294967296.0);
			} else {
				sum = static_cast<floatType>(Next());
				sum += static_cast<floatType>(Next()) * static_cast<floatType>(4294967296.0);
				scale = static_cast<floatType>(18446744073709551616.0);
			}
			floatType ret = sum / scale;
			if (ret >= floatType(1)) {
				ret = std::nextafter(floatType(1), floatType(0));
			}
			return ret;
		}

		/// std::uniform_real_distribution<floatType>(a, b)(m_RNG), as libstdc++
		/// computes it: canonical * (b - a) + a.
		template <typename floatType>
		floatType LibstdcxxUniformReal(floatType a, floatType b) {
			return (LibstdcxxCanonical<floatType>() * (b - a)) + a;
		}

	public:
		/// Seed the random number generator.
		void Seed(uint64_t seed) { m_RNG.seed(static_cast<std::mt19937::result_type>(seed)); };

		/// How many values have been drawn, and a hash of all of them, for
		/// comparing the stream between builds.
		uint64_t GetDrawCount() const { return m_DrawCount; }
		uint64_t GetDrawHash() const { return m_DrawHash; }

		// Each method below is the original's, with its std:: distribution replaced by
		// the libstdc++ reproduction above and the arguments passed unchanged.

		/// Function template which returns a uniformly distributed random number in the range [-1, 1].
		/// @return Uniformly distributed random number in the range [-1, 1].
		template <typename floatType = float>
		typename std::enable_if<std::is_floating_point<floatType>::value, floatType>::type RandomNormalNum() {
			return LibstdcxxUniformReal<floatType>(floatType(-1.0), std::nextafter(floatType(1.0), std::numeric_limits<floatType>::max()));
		}

		/// Function template specialization for int types which returns a uniformly distributed random number in the range [-1, 1].
		/// @return Uniformly distributed random number in the range [-1, 1].
		template <typename intType>
		typename std::enable_if<std::is_integral<intType>::value, intType>::type RandomNormalNum() {
			return LibstdcxxUniformInt<intType>(intType(-1), intType(1));
		}

		/// Function template which returns a uniformly distributed random number in the range [0, 1].
		/// @return Uniformly distributed random number in the range [0, 1].
		template <typename floatType = float>
		typename std::enable_if<std::is_floating_point<floatType>::value, floatType>::type RandomNum() {
			return LibstdcxxUniformReal<floatType>(floatType(0.0), std::nextafter(floatType(1.0), std::numeric_limits<floatType>::max()));
		}

		/// Function template specialization for int types which returns a uniformly distributed random number in the range [0, 1].
		/// @return Uniformly distributed random number in the range [0, 1].
		template <typename intType>
		typename std::enable_if<std::is_integral<intType>::value, intType>::type RandomNum() {
			return LibstdcxxUniformInt<intType>(intType(0), intType(1));
		}

		/// Function template which returns a uniformly distributed random number in the range [min, max].
		/// @param min Lower boundary of the range to pick a number from.
		/// @param max Upper boundary of the range to pick a number from.
		/// @return Uniformly distributed random number in the range [min, max].
		template <typename floatType = float>
		typename std::enable_if<std::is_floating_point<floatType>::value, floatType>::type RandomNum(floatType min, floatType max) {
			if (max < min) {
				std::swap(min, max);
			}
			return (LibstdcxxUniformReal<floatType>(floatType(0.0), std::nextafter(max - min, std::numeric_limits<floatType>::max())) + min);
		}

		/// Function template specialization for int types which returns a uniformly distributed random number in the range [min, max].
		/// @param min Lower boundary of the range to pick a number from.
		/// @param max Upper boundary of the range to pick a number from.
		/// @return Uniformly distributed random number in the range [min, max].
		template <typename intType>
		typename std::enable_if<std::is_integral<intType>::value, intType>::type RandomNum(intType min, intType max) {
			if (max < min) {
				std::swap(min, max);
			}
			return (LibstdcxxUniformInt<intType>(intType(0), static_cast<intType>(max - min)) + min);
		}
	};

	extern RandomGenerator g_RandomGenerator; //!< The global random number generator used in our simulation thread.

	/// Seed the global random number generator.
	void SeedRNG();

	// TODO: Maybe remove these passthrough functions and force the user to manually specify if they want the simulation thread random,
	// Or, in future, a render-thread random, as right now determinism isn't viable because framerate affects sim updates per draw
	template <typename floatType = float>
	typename std::enable_if<std::is_floating_point<floatType>::value, floatType>::type RandomNormalNum() {
		return g_RandomGenerator.RandomNormalNum();
	}

	template <typename intType>
	typename std::enable_if<std::is_integral<intType>::value, intType>::type RandomNormalNum() {
		return g_RandomGenerator.RandomNormalNum();
	}

	template <typename floatType = float>
	typename std::enable_if<std::is_floating_point<floatType>::value, floatType>::type RandomNum() {
		return g_RandomGenerator.RandomNum();
	}

	template <typename intType>
	typename std::enable_if<std::is_integral<intType>::value, intType>::type RandomNum() {
		return g_RandomGenerator.RandomNum();
	}

	template <typename floatType = float>
	typename std::enable_if<std::is_floating_point<floatType>::value, floatType>::type RandomNum(floatType min, floatType max) {
		return g_RandomGenerator.RandomNum(min, max);
	}

	template <typename intType>
	typename std::enable_if<std::is_integral<intType>::value, intType>::type RandomNum(intType min, intType max) {
		return g_RandomGenerator.RandomNum(min, max);
	}
} // namespace RTE
