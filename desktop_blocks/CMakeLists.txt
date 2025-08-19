add_library(cler_desktop_blocks INTERFACE)
add_library(cler::desktop_blocks ALIAS cler_desktop_blocks)

target_link_libraries(cler_desktop_blocks INTERFACE cler::cler)

target_include_directories(cler_desktop_blocks INTERFACE #so people call #include "blocks/..."
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
)

if (CLER_BUILD_BLOCKS_LIQUID)
  include(FetchContent)
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_AUTOTESTS OFF CACHE BOOL "" FORCE)
  set(BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
  
  FetchContent_Declare(
    liquid
    GIT_REPOSITORY https://github.com/jgaeddert/liquid-dsp
    GIT_TAG master
  )
  FetchContent_MakeAvailable(liquid)
  target_include_directories(liquid INTERFACE 
      SYSTEM ${liquid_SOURCE_DIR}/include #system ignore warnings from liquid
      SYSTEM ${liquid_SOURCE_DIR}/scripts #for config.h
  )
endif()

#so any block that wants can use this..
add_library(blocks_blob STATIC blob.cpp)
add_library(cler::blocks_blob ALIAS blocks_blob)
target_link_libraries(blocks_blob PUBLIC cler::cler)

add_subdirectory(sources)
target_link_libraries(cler_desktop_blocks INTERFACE 
  cler::blocks_sources
)

if (CLER_BUILD_BLOCKS_GUI)
  add_subdirectory(gui)
  add_subdirectory(plots)

  target_link_libraries(cler_desktop_blocks INTERFACE 
    cler::blocks_gui
  )
  target_link_libraries(cler_desktop_blocks INTERFACE
    cler::blocks_plots
  )
else()
  message(STATUS "Skipping GUI blocks because CLER_BUILD_BLOCKS_GUI is OFF")
endif()

if (NOT WIN32)
  add_subdirectory(udp)
  target_link_libraries(cler_desktop_blocks INTERFACE   
    cler::blocks_udp
  )
else()
  message(STATUS "Skipping UDP blocks because this is a Windows build")
endif()

if (CLER_BUILD_BLOCKS_LIQUID)
  add_subdirectory(ezgmsk)
  target_link_libraries(cler_desktop_blocks INTERFACE 
    cler::blocks_ezgmsk)
endif()