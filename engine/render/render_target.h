#pragma once
// Offscreen framebuffers. The scene renders into an HDR (RGBA16F) target, optionally
// multisampled; it's resolved and then tonemapped onto the window in a post pass.

#include <cstdint>

namespace k {

class RenderTarget {
public:
    RenderTarget() = default;
    RenderTarget(const RenderTarget&) = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;
    ~RenderTarget();

    // samples <= 1 creates a regular texture-backed target; > 1 a multisampled one.
    // Returns false if the driver rejects the configuration.
    bool create(int width, int height, int samples, bool withDepth);
    void release();

    void bind() const;
    // Resolve (or copy) color into `dst`.
    void blitTo(const RenderTarget& dst) const;

    bool matches(int width, int height, int samples) const {
        return fbo_ && width_ == width && height_ == height && samples_ == samples;
    }
    uint32_t colorTexture() const { return color_; }
    int width() const { return width_; }
    int height() const { return height_; }
    int samples() const { return samples_; }

private:
    uint32_t fbo_ = 0, color_ = 0, depth_ = 0;
    int width_ = 0, height_ = 0, samples_ = 0;
};

} // namespace k
