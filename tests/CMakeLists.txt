include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.8.1
  GIT_SHALLOW TRUE
  FIND_PACKAGE_ARGS 3
)
FetchContent_MakeAvailable(Catch2)

add_executable(mpp_tests
  golden.cpp
  test_bin_commands.cpp
  test_ffmpeg_source.cpp
  test_filters.cpp
  test_keys.cpp
  test_media_cache.cpp
  test_project_file.cpp
  test_rate.cpp
  test_renderer.cpp
  test_resample.cpp
  test_time_scale.cpp
  test_timeline_commands.cpp
  test_transitions.cpp
)
target_link_libraries(mpp_tests PRIVATE mpp_core Catch2::Catch2WithMain)
target_compile_definitions(mpp_tests PRIVATE
  MPP_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
  MPP_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
  MPP_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}/output"
)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/output)

add_test(NAME mpp_tests COMMAND mpp_tests)
