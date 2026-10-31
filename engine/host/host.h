#pragma once
// The host owns every engine system and runs the main loop (Source's CHostState / host.cpp).

#include "client/client.h"
#include "server/server.h"

#include <memory>
#include <string>
#include <vector>

namespace k {

class Window;
class Renderer;
class ConsoleUI;

struct LaunchOptions {
    bool dedicated = false;
    bool fullscreen = false;
    int width = 1280;
    int height = 720;
    std::string gameDir = "base";
    std::string startupCommands; // the "+cmd args" part of the command line
    bool hasMapCommand = false;
};

LaunchOptions parseCommandLine(const std::vector<std::string>& args);

class Host {
public:
    Host();
    ~Host();

    int run(const std::vector<std::string>& args);

    // Console command handlers.
    void startMap(const std::string& name);
    void disconnect();
    void quit() { quit_ = true; }
    void toggleConsole();
    void screenshot();
    void writeConfig();
    void reloadShaders();
    void reloadMaterials();
    void setPos(const Vec3& origin);
    void setAng(const QAngle& angles);
    void printPos();
    void forwardToServer(const std::string& text);

    static Host* instance() { return instance_; }

private:
    bool initClient(const LaunchOptions& opts);
    void shutdownClient();
    void frame(double frameTime);
    void pumpEvents();
    void renderFrame(float alpha);
    void drawOverlay();
    void updateMouseCapture();

    static Host* instance_;

    LaunchOptions opts_;
    bool quit_ = false;
    double tickAccumulator_ = 0.0;
    uint64_t hostTicks_ = 0;
    uint64_t hostFrames_ = 0;
    float mouseDx_ = 0, mouseDy_ = 0;
    double fpsTimer_ = 0;
    int fpsFrames_ = 0;
    float fps_ = 0;

    Server server_;
    Client client_;

    std::unique_ptr<Window> window_;
    std::unique_ptr<Renderer> renderer_;
    std::unique_ptr<ConsoleUI> console_;
    bool imguiReady_ = false;
    bool windowFocused_ = true;
};

} // namespace k
