add_executable(k main.cpp)
target_link_libraries(k PRIVATE k_engine)
k_set_warnings(k)

# Copy the game content directory next to the executable after every build.
add_custom_command(TARGET k POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/base $<TARGET_FILE_DIR:k>/base
  COMMENT "Copying game content")
