#pragma once

// Float trigonometry for the simulation, computed in double precision and
// rounded once.
//
// The float libm entry points differ between builds: the native library and
// Emscripten's musl-derived one disagree by one unit in the last place on
// `sinf` and `atan2f`. Computing with more precision and narrowing afterwards
// discards the digits where the two implementations disagree, so both builds
// produce the same float. The double versions of `atan2`, `tan`, `asin` and
// `acos` also differ by one unit in the last place, which is why the narrowing
// matters rather than simply switching to double everywhere.
//
// `tests/float_contract.cpp` checks this; see notes/randomness.md.

#include <cmath>

namespace RTE {
	inline float Sin(float radians) { return static_cast<float>(std::sin(static_cast<double>(radians))); }
	inline float Cos(float radians) { return static_cast<float>(std::cos(static_cast<double>(radians))); }
	inline float Tan(float radians) { return static_cast<float>(std::tan(static_cast<double>(radians))); }
	inline float Asin(float value) { return static_cast<float>(std::asin(static_cast<double>(value))); }
	inline float Acos(float value) { return static_cast<float>(std::acos(static_cast<double>(value))); }
	inline float Atan(float value) { return static_cast<float>(std::atan(static_cast<double>(value))); }
	inline float Atan2(float y, float x) { return static_cast<float>(std::atan2(static_cast<double>(y), static_cast<double>(x))); }
} // namespace RTE
