#include "render/render_target.h"

#include "core/log.h"

#include <glad/gl.h>

namespace k {

RenderTarget::~RenderTarget() { release(); }

void RenderTarget::release() {
    if (fbo_) glDeleteFramebuffers(1, &fbo_);
    if (color_) glDeleteTextures(1, &color_);
    if (depth_) glDeleteRenderbuffers(1, &depth_);
    fbo_ = color_ = depth_ = 0;
    width_ = height_ = samples_ = 0;
}

bool RenderTarget::create(int width, int height, int samples, bool withDepth) {
    release();
    width_ = width;
    height_ = height;
    samples_ = samples;
    glCreateFramebuffers(1, &fbo_);
    if (samples > 1) {
        glCreateTextures(GL_TEXTURE_2D_MULTISAMPLE, 1, &color_);
        glTextureStorage2DMultisample(color_, samples, GL_RGBA16F, width, height, GL_TRUE);
    } else {
        glCreateTextures(GL_TEXTURE_2D, 1, &color_);
        glTextureStorage2D(color_, 1, GL_RGBA16F, width, height);
        glTextureParameteri(color_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(color_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(color_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(color_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glNamedFramebufferTexture(fbo_, GL_COLOR_ATTACHMENT0, color_, 0);
    if (withDepth) {
        glCreateRenderbuffers(1, &depth_);
        if (samples > 1)
            glNamedRenderbufferStorageMultisample(depth_, samples, GL_DEPTH_COMPONENT24, width, height);
        else
            glNamedRenderbufferStorage(depth_, GL_DEPTH_COMPONENT24, width, height);
        glNamedFramebufferRenderbuffer(fbo_, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_);
    }
    GLenum status = glCheckNamedFramebufferStatus(fbo_, GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        log::warn("Render target {}x{} ({} samples) incomplete: 0x{:x}", width, height, samples, status);
        release();
        return false;
    }
    return true;
}

void RenderTarget::bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, width_, height_);
}

void RenderTarget::blitTo(const RenderTarget& dst) const {
    glBlitNamedFramebuffer(fbo_, dst.fbo_, 0, 0, width_, height_, 0, 0, dst.width_, dst.height_,
                           GL_COLOR_BUFFER_BIT, GL_NEAREST);
}

} // namespace k
