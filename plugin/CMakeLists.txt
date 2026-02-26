cmake_minimum_required(VERSION 3.14)
project(SAMVST VERSION 0.1.0)

include(${IPLUG2_DIR}/iPlug2.cmake)
find_package(iPlug2 REQUIRED)

iplug_add_plugin(${PROJECT_NAME}
  FORMATS VST3
  SOURCES
    ../src/reciter.c
    ../src/sam.c
    ../src/render.c
    ../src/processframes.c
    ../src/createtransitions.c
    ../src/debug.c
    src/SAMBridge.cpp
    src/SAMVST.cpp
    src/SAMBridge.h
    src/SAMVST.h
    src/config.h
    resources/resource.h
  RESOURCES
    resources/fonts/C64_Pro_Mono-STYLE.ttf
)

target_include_directories(${PROJECT_NAME}-vst3 PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_LIST_DIR}/../src
)
