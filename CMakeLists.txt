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

add_executable(dragonslayr
  src/main.cpp
  src/core/log.cpp
  src/game/app.cpp
  src/platform/platform.cpp
  src/render/texture_load.cpp
  src/render/gpu_device.cpp
  src/render/gpu_renderer.cpp
  src/render/debug_ui.cpp
)
target_include_directories(dragonslayr PRIVATE src)
target_link_libraries(dragonslayr PRIVATE ds_warnings ds_glm_config SDL3::SDL3 ds_imgui ds_stb)
add_dependencies(dragonslayr shaders)

enable_testing()

add_executable(ds_tests
  tests/test_smoke.cpp
  tests/test_texture.cpp
  src/core/log.cpp
  src/render/texture_load.cpp
)
target_include_directories(ds_tests PRIVATE src)
target_link_libraries(ds_tests PRIVATE ds_warnings ds_glm_config ds_stb doctest::doctest)

add_test(NAME unit COMMAND ds_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_test(NAME smoke_headless COMMAND dragonslayr --headless --ticks 600 --seed 1)
