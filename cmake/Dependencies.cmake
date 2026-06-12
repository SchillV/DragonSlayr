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

FetchContent_MakeAvailable(SDL3 doctest)
