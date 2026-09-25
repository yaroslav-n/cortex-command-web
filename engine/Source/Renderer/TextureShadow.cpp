#include "TextureShadow.h"
#include "allegro.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

using namespace RTE;

namespace {
	// Changed rows closer together than this go up in one upload: a call costs more than
	// sending a few unchanged rows with it.
	constexpr int c_MaxRowGap = 16;

	// Rows that match are the usual case, so these compare 32 bytes a step. The C library's
	// memcmp compares words only when both rows start word-aligned, which a scene area rarely does.
	bool SameBlock(const unsigned char* a, const unsigned char* b) {
		std::uint64_t a0, a1, a2, a3, b0, b1, b2, b3;
		std::memcpy(&a0, a, 8);
		std::memcpy(&a1, a + 8, 8);
		std::memcpy(&a2, a + 16, 8);
		std::memcpy(&a3, a + 24, 8);
		std::memcpy(&b0, b, 8);
		std::memcpy(&b1, b + 8, 8);
		std::memcpy(&b2, b + 16, 8);
		std::memcpy(&b3, b + 24, 8);
		return ((a0 ^ b0) | (a1 ^ b1) | (a2 ^ b2) | (a3 ^ b3)) == 0;
	}

	/// The index of the first byte where two rows differ, or size if they match.
	std::size_t FirstDifference(const unsigned char* a, const unsigned char* b, std::size_t size) {
		std::size_t i = 0;
		while (i + 32 <= size && SameBlock(a + i, b + i)) {
			i += 32;
		}
		while (i < size && a[i] == b[i]) {
			++i;
		}
		return i;
	}

	/// One past the index of the last byte where two rows differ, or 0 if they match.
	std::size_t EndOfDifference(const unsigned char* a, const unsigned char* b, std::size_t size) {
		std::size_t end = size;
		while (end >= 32 && SameBlock(a + end - 32, b + end - 32)) {
			end -= 32;
		}
		while (end > 0 && a[end - 1] == b[end - 1]) {
			--end;
		}
		return end;
	}
} // namespace

void TextureShadow::Reset(int width, int height, int bytesPerPixel) {
	m_Width = std::max(width, 0);
	m_Height = std::max(height, 0);
	m_BytesPerPixel = std::max(bytesPerPixel, 0);
	m_Pixels.assign(static_cast<std::size_t>(m_Width) * m_Height * m_BytesPerPixel, 0);
}

void TextureShadow::Upload(const BITMAP* bitmap, int x, int y, int width, int height, int textureX, int textureY, GLenum format) {
	if (m_Pixels.empty() || x < 0 || y < 0 || textureX < 0 || textureY < 0) {
		return;
	}
	width = std::min({width, bitmap->w - x, m_Width - textureX});
	height = std::min({height, bitmap->h - y, m_Height - textureY});
	if (width <= 0 || height <= 0) {
		return;
	}

	const std::size_t pixelSize = m_BytesPerPixel;
	const std::size_t areaRowSize = static_cast<std::size_t>(width) * pixelSize;
	const std::size_t copyPitch = static_cast<std::size_t>(m_Width) * pixelSize;
	// Allegro's memory bitmaps space their rows evenly, and a sub-bitmap's rows as its parent's,
	// so GL can read an area straight out of the bitmap given the row length.
	const std::ptrdiff_t bitmapPitch = bitmap->h > 1 ? bitmap->line[1] - bitmap->line[0] : static_cast<std::ptrdiff_t>(bitmap->w) * pixelSize;

	bool unpackStateSet = false;
	int runTop = -1; // The first row of the changed rows not sent yet, or -1 if there are none.
	int runBottom = 0; // One past the last of those rows.
	int runLeft = 0; // The leftmost changed pixel in those rows.
	int runRight = 0; // One past the rightmost.
	auto sendRun = [&]() {
		if (!unpackStateSet) {
			glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(bitmapPitch / static_cast<std::ptrdiff_t>(pixelSize)));
			unpackStateSet = true;
		}
		glTexSubImage2D(GL_TEXTURE_2D, 0, textureX + runLeft, textureY + runTop, runRight - runLeft, runBottom - runTop, format, GL_UNSIGNED_BYTE,
		                bitmap->line[y + runTop] + static_cast<std::size_t>(x + runLeft) * pixelSize);
		runTop = -1;
	};

	for (int row = 0; row < height; ++row) {
		const unsigned char* source = bitmap->line[y + row] + static_cast<std::size_t>(x) * pixelSize;
		unsigned char* copy = m_Pixels.data() + static_cast<std::size_t>(textureY + row) * copyPitch + static_cast<std::size_t>(textureX) * pixelSize;
		const std::size_t first = FirstDifference(source, copy, areaRowSize);
		if (first == areaRowSize) {
			if (runTop >= 0 && row + 1 - runBottom >= c_MaxRowGap) {
				sendRun();
			}
			continue;
		}
		const std::size_t end = first + EndOfDifference(source + first, copy + first, areaRowSize - first);
		// The bytes around this span already match, so the copy now matches the whole row.
		std::memcpy(copy + first, source + first, end - first);

		const int left = static_cast<int>(first / pixelSize);
		const int right = static_cast<int>((end + pixelSize - 1) / pixelSize);
		if (runTop < 0) {
			runTop = row;
			runLeft = left;
			runRight = right;
		} else {
			runLeft = std::min(runLeft, left);
			runRight = std::max(runRight, right);
		}
		runBottom = row + 1;
	}
	if (runTop >= 0) {
		sendRun();
	}
	if (unpackStateSet) {
		glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	}
}
