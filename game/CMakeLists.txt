# The game code: entities, I/O, game rules. Talks to the engine only through game_server.h
# and the shared/ code.
add_library(k_game STATIC
  entity.cpp
  game_world.cpp
  entities/logic.cpp
  entities/triggers.cpp
  entities/movers.cpp
  entities/misc.cpp
)
target_include_directories(k_game PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(k_game PUBLIC k_assets)
k_set_warnings(k_game)
