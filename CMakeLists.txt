# warploom-game — the first external consumer of the warploom engine.
#
# A minimal headless game: drop a tower of simulated cubes, run a fixed-step
# simulation, and prove the replay reproduces it. It depends on warploom as
# a package (FetchContent) and touches the public API only
# (warploom/core/..., warploom/physics/...). If this file needs an
# engine-internal header, that is an API gap and the fix belongs in
# warploom, not here.
cmake_minimum_required(VERSION 3.28)

project(warploom-game LANGUAGES CXX)

# Engine options, fixed before FetchContent runs: no tests, no examples, no
# Vulkan. This game is headless by design — CI has no GPU and needs none.
set(WARPLOOM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WARPLOOM_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WARPLOOM_USE_VULKAN OFF CACHE BOOL "" FORCE)
set(WARPLOOM_ENABLE_LINTING OFF CACHE BOOL "" FORCE)
set(WARPLOOM_ENABLE_FORMATTING OFF CACHE BOOL "" FORCE)

include(FetchContent)
FetchContent_Declare(
  warploom
  GIT_REPOSITORY https://github.com/WyattAu/warploom.git
  GIT_TAG main
)
FetchContent_MakeAvailable(warploom)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_executable(drop src/drop.cpp)
target_link_libraries(drop PRIVATE Warploom::engine)
target_compile_features(drop PRIVATE cxx_std_23)

# The game's proof is its exit code: 0 iff the replay reproduces the run.
enable_testing()
add_test(NAME replay_determinism COMMAND drop)
