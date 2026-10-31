# k_core: platform-independent foundation (no SDL/GL). Unit tests link only this.
add_library(k_core STATIC
  core/log.cpp
  core/cvar.cpp
  core/cmdbuffer.cpp
  core/filesystem.cpp
  core/png.cpp
  shared/movement.cpp
)
target_include_directories(k_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(k_core PUBLIC glm_headers)
k_set_warnings(k_core)

# k_engine: everything else (window, renderer, client, server, host).
add_library(k_engine STATIC
  platform/keys.cpp
  platform/window.cpp
  render/shader.cpp
  render/mesh.cpp
  render/renderer.cpp
  render/devroom.cpp
  client/bindings.cpp
  client/kbutton.cpp
  client/client.cpp
  server/server.cpp
  ui/console_ui.cpp
  host/host.cpp
)
target_include_directories(k_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(k_engine PUBLIC k_core SDL3::SDL3-static glad imgui)
k_set_warnings(k_engine)
