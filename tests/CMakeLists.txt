add_executable(k_tests
  main.cpp
  test_cvar.cpp
  test_cmdbuffer.cpp
  test_filesystem.cpp
  test_net.cpp
  test_math.cpp
  test_png.cpp
  test_keyvalues.cpp
  test_assets.cpp
  test_bsp.cpp
  test_movement.cpp
  test_game.cpp
  test_physics.cpp
  test_audio.cpp
)
target_link_libraries(k_tests PRIVATE k_assets k_game k_audio kbsp_lib doctest_headers)
target_compile_definitions(k_tests PRIVATE K_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
k_set_warnings(k_tests)
add_test(NAME k_tests COMMAND k_tests)
