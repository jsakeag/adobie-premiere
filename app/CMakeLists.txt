include(FetchContent)

set(NANOGUI_BACKEND "OpenGL" CACHE STRING "" FORCE)
set(NANOGUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(NANOGUI_BUILD_PYTHON OFF CACHE BOOL "" FORCE)
set(NANOGUI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(NANOGUI_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  nanogui
  GIT_REPOSITORY https://github.com/mitsuba-renderer/nanogui.git
  GIT_TAG 500be32ddd2d8f6518c9f29136aaa422dc615a61  # 2026-08-28; later commits break the macOS OpenGL build
)
FetchContent_MakeAvailable(nanogui)

add_executable(mock_premiere
  app.cpp
  clip_window.cpp
  construction_view.cpp
  frame_view.cpp
  main.cpp
  menu_bar.cpp
  project_window.cpp
  selftest.cpp
  settings_window.cpp
  timeline_windows.cpp
)
target_link_libraries(mock_premiere PRIVATE mpp_core nanogui)
# Apple deprecates OpenGL; NanoGUI's backend and our screenshot code use it.
target_compile_definitions(mock_premiere PRIVATE GL_SILENCE_DEPRECATION)
target_compile_options(mock_premiere PRIVATE -Wall -Wextra)
