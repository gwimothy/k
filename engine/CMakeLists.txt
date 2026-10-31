# k_core: platform-independent foundation (no SDL/GL). Unit tests link only this.
add_library(k_core STATIC
  core/log.cpp
  core/cvar.cpp
  core/cmdbuffer.cpp
  core/filesystem.cpp
  core/png.cpp
)
target_include_directories(k_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(k_core PUBLIC glm_headers)
k_set_warnings(k_core)

# k_net: UDP sockets, the network channel, and protocol encoding.
add_library(k_net STATIC
  net/socket.cpp
  net/udp_channel.cpp
  shared/protocol.cpp
)
target_include_directories(k_net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(k_net PUBLIC k_core)
if(WIN32)
  target_link_libraries(k_net PUBLIC ws2_32)
endif()
k_set_warnings(k_net)

# k_assets: asset code that needs no GPU (mesh building, glTF parsing). Tests link this.
add_library(k_assets STATIC
  core/keyvalues.cpp
  shared/bspfile.cpp
  shared/bsp_trace.cpp
  shared/gamemovement.cpp
  render/mesh_data.cpp
  render/gltf_loader.cpp
)
target_include_directories(k_assets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(k_assets PUBLIC k_core k_net PRIVATE cgltf_headers)
k_set_warnings(k_assets)

# k_audio: sound scripts and playback (miniaudio). No window or GPU needed.
add_library(k_audio STATIC audio/audio.cpp)
target_include_directories(k_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
find_package(Threads REQUIRED)
target_link_libraries(k_audio PUBLIC k_assets PRIVATE miniaudio_headers Threads::Threads ${CMAKE_DL_LIBS})
if(UNIX AND NOT APPLE)
  target_link_libraries(k_audio PRIVATE m)
endif()
k_set_warnings(k_audio)

# k_engine: everything else (window, renderer, client, server, host).
add_library(k_engine STATIC
  platform/keys.cpp
  platform/window.cpp
  render/shader.cpp
  render/mesh.cpp
  render/renderer.cpp
  render/world_renderer.cpp
  render/texture.cpp
  render/material.cpp
  render/model.cpp
  render/shader_library.cpp
  render/render_target.cpp
  render/debug_draw.cpp
  render/third_party_impl.cpp
  client/bindings.cpp
  client/kbutton.cpp
  client/client.cpp
  server/server.cpp
  ui/console_ui.cpp
  host/host.cpp
)
target_include_directories(k_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(k_engine PUBLIC k_assets k_game k_audio SDL3::SDL3-static glad imgui PRIVATE stb_headers)
k_set_warnings(k_engine)
