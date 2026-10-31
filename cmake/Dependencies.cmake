include(FetchContent)
set(FETCHCONTENT_QUIET ON)

# ---- SDL3: window, input, GL context -------------------------------------------
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG release-3.2.8
  GIT_SHALLOW TRUE)

# ---- Header-only / source-only libraries ----------------------------------------
# SOURCE_SUBDIR points at a folder that does not exist so FetchContent only downloads
# the code; we define our own small targets below.
FetchContent_Declare(glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG 1.0.1
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR _none)
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.91.9b
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR _none)
FetchContent_Declare(doctest
  GIT_REPOSITORY https://github.com/doctest/doctest.git
  GIT_TAG v2.4.11
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR _none)

FetchContent_MakeAvailable(SDL3 glm imgui doctest)

add_library(glm_headers INTERFACE)
target_include_directories(glm_headers SYSTEM INTERFACE ${glm_SOURCE_DIR})
target_compile_definitions(glm_headers INTERFACE GLM_FORCE_XYZW_ONLY)

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL3::SDL3-static)

add_library(doctest_headers INTERFACE)
target_include_directories(doctest_headers SYSTEM INTERFACE ${doctest_SOURCE_DIR})
