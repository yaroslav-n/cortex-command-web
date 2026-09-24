#pragma once
#include "glad/gl.h"
#include <algorithm>
#include <cstddef>
#include <limits>
#include <vector>

namespace RTE {
// Call on the owning GL thread, after queued draws have been flushed. Returns
// tightly packed top-to-bottom RGBA8, without changing framebuffer/pack state.
inline bool ReadFramebufferRGBA(GLuint framebuffer, int width, int height, std::vector<unsigned char>& output) {
    if (width <= 0 || height <= 0 || static_cast<std::size_t>(width) > std::numeric_limits<std::size_t>::max() / 4 / static_cast<std::size_t>(height)) return false;
    GLint previousRead, previousPackBuffer, alignment, rowLength, skipRows, skipPixels;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousRead);
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &previousPackBuffer);
    glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
    glGetIntegerv(GL_PACK_ROW_LENGTH, &rowLength);
    glGetIntegerv(GL_PACK_SKIP_ROWS, &skipRows);
    glGetIntegerv(GL_PACK_SKIP_PIXELS, &skipPixels);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
    if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, previousRead);
        return false;
    }
    GLint targetReadBuffer;
    glGetIntegerv(GL_READ_BUFFER, &targetReadBuffer);
    // Allocate before modifying the pack state; restore the binding on failure.
    try { output.resize(static_cast<std::size_t>(width) * height * 4); }
    catch (...) { glBindFramebuffer(GL_READ_FRAMEBUFFER, previousRead); throw; }
    glReadBuffer(framebuffer == 0 ? GL_BACK : GL_COLOR_ATTACHMENT0);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_SKIP_ROWS, 0);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, output.data());
    glPixelStorei(GL_PACK_ALIGNMENT, alignment);
    glPixelStorei(GL_PACK_ROW_LENGTH, rowLength);
    glPixelStorei(GL_PACK_SKIP_ROWS, skipRows);
    glPixelStorei(GL_PACK_SKIP_PIXELS, skipPixels);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, previousPackBuffer);
    glReadBuffer(targetReadBuffer);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, previousRead);
    const std::size_t pitch = static_cast<std::size_t>(width) * 4;
    for (int y = 0; y < height / 2; ++y) {
        auto top = output.begin() + static_cast<std::size_t>(y) * pitch;
        auto bottom = output.begin() + static_cast<std::size_t>(height - 1 - y) * pitch;
        std::swap_ranges(top, top + pitch, bottom);
    }
    return true;
}
} // namespace RTE
