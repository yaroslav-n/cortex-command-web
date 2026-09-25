#include "BigTexture.h"
#include "TextureTileMapping.h"
#include "glad/gl.h"
#include <algorithm>
#include <cmath>
#include "Draw.h"
#include "GLResourceMan.h"
#include "tracy/Tracy.hpp"
#include "tracy/TracyOpenGL.hpp"

using namespace RTE;
int BigTexture::s_MaxGLTextureSize{0};

namespace {
// Texture upload state belongs to the caller; stage tightly packed rows locally.
class ScopedTextureUnpackState {
public:
 ScopedTextureUnpackState() {
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &m_Texture);
  glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &m_Buffer);
  glGetIntegerv(GL_UNPACK_ALIGNMENT, &m_Alignment);
  glGetIntegerv(GL_UNPACK_ROW_LENGTH, &m_RowLength);
  glGetIntegerv(GL_UNPACK_SKIP_ROWS, &m_SkipRows);
  glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &m_SkipPixels);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
  glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
 }
 ~ScopedTextureUnpackState() {
  glPixelStorei(GL_UNPACK_ALIGNMENT, m_Alignment);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, m_RowLength);
  glPixelStorei(GL_UNPACK_SKIP_ROWS, m_SkipRows);
  glPixelStorei(GL_UNPACK_SKIP_PIXELS, m_SkipPixels);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_Buffer);
  glBindTexture(GL_TEXTURE_2D, m_Texture);
 }
 ScopedTextureUnpackState(const ScopedTextureUnpackState&) = delete;
 ScopedTextureUnpackState& operator=(const ScopedTextureUnpackState&) = delete;
private:
 GLint m_Texture, m_Buffer, m_Alignment, m_RowLength, m_SkipRows, m_SkipPixels;
};
}


BigTexture::BigTexture(BITMAP* bitmap) {
	// A null allocation pointer must not become an offset into the caller's PBO.
	// rlLoadTexture and buffer allocation also change bindings/unpack settings.
	ScopedTextureUnpackState unpackState;
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
	if (!s_MaxGLTextureSize) {
		glGetIntegerv(GL_MAX_TEXTURE_SIZE, &s_MaxGLTextureSize);
		s_MaxGLTextureSize /= 2;
	}
	int bitsPerPixel = bitmap_color_depth(bitmap);
	int bytesPerPixel = bitsPerPixel / 8;
	m_Bitmap = bitmap;
	GLBitmapInfo* bitmapExtra = g_GLResourceMan.MakeBitmapInfo();
	bitmap->extra = reinterpret_cast<void*>(bitmapExtra);
	PixelFormat format = bitsPerPixel == 8 ? PIXELFORMAT_UNCOMPRESSED_GRAYSCALE : PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
	m_Width = bitmap->w;
	m_Height = bitmap->h;

	int height = bitmap->h;
	for (int y = 0; y < bitmap->h; y += s_MaxGLTextureSize) {
		int width = bitmap->w;
		for (int x = 0; x < bitmap->w; x += s_MaxGLTextureSize) {
			int regionWidth = std::min(width, s_MaxGLTextureSize);
			int regionHeight = std::min(height, s_MaxGLTextureSize);
			m_Regions.emplace_back(
			    Vector(x, y),
			    regionWidth,
			    regionHeight);
			m_Textures.emplace_back(
			    rlLoadTexture(nullptr, regionWidth, regionHeight, format, 1),
			    regionWidth,
			    regionHeight,
			    1,
			    format);
#ifndef __EMSCRIPTEN__
			GLuint uploadBuffer;
			glGenBuffers(1, &uploadBuffer);
			glBindBuffer(GL_PIXEL_UNPACK_BUFFER, uploadBuffer);
			glBufferData(GL_PIXEL_UNPACK_BUFFER, regionWidth * regionHeight * bytesPerPixel + 1, NULL, GL_STREAM_DRAW);
			glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
			m_UploadBuffers.emplace_back(uploadBuffer);
#else
			// Update reads straight out of the bitmap in the browser and never maps
			// these, so allocating one per tile only costs time and GPU memory. The
			// tile's copy is sized when it is first updated: some layers never are.
			(void)bytesPerPixel;
			m_Shadows.emplace_back();
#endif
			width -= s_MaxGLTextureSize;
		}
		height -= s_MaxGLTextureSize;
	}
}

BigTexture::~BigTexture() {
    if(!m_UploadBuffers.empty()) glDeleteBuffers(static_cast<GLsizei>(m_UploadBuffers.size()),m_UploadBuffers.data());
	for (Texture2D& texture: m_Textures) {
		glDeleteTextures(1, &texture.id);
	}
}

