# k

A 3D game engine in the spirit of Valve's **Source 1**: compiled BSP maps with precomputed
visibility and baked lighting, an authoritative client/server split (even in single-player),
entity input/output wiring, and a console + cvar system driving everything.

> Status: **Phase 1 (Foundation) done.** You can fly around a test room with noclip, use the
> developer console, bind keys and save config. See [docs/ROADMAP.md](docs/ROADMAP.md).

![devroom](docs/images/devroom.png)

## Building

Needs CMake 3.24+, a C++20 compiler (GCC 13+, Clang 16+, MSVC 2022) and OpenGL 4.5.
Dependencies (SDL3, glm, Dear ImGui, doctest) are downloaded automatically.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
./build/bin/k
```

On Linux you need the X11/GL development headers first:

```sh
sudo apt install libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev \
  libxfixes-dev libxss-dev libgl-dev libegl-dev libasound2-dev libpulse-dev
```

macOS is not supported yet (Apple stops at OpenGL 4.1; a Metal/Vulkan backend is on the roadmap).

## Running

| Action | Default key |
|---|---|
| Move | W A S D |
| Fly up / down (noclip) | Space / Ctrl |
| Go faster | Shift |
| Console | `` ` `` or Esc |
| Screenshot | F5 |
| FPS / position overlay | F9 / F10 |

Command-line options work like Source:

```sh
./build/bin/k -w 1920 -h 1080 -fullscreen +sv_cheats 1 +map devroom
./build/bin/k -dedicated +map devroom          # headless server, reads commands from stdin
```

Handy console commands: `cvarlist`, `find <text>`, `help <name>`, `bind <key> <cmd>`,
`alias`, `exec <cfg>`, `toggle <cvar>`, `screenshot`, `r_reloadshaders`, `quit`.

Your settings are written to `base/cfg/config.cfg` on exit. Put your own startup commands
in `base/cfg/autoexec.cfg`.

## Testing

```sh
scripts/smoke_test.sh build
```

Runs the unit tests, a dedicated-server run, and (on Linux, via Xvfb if there's no display) a
full client run with scripted input that saves screenshots. CI runs the same script on every
push for Linux and Windows.

## Layout

```
engine/
  core/      logging, cvars & commands, command buffer, filesystem, PNG writer
  net/       message serialization, loopback channel (UDP later)
  shared/    code used by both client and server: math, UserCmd, protocol, movement
  server/    authoritative game simulation
  client/    input -> UserCmds, key bindings, snapshot interpolation
  platform/  SDL3 window and key names
  render/    OpenGL 4.5 renderer
  ui/        developer console (Dear ImGui)
  host/      main loop, startup, config
launcher/    executable entry point
base/        game content: cfg/, shaders/ (copied next to the binary on build)
tests/       unit tests (doctest)
docs/        roadmap and architecture
```
