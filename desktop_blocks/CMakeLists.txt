add_library(cler_desktop_blocks INTERFACE)
add_library(cler::cler_desktop_blocks ALIAS cler_desktop_blocks)

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

if (CLER_BUILD_BLOCKS_GUI)
  add_subdirectory(gui)
  if (CLER_BUILD_BLOCKS_LIQUID)
    add_subdirectory(plots)
  else()
    message(STATUS "Skipping plots because CLER_BUILD_BLOCKS_LIQUID is OFF")
  endif()
else()
  message(STATUS "Skipping GUI blocks because CLER_BUILD_BLOCKS_GUI is OFF")
endif()

add_subdirectory(udp)
add_subdirectory(sources)

target_link_libraries(cler_desktop_blocks INTERFACE   
  cler::blocks_udp
  cler::blocks_sources
)

if (CLER_BUILD_BLOCKS_LIQUID)
  add_subdirectory(ezgmsk_demod)
  target_link_libraries(cler_desktop_blocks INTERFACE 
    cler::blocks_ezgmsk_demod
    )
endif()

if (CLER_BUILD_BLOCKS_GUI)
  target_link_libraries(cler_desktop_blocks INTERFACE 
    cler::blocks_gui
  )
endif()

if (CLER_BUILD_BLOCKS_LIQUID AND CLER_BUILD_BLOCKS_GUI)
  target_link_libraries(cler_desktop_blocks INTERFACE
    cler::blocks_plots
  )
endif()