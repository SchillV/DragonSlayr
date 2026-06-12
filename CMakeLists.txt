cmake_minimum_required(VERSION 3.28)
project(DragonSlayr VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Dependencies)

add_library(ds_warnings INTERFACE)
if(NOT MSVC)
  target_compile_options(ds_warnings INTERFACE -Wall -Wextra)
else()
  target_compile_options(ds_warnings INTERFACE /W4)
endif()

add_executable(dragonslayr
  src/main.cpp
  src/core/log.cpp
  src/game/app.cpp
)
target_include_directories(dragonslayr PRIVATE src)
target_link_libraries(dragonslayr PRIVATE ds_warnings SDL3::SDL3)

enable_testing()

add_executable(ds_tests
  tests/test_smoke.cpp
  src/core/log.cpp
)
target_include_directories(ds_tests PRIVATE src)
target_link_libraries(ds_tests PRIVATE ds_warnings doctest::doctest)

add_test(NAME unit COMMAND ds_tests)
add_test(NAME smoke_headless COMMAND dragonslayr --headless --ticks 600 --seed 1)
