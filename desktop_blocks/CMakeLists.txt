add_library(cler_desktop_blocks INTERFACE)
add_library(cler::desktop_blocks ALIAS cler_desktop_blocks)

target_link_libraries(cler_desktop_blocks INTERFACE cler::cler)

target_include_directories(cler_desktop_blocks INTERFACE #so people call #include "desktop_blocks/..."
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
)

if (CLER_BUILD_BLOCKS_LIQUID)
  include(FetchContent)

  # Put dep trees under the current build dir (prevents cross-build clashes)
  set(FETCHCONTENT_BASE_DIR "${CMAKE_BINARY_DIR}/_deps")

  # Disable liquid's extra builds
  set(BUILD_EXAMPLES   OFF CACHE BOOL "" FORCE)
  set(BUILD_AUTOTESTS  OFF CACHE BOOL "" FORCE)
  set(BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)

  # Declare liquid dependency
  # TODO: Consider pinning to a specific commit for reproducibility
  FetchContent_Declare(liquid
    GIT_REPOSITORY https://github.com/jgaeddert/liquid-dsp
    GIT_TAG        master   # better: replace with a specific commit
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
  )

  # Download only
  FetchContent_Populate(liquid)

  # Clean mismatched generator cache if needed
  include(${CMAKE_SOURCE_DIR}/cmake/CleanOnGenMismatch.cmake)
  clean_on_gen_mismatch("${liquid_BINARY_DIR}")  # nuke mismatched build cache only

  # Configure & build the dep with current generator
  add_subdirectory(${liquid_SOURCE_DIR} ${liquid_BINARY_DIR})

  target_include_directories(liquid INTERFACE
    SYSTEM ${liquid_SOURCE_DIR}/include
    SYSTEM ${liquid_SOURCE_DIR}/scripts  # for config.h
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

add_subdirectory(sinks)
target_link_libraries(cler_desktop_blocks INTERFACE
  cler::blocks_sinks
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

if (CLER_BUILD_BLOCKS_GUI)
  add_subdirectory(adsb)
  target_link_libraries(cler_desktop_blocks INTERFACE
    cler::blocks_adsb)
else()
  message(STATUS "Skipping ADSB blocks because CLER_BUILD_BLOCKS_GUI is OFF")
endif()