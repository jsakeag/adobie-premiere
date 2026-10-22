add_library(mpp_core STATIC
  src/audio_mixer.cpp
  src/bin_commands.cpp
  src/capture.cpp
  src/command.cpp
  src/compositing.cpp
  src/edl.cpp
  src/editor.cpp
  src/export_job.cpp
  src/ffmpeg_source.cpp
  src/filmstrip.cpp
  src/filters.cpp
  src/image.cpp
  src/image_io.cpp
  src/image_sequence.cpp
  src/keys.cpp
  src/media.cpp
  src/media_cache.cpp
  src/motion.cpp
  src/movie_writer.cpp
  src/project.cpp
  src/project_file.cpp
  src/rate.cpp
  src/render_job.cpp
  src/renderer.cpp
  src/resample.cpp
  src/time_scale.cpp
  src/timeline_commands.cpp
  src/title.cpp
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

# FFmpeg from the system (brew install ffmpeg); libavdevice for capture.
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
  libavdevice libavformat libavcodec libavutil libswscale libswresample)
target_link_libraries(mpp_core PUBLIC PkgConfig::FFMPEG)
find_package(Threads REQUIRED)
target_link_libraries(mpp_core PUBLIC Threads::Threads)
