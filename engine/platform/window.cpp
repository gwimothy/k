#include "platform/window.h"

#include "core/log.h"

#include <SDL3/SDL.h>

namespace k {

Window::~Window() { destroy(); }

bool Window::create(const WindowDesc& desc) {
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        log::error("SDL video init failed: {}", SDL_GetError());
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, 1);
#ifndef NDEBUG
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
#endif

    SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (desc.fullscreen) flags |= SDL_WINDOW_FULLSCREEN;
    window_ = SDL_CreateWindow(desc.title.c_str(), desc.width, desc.height, flags);
    if (!window_) {
        log::error("Couldn't create window: {}", SDL_GetError());
        return false;
    }

    context_ = SDL_GL_CreateContext(window_);
    if (!context_) {
        log::error("Couldn't create an OpenGL 4.5 core context: {}", SDL_GetError());
        destroy();
        return false;
    }
    SDL_GL_MakeCurrent(window_, context_);
    setVsync(desc.vsync);
    return true;
}

void Window::destroy() {
    if (context_) {
        SDL_GL_DestroyContext(context_);
        context_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
}

void Window::swap() { SDL_GL_SwapWindow(window_); }

void Window::setVsync(bool enabled) {
    if (!SDL_GL_SetSwapInterval(enabled ? 1 : 0)) log::debug("Couldn't set vsync: {}", SDL_GetError());
}

void Window::setRelativeMouse(bool enabled) {
    relativeMouse_ = enabled;
    SDL_SetWindowRelativeMouseMode(window_, enabled);
}

void Window::drawableSize(int& w, int& h) const { SDL_GetWindowSizeInPixels(window_, &w, &h); }

} // namespace k
