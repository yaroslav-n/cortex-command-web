#pragma once
#include "glad/gl.h"

#include <vector>

struct BITMAP;

namespace RTE {
	/// A copy of what a texture holds, so that uploading to it sends only the pixels that changed.
	/// In the browser every upload is copied twice more, into memory shared with the GPU process
	/// and by that process into the texture, so comparing a screenful against this copy costs less
	/// than sending it again. A new WebGL texture is all zeros, and so is a new copy: the two stay
	/// the same as long as every upload to the texture goes through Upload.
	class TextureShadow {
	public:
		/// Sizes this for a texture that was just created, or created again, without data.
		/// @param width The texture's width in pixels.
		/// @param height The texture's height in pixels.
		/// @param bytesPerPixel Bytes in one pixel of the texture and of the bitmaps uploaded to it.
		void Reset(int width, int height, int bytesPerPixel);

		/// Whether Reset has not sized this for a texture yet.
		bool IsEmpty() const { return m_Pixels.empty(); }

		/// Uploads the pixels of an area of a bitmap that differ from what the texture holds, to the
		/// texture bound to GL_TEXTURE_2D. No pixel unpack buffer may be bound, and GL_UNPACK_SKIP_ROWS
		/// and GL_UNPACK_SKIP_PIXELS must be 0. Leaves GL_UNPACK_ROW_LENGTH at 0, and GL_UNPACK_ALIGNMENT
		/// at 1 if anything was sent. Parts of the area past the right or bottom edge of the bitmap or
		/// the texture are left out; an area with a negative position is ignored.
		/// @param bitmap The memory bitmap to upload from, with this copy's bytes per pixel.
		/// @param x The left of the area in the bitmap.
		/// @param y The top of the area in the bitmap.
		/// @param width The width of the area.
		/// @param height The height of the area.
		/// @param textureX Where the area's left lands in the texture.
		/// @param textureY Where the area's top lands in the texture.
		/// @param format The pixel format of the bitmap's data, such as GL_RED or GL_RGBA.
		void Upload(const BITMAP* bitmap, int x, int y, int width, int height, int textureX, int textureY, GLenum format);

	private:
		std::vector<unsigned char> m_Pixels; //!< The texture's pixels, in rows without padding.
		int m_Width = 0; //!< The texture's width in pixels.
		int m_Height = 0; //!< The texture's height in pixels.
		int m_BytesPerPixel = 0; //!< Bytes in one pixel.
	};
} // namespace RTE
