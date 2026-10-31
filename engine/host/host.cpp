#include "host/host.h"

#include "client/bindings.h"
#include "core/cmdbuffer.h"
#include "core/cvar.h"
#include "core/filesystem.h"
#include "core/log.h"
#include "core/time.h"
#include "platform/keys.h"
#include "platform/window.h"
#include "render/renderer.h"
#include "ui/console_ui.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>

namespace k {

Host* Host::instance_ = nullptr;

namespace {

ConVar host_timescale("host_timescale", "1", FCVAR_CHEAT | FCVAR_REPLICATED, "Game speed multiplier.", 0.01f, 10.0f);
ConVar host_maxticks("host_maxticks", "0", FCVAR_DEVELOPMENT, "Quit after this many server ticks (0 = never). For automated tests.");
ConVar fps_max("fps_max", "300", FCVAR_ARCHIVE, "Frame rate limit (0 = unlimited).", 0.0f, 1000.0f);
ConVar mat_vsync("mat_vsync", "1", FCVAR_ARCHIVE, "Wait for vertical sync.");
ConVar cl_showfps("cl_showfps", "0", FCVAR_ARCHIVE, "Show frame rate.");
ConVar cl_showpos("cl_showpos", "0", FCVAR_ARCHIVE, "Show player position and velocity.");
ConVar player_name("name", "player", FCVAR_ARCHIVE, "Your player name.");

// Every server tick uses at most this many iterations per frame so a long hitch can't
// snowball into a "spiral of death".
constexpr int kMaxTicksPerFrame = 8;

Host* host() { return Host::instance(); }

ConCommand cmd_quit("quit", [](const CmdArgs&) { if (host()) host()->quit(); }, "Exit the engine.");
ConCommand cmd_exit("exit", [](const CmdArgs&) { if (host()) host()->quit(); }, "Exit the engine.");
ConCommand cmd_map(
    "map",
    [](const CmdArgs& a) {
        if (a.argc() < 2) {
            log::info("Usage: map <name>");
            return;
        }
        if (host()) host()->startMap(a.arg(1));
    },
    "Start a local server on a map.");
ConCommand cmd_disconnect("disconnect", [](const CmdArgs&) { if (host()) host()->disconnect(); }, "Leave the current game.");
ConCommand cmd_toggleconsole("toggleconsole", [](const CmdArgs&) { if (host()) host()->toggleConsole(); }, "Show/hide the console.");
ConCommand cmd_screenshot("screenshot", [](const CmdArgs&) { if (host()) host()->screenshot(); }, "Save a PNG screenshot to screenshots/.");
ConCommand cmd_writeconfig("host_writeconfig", [](const CmdArgs&) { if (host()) host()->writeConfig(); }, "Save binds and archived cvars to cfg/config.cfg.");
ConCommand cmd_reloadshaders("r_reloadshaders", [](const CmdArgs&) { if (host()) host()->reloadShaders(); }, "Recompile shaders from disk.");

// Reads lines from stdin on a background thread (dedicated server console).
class StdinReader {
public:
    void start() {
        std::thread([this] {
            std::string line;
            while (std::getline(std::cin, line)) {
                std::lock_guard lock(mutex_);
                lines_.push_back(line);
            }
        }).detach();
    }
    void flushInto() {
        std::lock_guard lock(mutex_);
        for (auto& l : lines_) cmd::append(l);
        lines_.clear();
    }

private:
    std::mutex mutex_;
    std::vector<std::string> lines_;
};

StdinReader& stdinReader() {
    static StdinReader r;
    return r;
}

bool looksLikeNumber(const std::string& s) {
    return s.size() > 1 && s[0] == '-' && (std::isdigit(static_cast<unsigned char>(s[1])) || s[1] == '.');
}

} // namespace

LaunchOptions parseCommandLine(const std::vector<std::string>& args) {
    LaunchOptions o;
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto next = [&](const std::string& fallback) { return i + 1 < args.size() ? args[++i] : fallback; };
        if (a == "-dedicated") o.dedicated = true;
        else if (a == "-fullscreen") o.fullscreen = true;
        else if (a == "-windowed" || a == "-sw") o.fullscreen = false;
        else if (a == "-w" || a == "-width") o.width = std::max(320, std::atoi(next("1280").c_str()));
        else if (a == "-h" || a == "-height") o.height = std::max(240, std::atoi(next("720").c_str()));
        else if (a == "-game") o.gameDir = next(o.gameDir);
        else if (!a.empty() && a[0] == '+') {
            // "+cmd arg arg" up to the next +/- token.
            std::string statement = a.substr(1);
            if (statement == "map") o.hasMapCommand = true;
            while (i + 1 < args.size() && (args[i + 1].empty() || (args[i + 1][0] != '+' &&
                                                                   (args[i + 1][0] != '-' || looksLikeNumber(args[i + 1]))))) {
                const std::string& arg = args[++i];
                statement += arg.find(' ') != std::string::npos ? " \"" + arg + "\"" : " " + arg;
            }
            o.startupCommands += statement + "\n";
        }
    }
    return o;
}

