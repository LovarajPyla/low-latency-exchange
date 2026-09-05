cmake_minimum_required(VERSION 3.16)

project(low_latency_exchange CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

include_directories(${CMAKE_SOURCE_DIR})

find_package(Threads REQUIRED)

# ============================================================
# Executables
# ============================================================

add_executable(
    matching_engine
    engine/matching_engine.cpp
)

add_executable(
    gateway
    networking/tcp_server.cpp
)

target_link_libraries(
    gateway
    PRIVATE
    Threads::Threads
)

add_executable(
    replay
    simulator/market_replay.cpp
)

add_executable(
    bench
    benchmarks/latency_benchmark.cpp
)

# ============================================================
# GoogleTest
#
# The current macOS/Anaconda environment provides:
#
#   /opt/anaconda3/lib/libgtest.1.11.0.dylib
#   /opt/anaconda3/lib/libgtest_main.1.11.0.dylib
#
# We add that directory to the runtime search path so macOS
# can locate the libraries when ctest executes the test.
# ============================================================

find_package(GTest REQUIRED)

# ============================================================
# Tests
# ============================================================

enable_testing()

add_executable(
    orderbook_test
    tests/orderbook_test.cpp
)

target_link_libraries(
    orderbook_test
    PRIVATE
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)

# macOS runtime search path for Anaconda GoogleTest.
if(APPLE)
    set_target_properties(
        orderbook_test
        PROPERTIES
        BUILD_RPATH "/opt/anaconda3/lib"
        INSTALL_RPATH "/opt/anaconda3/lib"
    )
endif()

add_test(
    NAME orderbook_test
    COMMAND orderbook_test
)