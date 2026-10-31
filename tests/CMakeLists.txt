add_executable(k_tests
  main.cpp
  test_cvar.cpp
  test_cmdbuffer.cpp
  test_filesystem.cpp
  test_net.cpp
  test_math.cpp
  test_png.cpp
)
target_link_libraries(k_tests PRIVATE k_core doctest_headers)
k_set_warnings(k_tests)
add_test(NAME k_tests COMMAND k_tests)
