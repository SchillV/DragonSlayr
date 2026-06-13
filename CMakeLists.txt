cmake_minimum_required(VERSION 3.28)
project(DragonSlayr VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Dependencies)
include(Shaders)

add_library(ds_warnings INTERFACE)
if(NOT MSVC)
  target_compile_options(ds_warnings INTERFACE -Wall -Wextra)
else()
  target_compile_options(ds_warnings INTERFACE /W4)
endif()

# SDL_GPU clip space: y-up NDC, z in [0,1] — glm must match.
add_library(ds_glm_config INTERFACE)
target_compile_definitions(ds_glm_config INTERFACE GLM_FORCE_DEPTH_ZERO_TO_ONE)
target_link_libraries(ds_glm_config INTERFACE glm::glm)

# --- ds_sim: headless game logic. STRUCTURALLY SDL-free — SDL is not on this
# target's include path, so sim code that includes SDL fails to compile.
add_library(ds_sim STATIC
  src/core/log.cpp
  src/core/cvar.cpp
  src/sim/tilemap.cpp
  src/sim/dungeon_gen.cpp
  src/sim/collision.cpp
  src/sim/player.cpp
  src/sim/world.cpp
  src/sim/bot.cpp
  src/sim/content.cpp
  src/sim/pathfinding.cpp
  src/sim/enemy_ai.cpp
  src/sim/combat.cpp
  src/sim/telemetry.cpp
)
target_include_directories(ds_sim PUBLIC src)
target_link_libraries(ds_sim
  PUBLIC ds_glm_config EnTT::EnTT nlohmann_json::nlohmann_json
  PRIVATE ds_warnings
)

# --- the game executable: platform + render + game glue on top of ds_sim ------
add_executable(dragonslayr
  src/main.cpp
  src/game/app.cpp
  src/game/hud.cpp
  src/platform/platform.cpp
  src/platform/input.cpp
  src/platform/audio.cpp
  src/render/texture_load.cpp
  src/render/dungeon_mesh.cpp
  src/render/gpu_device.cpp
  src/render/gpu_renderer.cpp
  src/render/debug_ui.cpp
)
target_link_libraries(dragonslayr PRIVATE ds_warnings ds_sim SDL3::SDL3 ds_imgui ds_stb ds_miniaudio)
if(UNIX AND NOT APPLE)
  target_link_libraries(dragonslayr PRIVATE ${CMAKE_DL_LIBS} m pthread) # miniaudio backends
endif()
add_dependencies(dragonslayr shaders)

# Stage compiled shaders into a shaders/ folder right next to the executable so
# find_resource_dir("shaders") locates them regardless of generator layout
# (Ninja keeps the exe at build/<preset>/, Visual Studio at build/<cfg>/).
add_custom_command(TARGET dragonslayr POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${DS_SHADER_OUT_DIR} $<TARGET_FILE_DIR:dragonslayr>/shaders
  COMMENT "Staging shaders next to the executable"
  VERBATIM)

enable_testing()

# Tests link the sim plus the few render-side files that are pure CPU
# (texture decode, mesh building) — still no SDL anywhere.
add_executable(ds_tests
  tests/test_smoke.cpp
  tests/test_texture.cpp
  tests/test_dungeon_gen.cpp
  tests/test_mesh.cpp
  tests/test_collision.cpp
  tests/test_cvar.cpp
  tests/test_pathfinding.cpp
  tests/test_content.cpp
  tests/test_telemetry.cpp
  tests/test_combat.cpp
  tests/test_spawn.cpp
  src/render/texture_load.cpp
  src/render/dungeon_mesh.cpp
)
target_link_libraries(ds_tests PRIVATE ds_warnings ds_sim ds_stb doctest::doctest)

add_test(NAME unit COMMAND ds_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_test(NAME smoke_headless
         COMMAND dragonslayr --headless --ticks 1800 --seed 42 --bot walk_attack
                 --telemetry-dir ${CMAKE_BINARY_DIR}/telemetry
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_test(NAME smoke_print_map COMMAND dragonslayr --print-map --seed 7)
