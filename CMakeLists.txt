cmake_minimum_required(VERSION 3.24)
project(mock_premiere LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(MPP_BUILD_TESTS "Build the core test suite" ON)
option(MPP_BUILD_APP "Build the NanoGUI application" ON)

add_subdirectory(core)

if(MPP_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(MPP_BUILD_APP)
  add_subdirectory(app)
endif()
