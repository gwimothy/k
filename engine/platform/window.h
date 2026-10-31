#pragma once
// The OS window + OpenGL context (SDL3).

#include <string>

struct SDL_Window;
typedef struct SDL_GLContextState* SDL_GLContext;

namespace k {

struct WindowDesc {
    std::string title = "k";
    int width = 1280;
    int height = 720;
    bool fullscreen = false;
    bool vsync = true;
};

class Window {
public:
    Window() = default;
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    ~Window();

    bool create(const WindowDesc& desc);
    void destroy();

    void swap();
    void setVsync(bool enabled);
    void setRelativeMouse(bool enabled);
    bool relativeMouse() const { return relativeMouse_; }
    void drawableSize(int& w, int& h) const;

    SDL_Window* handle() const { return window_; }
    SDL_GLContext glContext() const { return context_; }

private:
    SDL_Window* window_ = nullptr;
    SDL_GLContext context_ = nullptr;
    bool relativeMouse_ = false;
};

} // namespace k
