add_executable(k main.cpp)
target_link_libraries(k PRIVATE k_engine)
k_set_warnings(k)

# Copy the game content directory next to the executable after every build.
add_custom_command(TARGET k POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/base $<TARGET_FILE_DIR:k>/base
  COMMENT "Copying game content")

# Compile every map in base/maps/src with kbsp into the build's game directory.
file(GLOB K_MAP_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/base/maps/src/*.map)
set(K_COMPILED_MAPS)
foreach(map_src ${K_MAP_SOURCES})
  get_filename_component(map_name ${map_src} NAME_WE)
  set(map_out ${CMAKE_BINARY_DIR}/bin/base/maps/${map_name}.kbsp)
  add_custom_command(
    OUTPUT ${map_out}
    COMMAND kbsp -game ${PROJECT_SOURCE_DIR}/base -o ${map_out} ${map_src}
    DEPENDS kbsp ${map_src}
    COMMENT "Compiling map ${map_name}"
    VERBATIM)
  list(APPEND K_COMPILED_MAPS ${map_out})
endforeach()
add_custom_target(maps ALL DEPENDS ${K_COMPILED_MAPS})
add_dependencies(k maps)
