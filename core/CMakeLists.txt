add_library(mpp_core STATIC
  src/bin_commands.cpp
  src/command.cpp
  src/compositing.cpp
  src/editor.cpp
  src/ffmpeg_source.cpp
  src/image.cpp
  src/image_io.cpp
  src/media.cpp
  src/media_cache.cpp
  src/project.cpp
  src/project_file.cpp
  src/rate.cpp
  src/renderer.cpp
  src/resample.cpp
  src/time_scale.cpp
  src/timeline_commands.cpp
  src/transitions.cpp
)
target_include_directories(mpp_core PUBLIC include)
target_include_directories(mpp_core SYSTEM PRIVATE
  ${PROJECT_SOURCE_DIR}/third_party/stb
  ${PROJECT_SOURCE_DIR}/third_party)
# No fused multiply-add: rendered frames must match the goldens on every CPU.
target_compile_options(mpp_core PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off)
target_compile_definitions(mpp_core PUBLIC
  MPP_FONT_DIR="${PROJECT_SOURCE_DIR}/third_party/fonts")

# FFmpeg from the system (brew install ffmpeg).
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
  libavformat libavcodec libavutil libswscale libswresample)
target_link_libraries(mpp_core PUBLIC PkgConfig::FFMPEG)
find_package(Threads REQUIRED)
target_link_libraries(mpp_core PUBLIC Threads::Threads)