Host::Host() { instance_ = this; }

Host::~Host() {
    shutdownClient();
    if (instance_ == this) instance_ = nullptr;
}

int Host::run(const std::vector<std::string>& args) {
    opts_ = parseCommandLine(args);

    // Filesystem: <exe dir>/<game>, falling back to ./<game> for running from the source tree.
    std::filesystem::path base = SDL_GetBasePath() ? SDL_GetBasePath() : ".";
    std::filesystem::path gameRoot = base / opts_.gameDir;
    if (!std::filesystem::is_directory(gameRoot)) gameRoot = std::filesystem::current_path() / opts_.gameDir;
    if (!std::filesystem::is_directory(gameRoot)) {
        log::error("Game directory '{}' not found (looked next to the executable and in the working dir)", opts_.gameDir);
        return 1;
    }
    fs::addSearchPath(gameRoot, "MOD");
    fs::addSearchPath(gameRoot, "GAME");
    log::info("k engine - game dir: {}", gameRoot.string());

    if (!opts_.dedicated && !initClient(opts_)) return 1;
    if (opts_.dedicated) stdinReader().start();

    mat_vsync.setChangeCallback([this](ConVar& v, const std::string&) {
        if (window_) window_->setVsync(v.getBool());
    });

    cmd::append("exec default.cfg");
    if (fs::exists("cfg/config.cfg")) cmd::append("exec config.cfg");
    if (fs::exists("cfg/autoexec.cfg")) cmd::append("exec autoexec.cfg");
    // With no +map on the command line, load the test map first so +commands run in-game.
    if (!opts_.hasMapCommand && !opts_.dedicated) cmd::append("map devroom");
    cmd::append(opts_.startupCommands);

    double last = platformTime();
    while (!quit_) {
        double now = platformTime();
        double frameTime = std::min(now - last, 0.25);
        last = now;
        frame(frameTime);

        // Frame limiting.
        double target = 0.0;
        if (opts_.dedicated)
            target = server_.active() ? server_.tickInterval() : 0.05;
        else if (fps_max.getFloat() > 0.0f)
            target = 1.0 / fps_max.getFloat();
        double elapsed = platformTime() - now;
        if (target > elapsed) {
            double remaining = target - elapsed;
            // Sleep most of it, spin the last ~1ms for accuracy.
            if (remaining > 0.002) std::this_thread::sleep_for(std::chrono::duration<double>(remaining - 0.001));
            while (platformTime() - now < target) std::this_thread::yield();
        }
    }

    log::info("Shutting down after {} ticks, {} frames", hostTicks_, hostFrames_);
    if (!opts_.dedicated) writeConfig();
    disconnect();
    shutdownClient();
    return 0;
}

