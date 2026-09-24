// The simulation is deterministic by design, so any arithmetic that differs
// between the native and browser builds makes them drift apart during play in
// the same silent way the random stream did. Wasm arithmetic is strict IEEE-754;
// a native compiler may contract multiply-add pairs and ships its own libm. This
// prints exact bit patterns so the two builds can be diffed.
#include "FloatMath.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

static uint32_t bits(float value) {
	uint32_t out;
	std::memcpy(&out, &value, sizeof(out));
	return out;
}

static uint64_t bits(double value) {
	uint64_t out;
	std::memcpy(&out, &value, sizeof(out));
	return out;
}

// Deliberately shaped like a multiply-add so a contracting compiler can fuse it.
static float MultiplyAdd(float a, float b, float c) {
	return a * b + c;
}

int main() {
	const float samples[] = {0.1F, 0.5F, 1.0F, 1.5F, 2.7182818F, 3.14159265F, 12.345F, 1234.5678F, 1e-7F, 1e7F};

	std::printf("fma:");
	for (float value: samples) {
		std::printf(" %08x", bits(MultiplyAdd(value, 1.0F / 3.0F, 0.1F)));
	}
	std::printf("\n");

	std::printf("sinf:");
	for (float value: samples) {
		std::printf(" %08x", bits(std::sin(value)));
	}
	std::printf("\n");

	std::printf("cosf:");
	for (float value: samples) {
		std::printf(" %08x", bits(std::cos(value)));
	}
	std::printf("\n");

	std::printf("atan2f:");
	for (float value: samples) {
		std::printf(" %08x", bits(std::atan2(value, 1.75F)));
	}
	std::printf("\n");

	std::printf("sqrtf:");
	for (float value: samples) {
		std::printf(" %08x", bits(std::sqrt(value)));
	}
	std::printf("\n");

	std::printf("powf:");
	for (float value: samples) {
		std::printf(" %08x", bits(std::pow(value, 1.5F)));
	}
	std::printf("\n");

	std::printf("expf:");
	for (float value: samples) {
		std::printf(" %08x", bits(std::exp(value * 0.25F)));
	}
	std::printf("\n");

	std::printf("logf:");
	for (float value: samples) {
		std::printf(" %08x", bits(std::log(value + 1.0F)));
	}
	std::printf("\n");

	std::printf("fmodf:");
	for (float value: samples) {
		std::printf(" %08x", bits(std::fmod(value, 1.7F)));
	}
	std::printf("\n");

	// A vector normalize and a rotation, the shapes the engine actually uses.
	std::printf("normalize:");
	for (float value: samples) {
		const float x = value;
		const float y = value * 0.37F - 1.0F;
		const float magnitude = std::sqrt(x * x + y * y);
		std::printf(" %08x%08x", bits(x / magnitude), bits(y / magnitude));
	}
	std::printf("\n");

	// Through the engine's wrappers, because that is what the engine calls. The
	// raw std::sin/std::cos above are one unit in the last place apart between the
	// two libms, and a rotation built on them inherits that.
	std::printf("rotate:");
	for (float value: samples) {
		const float angle = value * 0.31F;
		const float s = RTE::Sin(angle);
		const float c = RTE::Cos(angle);
		std::printf(" %08x%08x", bits(3.5F * c - 1.25F * s), bits(3.5F * s + 1.25F * c));
	}
	std::printf("\n");

	// Routing the float trig through double precision is only viable if the
	// double versions agree between the builds, so check each one used.
	std::printf("double sin:");
	for (float value: samples) {
		std::printf(" %016llx", static_cast<unsigned long long>(bits(std::sin(static_cast<double>(value)))));
	}
	std::printf("\n");

	std::printf("double cos:");
	for (float value: samples) {
		std::printf(" %016llx", static_cast<unsigned long long>(bits(std::cos(static_cast<double>(value)))));
	}
	std::printf("\n");

	std::printf("double atan2:");
	for (float value: samples) {
		std::printf(" %016llx", static_cast<unsigned long long>(bits(std::atan2(static_cast<double>(value), 1.75))));
	}
	std::printf("\n");

	std::printf("double tan:");
	for (float value: samples) {
		std::printf(" %016llx", static_cast<unsigned long long>(bits(std::tan(static_cast<double>(value) * 0.3))));
	}
	std::printf("\n");

	std::printf("double asin:");
	for (float value: samples) {
		std::printf(" %016llx", static_cast<unsigned long long>(bits(std::asin(std::fmod(static_cast<double>(value), 1.0)))));
	}
	std::printf("\n");

	std::printf("double acos:");
	for (float value: samples) {
		std::printf(" %016llx", static_cast<unsigned long long>(bits(std::acos(std::fmod(static_cast<double>(value), 1.0)))));
	}
	std::printf("\n");

	// And the float results obtained by computing in double then narrowing.
	std::printf("narrowed sinf:");
	for (float value: samples) {
		std::printf(" %08x", bits(static_cast<float>(std::sin(static_cast<double>(value)))));
	}
	std::printf("\n");

	std::printf("narrowed atan2f:");
	for (float value: samples) {
		std::printf(" %08x", bits(static_cast<float>(std::atan2(static_cast<double>(value), 1.75))));
	}
	std::printf("\n");

	// The engine's own wrappers, which are what gameplay actually calls.
	std::printf("RTE::Sin:");
	for (float value: samples) {
		std::printf(" %08x", bits(RTE::Sin(value)));
	}
	std::printf("\n");

	std::printf("RTE::Cos:");
	for (float value: samples) {
		std::printf(" %08x", bits(RTE::Cos(value)));
	}
	std::printf("\n");

	std::printf("RTE::Atan2:");
	for (float value: samples) {
		std::printf(" %08x", bits(RTE::Atan2(value, 1.75F)));
	}
	std::printf("\n");

	// A sweep wide enough to catch a narrowing boundary the samples above miss.
	unsigned int sinAccumulator = 0;
	unsigned int atanAccumulator = 0;
	for (int step = 0; step < 200000; ++step) {
		const float angle = static_cast<float>(step) * 0.000073F - 7.0F;
		sinAccumulator ^= bits(RTE::Sin(angle)) + step;
		atanAccumulator ^= bits(RTE::Atan2(angle, 0.625F)) + step;
	}
	std::printf("sweep: sin %08x atan2 %08x\n", sinAccumulator, atanAccumulator);

	return 0;
}
