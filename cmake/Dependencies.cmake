# All third-party dependencies, pinned. Everything is fetched at configure time;
# no system packages are assumed beyond a C++20 toolchain.

include(FetchContent)

# --- SDL3 (platform + GPU API) ------------------------------------------------
# Headless CI containers have no display and often partial X11 headers, which SDL
# treats as a hard error. The 'headless' preset turns desktop video off entirely;
# the dummy video driver still works for --headless runs.
option(DS_ENABLE_DESKTOP_VIDEO "Build SDL desktop video backends (X11/Wayland)" ON)
if(NOT DS_ENABLE_DESKTOP_VIDEO)
  set(SDL_X11 OFF CACHE BOOL "" FORCE)
  set(SDL_WAYLAND OFF CACHE BOOL "" FORCE)
  set(SDL_UNIX_CONSOLE_BUILD ON CACHE BOOL "" FORCE) # SDL guards against accidental video-less builds
endif()

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG release-3.4.10
  GIT_SHALLOW TRUE
  SYSTEM
  EXCLUDE_FROM_ALL
)

# --- doctest (unit tests) -----------------------------------------------------
set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_Declare(doctest
  GIT_REPOSITORY https://github.com/doctest/doctest.git
  GIT_TAG v2.5.2
  GIT_SHALLOW TRUE
  SYSTEM
  EXCLUDE_FROM_ALL
)

# --- EnTT (ECS) -----------------------------------------------------------------
FetchContent_Declare(EnTT
  GIT_REPOSITORY https://github.com/skypjack/entt.git
  GIT_TAG v3.16.0
  GIT_SHALLOW TRUE
  SYSTEM
  EXCLUDE_FROM_ALL
)

# --- GLM (math) -----------------------------------------------------------------
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG 1.0.3
  GIT_SHALLOW TRUE
  SYSTEM
  EXCLUDE_FROM_ALL
)

# --- nlohmann/json (data files, saves, telemetry) -------------------------------
# The release tarball is tiny; the git repo is enormous.
FetchContent_Declare(nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  SYSTEM
  EXCLUDE_FROM_ALL
)

# --- stb_image (texture loading) — single pinned header -------------------------
FetchContent_Declare(stb_image
  URL https://raw.githubusercontent.com/nothings/stb/31c1ad37456438565541f4919958214b6e762fb4/stb_image.h
  DOWNLOAD_NO_EXTRACT TRUE
)

# --- Dear ImGui (debug UI only) — no upstream CMake, populate + own target ------
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.92.8
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR _no_cmake_here_ # populate only; targets defined below
)

# --- miniaudio (audio) — populate only, impl TU lives in src/platform -----------
FetchContent_Declare(miniaudio
  GIT_REPOSITORY https://github.com/mackron/miniaudio.git
  GIT_TAG 0.11.25
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR _no_cmake_here_
)

set(_ds_deps SDL3 doctest EnTT glm nlohmann_json stb_image imgui miniaudio)

# --- glslang (build-time GLSL -> SPIR-V compiler) --------------------------------
option(DS_USE_PREBUILT_SHADERS "Use committed .spv artifacts from shaders/compiled/ instead of building glslang" OFF)
if(NOT DS_USE_PREBUILT_SHADERS)
  set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
  set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
  set(ENABLE_OPT OFF CACHE BOOL "" FORCE)        # drops the SPIRV-Tools dependency
  set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
  set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
  set(ENABLE_GLSLANG_BINARIES ON CACHE BOOL "" FORCE)
  set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(glslang
    GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
    GIT_TAG 16.3.0
    GIT_SHALLOW TRUE
    SYSTEM
    EXCLUDE_FROM_ALL
  )
  list(APPEND _ds_deps glslang)
endif()

FetchContent_MakeAvailable(${_ds_deps})

# stb: header-only interface target
add_library(ds_stb INTERFACE)
target_include_directories(ds_stb SYSTEM INTERFACE ${stb_image_SOURCE_DIR})

# miniaudio: header-only interface target (implementation TU in src/platform)
add_library(ds_miniaudio INTERFACE)
target_include_directories(ds_miniaudio SYSTEM INTERFACE ${miniaudio_SOURCE_DIR})

# Dear ImGui: core + the SDL3/SDLGPU3 backends we use
add_library(ds_imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3.cpp
)
target_include_directories(ds_imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(ds_imgui PUBLIC SDL3::SDL3)
