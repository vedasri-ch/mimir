cmake_minimum_required(VERSION 3.20)
project(Mimir VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
add_compile_options(
  -Wall -Wextra -Wpedantic
  -Wno-unused-parameter
  $<$<CONFIG:Release>:-O3 -march=native>
  $<$<CONFIG:Debug>:-g -O0 -fsanitize=address,undefined>
)

add_link_options(
  $<$<CONFIG:Debug>:-fsanitize=address,undefined>
)

# Dependencies
find_package(Threads REQUIRED)
find_package(yaml-cpp REQUIRED)

# GoogleTest
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/v1.14.0.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(googletest)

# Source files
file(GLOB_RECURSE MIMIR_SOURCES
  src/server/*.cpp
  src/network/*.cpp
  src/protocol/*.cpp
  src/storage/*.cpp
  src/wal/*.cpp
  src/persistence/*.cpp
  src/expiration/*.cpp
  src/threadpool/*.cpp
  src/config/*.cpp
  src/logging/*.cpp
)

# Main server executable
add_executable(mimir src/main.cpp ${MIMIR_SOURCES})
target_include_directories(mimir PRIVATE src)
target_link_libraries(mimir PRIVATE Threads::Threads yaml-cpp)

# CLI client
add_executable(mimir-cli src/cli/main.cpp src/protocol/serializer.cpp)
target_include_directories(mimir-cli PRIVATE src)

# Tests
enable_testing()
file(GLOB_RECURSE TEST_SOURCES tests/*.cpp)
add_executable(mimir_tests ${TEST_SOURCES} ${MIMIR_SOURCES})
target_include_directories(mimir_tests PRIVATE src)
target_link_libraries(mimir_tests PRIVATE GTest::gtest_main Threads::Threads yaml-cpp)
include(GoogleTest)
gtest_discover_tests(mimir_tests)

# Benchmark
add_executable(mimir_bench benchmarks/bench_main.cpp ${MIMIR_SOURCES})
target_include_directories(mimir_bench PRIVATE src)
target_link_libraries(mimir_bench PRIVATE Threads::Threads yaml-cpp)

# clang-format target
find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
  file(GLOB_RECURSE ALL_CXX_SOURCE_FILES src/*.cpp src/*.hpp tests/*.cpp benchmarks/*.cpp)
  add_custom_target(format
    COMMAND ${CLANG_FORMAT} -i ${ALL_CXX_SOURCE_FILES}
    COMMENT "Running clang-format on Mimir sources"
  )
endif()
