# fm_radio picks its HackRF / Pluto / SoapySDR source at runtime; each is
# compiled in only if its library was found, asking for a missing one panics.
if (CLER_BUILD_BLOCKS_GUI AND CLER_BUILD_BLOCKS_LIQUID AND PORTAUDIO_FOUND
    AND (HACKRF_FOUND OR LIBIIO_FOUND OR SOAPYSDR_FOUND))
    add_executable(fm_radio fm_radio.cpp)
    target_link_libraries(fm_radio PRIVATE cler::desktop_blocks)
    if (SOAPYSDR_FOUND)
        target_link_libraries(fm_radio PRIVATE PkgConfig::SOAPYSDR)
    endif()
else()
    message(STATUS "Skipping fm_radio example (needs GUI + liquid + PortAudio + one of HackRF/libiio/SoapySDR)")
endif()
