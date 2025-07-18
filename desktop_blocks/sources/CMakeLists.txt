# blocks/sources/CMakeLists.txt

add_library(blocks_sources INTERFACE)
add_library(cler::blocks_sources ALIAS blocks_sources)

target_include_directories(blocks_sources INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

find_package(PkgConfig REQUIRED)

# === CaribouLite ===
pkg_check_modules(CARIBOULITE cariboulite)
if(CARIBOULITE_FOUND)
  message(STATUS "CaribouLite found: ${CARIBOULITE_LIBRARIES}")

  target_include_directories(blocks_sources INTERFACE ${CARIBOULITE_INCLUDE_DIRS})
  target_link_libraries(blocks_sources INTERFACE ${CARIBOULITE_LIBRARIES})

  set(CARIBOULITE_FOUND ${CARIBOULITE_FOUND} CACHE INTERNAL "CaribouLite found")
else()
  message(WARNING "CaribouLite package not found: SourceCaribouliteBlock will not be available")
endif()

# === HackRF ===
pkg_check_modules(HACKRF libhackrf)
if(HACKRF_FOUND)
  message(STATUS "HackRF found: ${HACKRF_LIBRARIES}")

  target_include_directories(blocks_sources INTERFACE ${HACKRF_INCLUDE_DIRS})
  target_link_libraries(blocks_sources INTERFACE ${HACKRF_LIBRARIES})

  set(HACKRF_FOUND ${HACKRF_FOUND} CACHE INTERNAL "HackRF found")
else()
  message(WARNING "HackRF not found: SourceHackRFBlock will not be available")
endif()
