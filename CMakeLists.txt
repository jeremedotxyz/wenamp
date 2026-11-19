cmake_minimum_required(VERSION 3.16)
project(wenamp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(WENAMP_BUILD_GUI   "Build the SDL2 window front-end (needs SDL2)" ON)
option(WENAMP_BUILD_TESTS "Build the test suite"                          ON)

find_package(Threads REQUIRED)

# ── Core library: zero third-party runtime deps beyond vendored miniaudio ──
add_library(wenamp_miniaudio STATIC src/miniaudio_impl.cpp)
target_include_directories(wenamp_miniaudio PUBLIC third_party)
target_link_libraries(wenamp_miniaudio PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
if(NOT MSVC)
  target_link_libraries(wenamp_miniaudio PUBLIC m)
  target_compile_options(wenamp_miniaudio PRIVATE -w)
endif()
if(APPLE)
  target_link_libraries(wenamp_miniaudio PUBLIC
    "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox")
endif()

add_library(wenamp_core STATIC
  src/fft.cpp
  src/spectrum.cpp
  src/eq.cpp
  src/tags.cpp
  src/playlist.cpp
  src/player.cpp
  src/wav.cpp
  src/canvas.cpp
  src/font.cpp
  src/theme.cpp
  src/ui.cpp)
target_include_directories(wenamp_core PUBLIC include)
target_link_libraries(wenamp_core PUBLIC wenamp_miniaudio)
if(NOT MSVC)
  target_compile_options(wenamp_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ── Headless CLI ──
add_executable(wenamp-cli src/cli_main.cpp)
target_link_libraries(wenamp-cli PRIVATE wenamp_core)
if(NOT MSVC)
  target_compile_options(wenamp-cli PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ── SDL2 window front-end (optional) ──
if(WENAMP_BUILD_GUI)
  find_package(SDL2 QUIET)
  if(SDL2_FOUND)
    add_executable(wenamp src/platform_sdl.cpp)
    if(TARGET SDL2::SDL2)
      target_link_libraries(wenamp PRIVATE wenamp_core SDL2::SDL2)
    else()
      target_include_directories(wenamp PRIVATE ${SDL2_INCLUDE_DIRS})
      target_link_libraries(wenamp PRIVATE wenamp_core ${SDL2_LIBRARIES})
    endif()
    if(NOT MSVC)
      target_compile_options(wenamp PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    install(TARGETS wenamp RUNTIME DESTINATION bin)
  else()
    message(STATUS "SDL2 not found - skipping the 'wenamp' window front-end (wenamp-cli still builds)")
  endif()
endif()

install(TARGETS wenamp-cli RUNTIME DESTINATION bin)

# ── Tests ──
if(WENAMP_BUILD_TESTS)
  enable_testing()
  add_executable(wenamp_tests
    tests/test_main.cpp
    tests/test_dsp.cpp
    tests/test_playlist.cpp
    tests/test_tags.cpp
    tests/test_player.cpp
    tests/test_ui.cpp)
  target_link_libraries(wenamp_tests PRIVATE wenamp_core)
  target_include_directories(wenamp_tests PRIVATE tests)
  if(NOT MSVC)
    target_compile_options(wenamp_tests PRIVATE -Wall -Wextra)
  endif()
  add_test(NAME unit COMMAND wenamp_tests)
  add_test(NAME cli-selftest COMMAND wenamp-cli selftest)
endif()