bool Host::initClient(const LaunchOptions& opts) {
    window_ = std::make_unique<Window>();
    WindowDesc desc;
    desc.title = "k";
    desc.width = opts.width;
    desc.height = opts.height;
    desc.fullscreen = opts.fullscreen;
    desc.vsync = mat_vsync.getBool();
    if (!window_->create(desc)) return false;

    renderer_ = std::make_unique<Renderer>();
    if (!renderer_->init(*window_)) return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL3_InitForOpenGL(window_->handle(), window_->glContext());
    ImGui_ImplOpenGL3_Init("#version 450");
    imguiReady_ = true;

    console_ = std::make_unique<ConsoleUI>();
    return true;
}

void Host::shutdownClient() {
    console_.reset();
    if (imguiReady_) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        imguiReady_ = false;
    }
    if (renderer_) renderer_->shutdown();
    renderer_.reset();
    window_.reset();
}

void Host::frame(double frameTime) {
    if (window_) pumpEvents();
    if (opts_.dedicated) stdinReader().flushInto();
    cmd::execute();

    float alpha = 1.0f;
    if (server_.active()) {
        const double interval = server_.tickInterval();
        tickAccumulator_ += frameTime * host_timescale.getFloat();
        int ticks = 0;
        while (tickAccumulator_ >= interval) {
            if (++ticks > kMaxTicksPerFrame) {
                tickAccumulator_ = 0.0;
                break;
            }
            if (client_.connected()) client_.createMove(static_cast<float>(interval));
            server_.tick();
            client_.readPackets();
            tickAccumulator_ -= interval;
            ++hostTicks_;
            if (host_maxticks.getInt() > 0 && hostTicks_ >= static_cast<uint64_t>(host_maxticks.getInt())) {
                log::info("host_maxticks reached");
                quit_ = true;
                break;
            }
        }
        alpha = static_cast<float>(tickAccumulator_ / interval);
    }

    if (window_) {
        client_.frameMouse(mouseDx_, mouseDy_);
        mouseDx_ = mouseDy_ = 0;
        renderFrame(alpha);
    }

    ++hostFrames_;
    ++fpsFrames_;
    fpsTimer_ += frameTime;
    if (fpsTimer_ >= 0.5) {
        fps_ = static_cast<float>(fpsFrames_ / fpsTimer_);
        fpsFrames_ = 0;
        fpsTimer_ = 0;
    }
}

void Host::pumpEvents() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        const bool consoleOpen = console_ && console_->open();

        // The console key and escape are handled before ImGui so they never type into the console.
        if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
            if (e.key.scancode == SDL_SCANCODE_GRAVE || e.key.scancode == SDL_SCANCODE_ESCAPE) {
                if (e.key.scancode == SDL_SCANCODE_ESCAPE) {
                    // Escape is reserved (no menu yet): it always toggles the console.
                    toggleConsole();
                } else {
                    // ` goes through the bind system so it can be rebound; default.cfg binds it.
                    bool down = true;
                    auto key = keys::fromEvent(e, down);
                    auto* b = bindings::find(key);
                    if (b && *b == "toggleconsole") toggleConsole();
                    else if (!consoleOpen) bindings::onKey(key, true);
                }
                continue;
            }
        }
        if (e.type == SDL_EVENT_TEXT_INPUT && e.text.text && std::string_view(e.text.text) == "`") continue;

        if (imguiReady_) ImGui_ImplSDL3_ProcessEvent(&e);

        switch (e.type) {
            case SDL_EVENT_QUIT: quit_ = true; break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                windowFocused_ = false;
                bindings::releaseAll();
                break;
            case SDL_EVENT_WINDOW_FOCUS_GAINED: windowFocused_ = true; break;
            case SDL_EVENT_MOUSE_MOTION:
                if (window_->relativeMouse()) {
                    mouseDx_ += e.motion.xrel;
                    mouseDy_ += e.motion.yrel;
                }
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                if (!consoleOpen && e.wheel.y != 0) {
                    const char* key = e.wheel.y > 0 ? "mwheelup" : "mwheeldown";
                    bindings::onKey(key, true);
                    bindings::onKey(key, false);
                }
                break;
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                bool down = false;
                auto key = keys::fromEvent(e, down);
                if (key.empty()) break;
                // Releases always go through so held buttons don't get stuck when the console opens.
                if (!consoleOpen || !down) bindings::onKey(key, down);
                break;
            }
            default: break;
        }
    }
    updateMouseCapture();
}

