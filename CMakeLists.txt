cmake_minimum_required(VERSION 3.16)
project(BitGlide VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(BITGLIDE_BUILD_TESTS "Build unit tests" ON)
option(BITGLIDE_SANITIZE "Build with address + undefined sanitizers" OFF)

find_package(Threads REQUIRED)

add_library(bitglide_core
  src/log.cpp
  src/url.cpp
  src/playlist.cpp
  src/bandwidth_estimator.cpp
  src/abr.cpp
  src/http_client.cpp
  src/segment_buffer.cpp
  src/player.cpp
  src/config.cpp
  src/prefetcher.cpp
  src/metrics.cpp
  src/controller.cpp
)

target_include_directories(bitglide_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(bitglide_core PUBLIC Threads::Threads)
target_compile_options(bitglide_core PRIVATE -Wall -Wextra -Wpedantic)

if(BITGLIDE_SANITIZE)
  target_compile_options(bitglide_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(bitglide_core PUBLIC -fsanitize=address,undefined)
endif()

add_executable(bitglide src/main.cpp)
target_link_libraries(bitglide PRIVATE bitglide_core)
target_compile_options(bitglide PRIVATE -Wall -Wextra -Wpedantic)

install(TARGETS bitglide RUNTIME DESTINATION bin)
