cmake_minimum_required(VERSION 3.24)
project(k VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

option(K_BUILD_TESTS "Build unit tests" ON)

# Put every executable in one folder so the content directory sits next to them.
# The empty generator expression stops multi-config generators (Visual Studio) from
# appending a per-config subfolder.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin$<0:>)

include(cmake/Dependencies.cmake)
include(cmake/Warnings.cmake)

add_subdirectory(third_party/glad)
add_subdirectory(engine)
add_subdirectory(tools/kbsp)
add_subdirectory(launcher)

if(K_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