void BigTexture::Draw(Rectangle source, Rectangle dest) {
	ZoneScoped;
	TracyGpuZone("BigTexture::Draw");
    for (std::size_t i=0;i<m_Regions.size();++i) {
        TextureTileMapping mapping;
        if (!MapTextureTile(source,dest,m_Regions[i],mapping)) continue;
#ifdef DEBUG_BUILD
        DrawRectangleLines(mapping.destination.x,mapping.destination.y,mapping.destination.width,mapping.destination.height,{5,0,0,255});
#endif
        DrawTexturePro(m_Textures[i],mapping.source,mapping.destination,{0.0f,0.0f},0.0f,{255,255,255,255});
    }
}

void BigTexture::Update(const Box& updateRegion) {
	ZoneScoped;
	TracyGpuZone("BigTexture Upload");
	if (!m_Bitmap->extra) {
		m_Bitmap->extra = reinterpret_cast<void*>(g_GLResourceMan.MakeBitmapInfo());
	}
	int bytesPerPixel = bitmap_color_depth(m_Bitmap) / 8;
#ifdef __EMSCRIPTEN__
	// Saving and restoring the caller's unpack state took six WebGL queries on
	// every upload, several times a frame. Set a tightly packed layout whatever the
	// caller left, and leave it behind with no texture or pixel buffer bound, which
	// is the state the original leaves.
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
	glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
#else
	ScopedTextureUnpackState unpackState;
#endif
	for (int i = 0; i < m_Regions.size(); ++i) {
		Box intersect = updateRegion.GetIntersection(m_Regions[i]);
		if (!intersect.IsEmpty()) {
#ifdef __EMSCRIPTEN__
			// WebGL cannot map GPU buffers. Scene layers are updated over the whole
			// screen every frame, most of it unchanged, so compare with what the tile
			// holds and send only the changed pixels, read straight out of the bitmap.
			TextureShadow& shadow = m_Shadows[i];
			if (shadow.IsEmpty()) {
				shadow.Reset(m_Textures[i].width, m_Textures[i].height, bytesPerPixel);
			}
			const int x = intersect.m_Corner.GetFloorIntX();
			const int y = intersect.m_Corner.GetFloorIntY();
			glBindTexture(GL_TEXTURE_2D, m_Textures[i].id);
			shadow.Upload(m_Bitmap, x, y, static_cast<int>(std::ceil(intersect.m_Width)), static_cast<int>(std::ceil(intersect.m_Height)),
			              x % s_MaxGLTextureSize, y % s_MaxGLTextureSize, bytesPerPixel == 1 ? GL_RED : GL_RGBA);
			glBindTexture(GL_TEXTURE_2D, 0);
#else
			glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_UploadBuffers[i]);
			size_t pixelsSize = std::ceil(intersect.m_Width) * std::ceil(intersect.m_Height) * bytesPerPixel;
			unsigned char* pixels = (unsigned char*)glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, (intersect.m_Corner.GetFloorIntY() %s_MaxGLTextureSize) * m_Textures[i].width + intersect.m_Corner.GetFloorIntX() % s_MaxGLTextureSize, pixelsSize, GL_MAP_WRITE_BIT|GL_MAP_INVALIDATE_BUFFER_BIT);
			
			for (size_t y = 0; y < static_cast<int>(std::ceil(intersect.m_Height)); y++) {
				memcpy(
					pixels + y * static_cast<int>(std::ceil(intersect.m_Width)) * bytesPerPixel,
					m_Bitmap->line[y + intersect.m_Corner.GetFloorIntY()] + intersect.m_Corner.GetFloorIntX(),
					std::ceil(intersect.m_Width) * bytesPerPixel
				);
			}
			glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);


			glBindTexture(GL_TEXTURE_2D, m_Textures[i].id);
			glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			glTexSubImage2D(
				GL_TEXTURE_2D,
				0, 
				intersect.m_Corner.GetFloorIntX() % s_MaxGLTextureSize, 
				intersect.m_Corner.GetFloorIntY() % s_MaxGLTextureSize, 
				std::ceil(intersect.m_Width), 
				std::ceil(intersect.m_Height), 
				bytesPerPixel == 1 ? GL_RED : GL_RGBA, 
				GL_UNSIGNED_BYTE,
				reinterpret_cast<void*>((intersect.m_Corner.GetFloorIntY() % s_MaxGLTextureSize) * m_Textures[i].width + intersect.m_Corner.GetFloorIntX() % s_MaxGLTextureSize)
			);

			glBindTexture(GL_TEXTURE_2D, 0);
			glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
#endif
		}
	}
}
