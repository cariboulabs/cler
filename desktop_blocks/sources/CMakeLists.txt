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

# === UHD (USRP) ===
pkg_check_modules(UHD uhd)
if(UHD_FOUND)
  message(STATUS "UHD found: ${UHD_LIBRARIES}")

  target_include_directories(blocks_sources INTERFACE ${UHD_INCLUDE_DIRS})
  target_link_libraries(blocks_sources INTERFACE ${UHD_LIBRARIES})
  target_compile_definitions(blocks_sources INTERFACE CLER_HAS_UHD)

  set(UHD_FOUND ${UHD_FOUND} CACHE INTERNAL "UHD found")
else()
  message(WARNING "UHD not found: SourceUHDBlock and SinkUHDBlock will not be available")
  message(STATUS "  Install with: sudo apt-get install libuhd-dev (Ubuntu/Debian)")
endif()

# === SoapySDR ===
pkg_check_modules(SOAPYSDR SoapySDR)
if(SOAPYSDR_FOUND)
  message(STATUS "SoapySDR found: ${SOAPYSDR_LIBRARIES}")

  target_include_directories(blocks_sources INTERFACE ${SOAPYSDR_INCLUDE_DIRS})
  target_link_libraries(blocks_sources INTERFACE ${SOAPYSDR_LIBRARIES})
  target_compile_definitions(blocks_sources INTERFACE CLER_HAS_SOAPYSDR)

  set(SOAPYSDR_FOUND ${SOAPYSDR_FOUND} CACHE INTERNAL "SoapySDR found")
else()
  message(WARNING "SoapySDR not found: SourceSoapySDRBlock and SinkSoapySDRBlock will not be available")
endif()
