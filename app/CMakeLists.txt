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
  audio_player.cpp
  capture_window.cpp
  clip_window.cpp
  construction_view.cpp
  filters_window.cpp
  frame_view.cpp
  key_window.cpp
  main.cpp
  make_windows.cpp
  menu_bar.cpp
  motion_window.cpp
  project_window.cpp
  selftest.cpp
  sequence_window.cpp
  settings_window.cpp
  timeline_windows.cpp
  title_window.cpp
  transition_windows.cpp
)
target_link_libraries(mock_premiere PRIVATE mpp_core nanogui)
target_include_directories(mock_premiere SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party/miniaudio)
# Apple deprecates OpenGL; NanoGUI's backend and our screenshot code use it.
target_compile_definitions(mock_premiere PRIVATE GL_SILENCE_DEPRECATION)
target_compile_options(mock_premiere PRIVATE -Wall -Wextra)

if(APPLE)
  # macOS grants the camera and microphone only to programs that say why
  # they want them. The Info.plist is embedded in the executable (for
  # running it from a terminal) and goes into Mock Premiere.app (for Finder).
  set(MPP_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist)
  target_link_options(mock_premiere PRIVATE "LINKER:-sectcreate,__TEXT,__info_plist,${MPP_INFO_PLIST}")
  set_property(TARGET mock_premiere APPEND PROPERTY LINK_DEPENDS ${MPP_INFO_PLIST})
  set(MPP_BUNDLE "${CMAKE_CURRENT_BINARY_DIR}/Mock Premiere.app")
  add_custom_command(TARGET mock_premiere POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${MPP_BUNDLE}/Contents/MacOS"
    COMMAND ${CMAKE_COMMAND} -E copy ${MPP_INFO_PLIST} "${MPP_BUNDLE}/Contents/Info.plist"
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:mock_premiere> "${MPP_BUNDLE}/Contents/MacOS/mock_premiere"
    COMMAND codesign --force --sign - "${MPP_BUNDLE}"
    VERBATIM)
endif()