void Host::updateMouseCapture() {
    bool want = windowFocused_ && !(console_ && console_->open()) && client_.active();
    if (want != window_->relativeMouse()) window_->setRelativeMouse(want);
}

void Host::renderFrame(float alpha) {
    renderer_->beginFrame();
    if (client_.active()) renderer_->renderView(client_.view(alpha));

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    drawOverlay();
    if (console_) console_->draw();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    renderer_->endFrame();
    window_->swap();
}

void Host::drawOverlay() {
    if (!cl_showfps.getBool() && !cl_showpos.getBool()) return;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 10, 10), ImGuiCond_Always, ImVec2(1, 0));
    ImGui::SetNextWindowBgAlpha(0.35f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin("##overlay", nullptr, flags)) {
        if (cl_showfps.getBool()) {
            ImGui::Text("%.0f fps (%.2f ms)", fps_, fps_ > 0 ? 1000.0f / fps_ : 0.0f);
            const auto& s = renderer_->stats();
            ImGui::Text("%zu draws, %zu tris", s.drawCalls, s.triangles);
        }
        if (cl_showpos.getBool()) {
            const auto& p = client_.latestState();
            auto v = client_.view(1.0f);
            ImGui::Text("pos: %.1f %.1f %.1f", p.origin.x, p.origin.y, p.origin.z);
            ImGui::Text("ang: %.1f %.1f %.1f", v.angles.pitch, v.angles.yaw, v.angles.roll);
            ImGui::Text("vel: %.1f", glm::length(p.velocity));
            ImGui::Text("tick: %u", client_.serverTick());
        }
    }
    ImGui::End();
}

void Host::startMap(const std::string& name) {
    disconnect();
    auto [clientEnd, serverEnd] = net::LoopbackChannel::createPair();
    if (!server_.start(name, std::move(serverEnd))) return;
    tickAccumulator_ = 0.0;
    if (!opts_.dedicated) {
        renderer_->loadMap(name);
        client_.connect(std::move(clientEnd), player_name.getString());
    }
}

void Host::disconnect() {
    bindings::releaseAll();
    client_.disconnect();
    server_.stop();
    if (renderer_) renderer_->unloadMap();
}

void Host::toggleConsole() {
    if (!console_) return;
    console_->toggle();
    if (console_->open()) bindings::releaseAll();
}

void Host::screenshot() {
    if (renderer_)
        renderer_->requestScreenshot();
    else
        log::warn("screenshot: no renderer in dedicated mode");
}

void Host::reloadShaders() {
    if (renderer_ && renderer_->reloadShaders()) log::info("Shaders reloaded");
}

void Host::writeConfig() {
    std::string out = "// Generated by the engine. Put your own commands in autoexec.cfg instead.\n";
    out += bindings::serialize();
    std::vector<std::string> lines;
    console::forEach([&](ConCommandBase& c) {
        if (c.isCommand() || !c.hasFlag(FCVAR_ARCHIVE) || c.hasFlag(FCVAR_CHEAT)) return;
        lines.push_back(c.name() + " \"" + static_cast<ConVar&>(c).getString() + "\"");
    });
    std::sort(lines.begin(), lines.end());
    for (auto& l : lines) out += l + "\n";
    if (fs::writeText("cfg/config.cfg", out, "MOD")) log::debug("Wrote cfg/config.cfg");
}

} // namespace k
