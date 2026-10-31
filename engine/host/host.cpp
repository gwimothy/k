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
#include "shared/bspfile.h"
#include "ui/console_ui.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <poll.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstdlib>
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
std::vector<std::string> completeMapName(std::string_view partial) {
    std::vector<std::string> out;
    for (auto& sp : fs::searchPaths()) {
        std::error_code ec;
        for (auto& entry : std::filesystem::directory_iterator(sp.root / "maps", ec)) {
            if (entry.path().extension() != ".kbsp") continue;
            std::string n = entry.path().stem().string();
            if (n.starts_with(partial) && std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

ConCommand cmd_maps(
    "maps",
    [](const CmdArgs& a) {
        for (auto& m : completeMapName(a.arg(1))) log::info("{}", m);
    },
    "List available maps.");

ConCommand cmd_map(
    "map",
    [](const CmdArgs& a) {
        if (a.argc() < 2) {
            log::info("Usage: map <name>");
            return;
        }
        if (host()) host()->startMap(a.arg(1));
    },
    "Start a local server on a map.", FCVAR_NONE, completeMapName);
ConCommand cmd_disconnect("disconnect", [](const CmdArgs&) { if (host()) host()->disconnect(); }, "Leave the current game.");
ConCommand cmd_toggleconsole("toggleconsole", [](const CmdArgs&) { if (host()) host()->toggleConsole(); }, "Show/hide the console.");
ConCommand cmd_screenshot("screenshot", [](const CmdArgs&) { if (host()) host()->screenshot(); }, "Save a PNG screenshot to screenshots/.");
ConCommand cmd_writeconfig("host_writeconfig", [](const CmdArgs&) { if (host()) host()->writeConfig(); }, "Save binds and archived cvars to cfg/config.cfg.");
ConCommand cmd_reloadshaders("r_reloadshaders", [](const CmdArgs&) { if (host()) host()->reloadShaders(); }, "Recompile shaders from disk.");
ConCommand cmd_reloadmaterials("mat_reloadall", [](const CmdArgs&) { if (host()) host()->reloadMaterials(); }, "Reload all textures and materials from disk.");
ConCommand cmd_setpos(
    "setpos",
    [](const CmdArgs& a) {
        if (a.argc() < 4) {
            log::info("Usage: setpos x y z");
            return;
        }
        if (host()) host()->setPos({std::stof(a.arg(1)), std::stof(a.arg(2)), std::stof(a.arg(3))});
    },
    "Move the player to a position (feet).", FCVAR_CHEAT);
ConCommand cmd_setang(
    "setang",
    [](const CmdArgs& a) {
        if (a.argc() < 3) {
            log::info("Usage: setang pitch yaw [roll]");
            return;
        }
        if (host()) host()->setAng({std::stof(a.arg(1)), std::stof(a.arg(2)), a.argc() > 3 ? std::stof(a.arg(3)) : 0.0f});
    },
    "Set the view angles.", FCVAR_CHEAT);
ConCommand cmd_getpos(
    "getpos",
    [](const CmdArgs&) {
        if (host()) host()->printPos();
    },
    "Print setpos/setang commands for the current position.");
ConCommand cmd_ent_fire(
    "ent_fire",
    [](const CmdArgs& a) {
        if (a.argc() < 3) {
            log::info("Usage: ent_fire <target> <input> [param]");
            return;
        }
        if (host()) host()->forwardToServer("ent_fire " + a.argString());
    },
    "Send an input to entities: ent_fire <target> <input> [param].", FCVAR_CHEAT);
ConCommand cmd_noclip(
    "noclip",
    [](const CmdArgs&) {
        if (host()) host()->forwardToServer("noclip");
    },
    "Toggle flying through walls (needs sv_cheats 1).");
ConVar crosshair("crosshair", "1", FCVAR_ARCHIVE, "Draw a crosshair.");

// Non-blocking stdin for the dedicated server console. Deliberately avoids C stdio /
// iostreams: a thread blocked in std::getline holds the stdin lock, and glibc then
// deadlocks flushing streams at exit.
class StdinReader {
public:
    void start() {
#ifdef _WIN32
        std::thread([this] {
            HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
            char buf[512];
            DWORD n = 0;
            while (ReadFile(in, buf, sizeof(buf), &n, nullptr) && n > 0) {
                std::lock_guard lock(mutex_);
                pending_.append(buf, n);
            }
        }).detach();
#endif
    }

    void flushInto() {
#ifndef _WIN32
        pollfd pfd{0, POLLIN, 0};
        while (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
            char buf[512];
            ssize_t n = read(0, buf, sizeof(buf));
            if (n <= 0) break;
            pending_.append(buf, static_cast<size_t>(n));
        }
#endif
        std::lock_guard lock(mutex_);
        size_t nl;
        while ((nl = pending_.find('\n')) != std::string::npos) {
            cmd::append(pending_.substr(0, nl));
            pending_.erase(0, nl + 1);
        }
    }

private:
    std::mutex mutex_;
    std::string pending_;
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
        client_.frameKeyboardLook(static_cast<float>(frameTime));
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
    if (client_.active()) {
        static std::vector<RenderEntity> entities;
        client_.renderEntities(alpha, entities);
        renderer_->renderView(client_.view(alpha), entities);
    }

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
    ImGuiIO& io = ImGui::GetIO();
    if (crosshair.getBool() && client_.active() && !(console_ && console_->open())) {
        auto* dl = ImGui::GetBackgroundDrawList();
        ImVec2 c(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
        const ImU32 col = IM_COL32(255, 220, 80, 230);
        dl->AddLine(ImVec2(c.x - 8, c.y), ImVec2(c.x - 3, c.y), col, 2.0f);
        dl->AddLine(ImVec2(c.x + 3, c.y), ImVec2(c.x + 8, c.y), col, 2.0f);
        dl->AddLine(ImVec2(c.x, c.y - 8), ImVec2(c.x, c.y - 3), col, 2.0f);
        dl->AddLine(ImVec2(c.x, c.y + 3), ImVec2(c.x, c.y + 8), col, 2.0f);
    }
    // Center-screen messages from the game (point_message ShowHudMessage), fading out.
    double age = platformTime() - client_.centerMessageTime();
    if (client_.active() && !client_.centerMessage().empty() && age < 4.0) {
        float alpha = age < 3.0 ? 1.0f : static_cast<float>(4.0 - age);
        const char* text = client_.centerMessage().c_str();
        ImVec2 size = ImGui::CalcTextSize(text);
        float scale = 1.6f;
        ImVec2 pos(io.DisplaySize.x * 0.5f - size.x * scale * 0.5f, io.DisplaySize.y * 0.32f);
        auto* dl = ImGui::GetForegroundDrawList();
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * scale, ImVec2(pos.x + 2, pos.y + 2),
                    IM_COL32(0, 0, 0, static_cast<int>(200 * alpha)), text);
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * scale, pos,
                    IM_COL32(255, 235, 170, static_cast<int>(255 * alpha)), text);
    }
    if (!cl_showfps.getBool() && !cl_showpos.getBool()) return;
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
            ImGui::Text("%zu textures, %zu materials, %zu models", renderer_->textures().count(),
                        renderer_->materials().count(), renderer_->models().count());
        }
        if (cl_showpos.getBool()) {
            const auto& p = client_.latestState();
            auto v = client_.view(1.0f);
            ImGui::Text("pos: %.1f %.1f %.1f", p.origin.x, p.origin.y, p.origin.z);
            if (auto* ws = renderer_->worldStats())
                ImGui::Text("leaf %d: %zu leaves, %zu faces visible", ws->cameraLeaf, ws->visibleLeaves, ws->visibleFaces);
            ImGui::Text("ang: %.1f %.1f %.1f", v.angles.pitch, v.angles.yaw, v.angles.roll);
            ImGui::Text("vel: %.1f (horizontal %.1f)%s", glm::length(p.velocity),
                        glm::length(Vec2(p.velocity.x, p.velocity.y)), (p.flags & 1u) ? " ground" : "");
            ImGui::Text("tick: %u", client_.serverTick());
        }
    }
    ImGui::End();
}

void Host::startMap(const std::string& name) {
    auto bytes = fs::readBinary("maps/" + name + ".kbsp");
    if (!bytes) {
        log::error("map: couldn't find maps/{}.kbsp", name);
        return;
    }
    std::string error;
    auto parsed = bsp::read(*bytes, &error);
    if (!parsed) {
        log::error("map: maps/{}.kbsp is invalid: {}", name, error);
        return;
    }
    auto map = std::make_shared<const bsp::BspData>(std::move(*parsed));

    disconnect();
    auto [clientEnd, serverEnd] = net::LoopbackChannel::createPair();
    if (!server_.start(name, map, std::move(serverEnd))) return;
    tickAccumulator_ = 0.0;
    if (!opts_.dedicated) {
        renderer_->loadMap(map);
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

void Host::setPos(const Vec3& origin) {
    if (server_.active()) server_.setPlayerOrigin(origin);
}

void Host::setAng(const QAngle& angles) { client_.setViewAngles(angles); }

void Host::forwardToServer(const std::string& text) {
    if (!client_.connected()) {
        log::warn("Not connected to a server");
        return;
    }
    client_.sendStringCmd(text);
}

void Host::printPos() {
    const auto& p = server_.player();
    const auto& a = client_.viewAngles();
    log::info("setpos {:.2f} {:.2f} {:.2f}; setang {:.2f} {:.2f} {:.2f}", p.origin.x, p.origin.y, p.origin.z, a.pitch,
              a.yaw, a.roll);
}

void Host::reloadMaterials() {
    if (renderer_) renderer_->reloadMaterials();
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
