cmake_minimum_required(VERSION 3.20)
project(SAMVST LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SAM_VST_BUILD_IPLUG2 "Build the iPlug2 VST3 skeleton target" ON)
set(IPLUG2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/iPlug2" CACHE PATH "Path to iPlug2 root directory")

if(SAM_VST_BUILD_IPLUG2)
  if(NOT EXISTS "${IPLUG2_DIR}/iPlug2.cmake")
    message(FATAL_ERROR "iPlug2.cmake not found at ${IPLUG2_DIR}. Pass -DIPLUG2_DIR=/absolute/path/to/iPlug2")
  endif()

  add_subdirectory(plugin)
endif()
