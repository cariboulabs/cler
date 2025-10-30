# blocks/sinks/CMakeLists.txt

add_library(blocks_sinks INTERFACE)
add_library(cler::blocks_sinks ALIAS blocks_sinks)

target_include_directories(blocks_sinks INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

find_package(PkgConfig REQUIRED)

# === PortAudio ===
pkg_check_modules(PORTAUDIO portaudio-2.0)
if(PORTAUDIO_FOUND)
  message(STATUS "PortAudio found: ${PORTAUDIO_LIBRARIES}")

  target_include_directories(blocks_sinks INTERFACE ${PORTAUDIO_INCLUDE_DIRS})
  target_link_libraries(blocks_sinks INTERFACE ${PORTAUDIO_LIBRARIES})
  target_compile_definitions(blocks_sinks INTERFACE CLER_HAS_PORTAUDIO)

  set(PORTAUDIO_FOUND ${PORTAUDIO_FOUND} CACHE INTERNAL "PortAudio found")
else()
  message(WARNING "PortAudio not found: SinkAudioBlock will not be available")
  message(STATUS "  Install with: sudo apt-get install portaudio19-dev (Ubuntu/Debian)")
endif()

# === UHD (USRP) ===
pkg_check_modules(UHD uhd)
if(UHD_FOUND)
  message(STATUS "UHD found: ${UHD_LIBRARIES}")

  target_include_directories(blocks_sinks INTERFACE ${UHD_INCLUDE_DIRS})
  target_link_libraries(blocks_sinks INTERFACE ${UHD_LIBRARIES})
  target_compile_definitions(blocks_sinks INTERFACE CLER_HAS_UHD)

  set(UHD_FOUND ${UHD_FOUND} CACHE INTERNAL "UHD found")
else()
  message(WARNING "UHD not found: SinkUHDBlock will not be available")
  message(STATUS "  Install with: sudo apt-get install libuhd-dev (Ubuntu/Debian)")
endif()

# === SoapySDR ===
pkg_check_modules(SOAPYSDR SoapySDR)
if(SOAPYSDR_FOUND)
  message(STATUS "SoapySDR found: ${SOAPYSDR_LIBRARIES}")

  target_include_directories(blocks_sinks INTERFACE ${SOAPYSDR_INCLUDE_DIRS})
  target_link_libraries(blocks_sinks INTERFACE ${SOAPYSDR_LIBRARIES})
  target_compile_definitions(blocks_sinks INTERFACE CLER_HAS_SOAPYSDR)

  set(SOAPYSDR_FOUND ${SOAPYSDR_FOUND} CACHE INTERNAL "SoapySDR found")
else()
  message(WARNING "SoapySDR not found: SinkSoapySDRBlock will not be available")
endif()
