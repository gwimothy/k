# The compiler as a library (so tests can use it) plus the command-line tool.
add_library(kbsp_lib STATIC
  winding.cpp
  map_file.cpp
  brushes.cpp
  tree.cpp
  portals.cpp
  faces.cpp
  vis.cpp
  light.cpp
)
target_include_directories(kbsp_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
find_package(Threads REQUIRED)
target_link_libraries(kbsp_lib PUBLIC k_assets Threads::Threads)
k_set_warnings(kbsp_lib)

add_executable(kbsp main.cpp)
target_link_libraries(kbsp PRIVATE kbsp_lib)
k_set_warnings(kbsp)
