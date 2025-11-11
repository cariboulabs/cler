if (CLER_BUILD_BLOCKS_GUI)
    add_executable(hello_world hello_world.cpp)
    target_link_libraries(hello_world PRIVATE cler::desktop_blocks)
else()
    message(STATUS "Skipping hello_world example because CLER_BUILD_BLOCKS_GUI is OFF")
endif()

add_executable(streamlined streamlined.cpp)
target_link_libraries(streamlined PRIVATE cler::cler)

add_executable(flowgraph flowgraph.cpp)
target_link_libraries(flowgraph PRIVATE cler::cler)

if (CLER_BUILD_BLOCKS_GUI AND CLER_BUILD_BLOCKS_LIQUID)
    add_executable(plots plots.cpp)
    target_link_libraries(plots PRIVATE cler::desktop_blocks)

    add_executable(frequency_shift frequency_shift.cpp)
    target_link_libraries(frequency_shift PRIVATE cler::desktop_blocks)
else()
    message(STATUS "Skipping plots example because CLER_BUILD_BLOCKS_GUI or CLER_BUILD_BLOCKS_LIQUID is OFF")
    message(STATUS "Skipping frequency_shift example because CLER_BUILD_BLOCKS_GUI or CLER_BUILD_BLOCKS_LIQUID is OFF")
endif()

if (CLER_BUILD_BLOCKS_LIQUID)
    add_subdirectory(ezgmsk)
else()
    message(STATUS "Skipping ezgmsk example because CLER_BUILD_BLOCKS_LIQUID is OFF")
endif()

if (CLER_BUILD_BLOCKS_GUI)
    add_executable(mass_spring_damper mass_spring_damper.cpp)
    target_link_libraries(mass_spring_damper PRIVATE cler::desktop_blocks)
else()
    message(STATUS "Skipping mass_spring_damper example because CLER_BUILD_BLOCKS_GUI is OFF")
endif()

add_executable(udp udp.cpp)
target_link_libraries(udp PRIVATE cler::desktop_blocks)

if (CLER_BUILD_BLOCKS_GUI AND CLER_BUILD_BLOCKS_LIQUID)
    add_executable(polyphase_channelizer polyphase_channelizer.cpp)
    target_link_libraries(polyphase_channelizer PRIVATE cler::desktop_blocks)
else()
    message(STATUS "Skipping polyphase_channelizer example because CLER_BUILD_BLOCKS_GUI or CLER_BUILD_BLOCKS_LIQUID is OFF")
endif()

if (CLER_BUILD_BLOCKS_GUI)
    if (CARIBOULITE_FOUND)
        add_executable(cariboulite_spectrum cariboulite_spectrum.cpp)
        target_link_libraries(cariboulite_spectrum PRIVATE cler::desktop_blocks)
    else()
        message(STATUS "Skipping cariboulite_spectrum example because CARIBOULITE_FOUND is OFF")
    endif()
else()
    message(STATUS "Skipping cariboulite_spectrum example because CLER_BUILD_BLOCKS_GUI is OFF")
endif()

if (CLER_BUILD_BLOCKS_GUI)
    if (HACKRF_FOUND)
        add_executable(hackrf_spectrum hackrf_spectrum.cpp)
        target_link_libraries(hackrf_spectrum PRIVATE cler::desktop_blocks)
    else()
        message(STATUS "Skipping hackrf_spectrum example because HACKRF_FOUND is OFF")
    endif()
else()
    message(STATUS "Skipping hackrf_spectrum example because CLER_BUILD_BLOCKS_GUI is OFF")
endif()

add_executable(selectable_blocks selectable_blocks.cpp)
target_link_libraries(selectable_blocks PRIVATE cler::desktop_blocks)

if (CARIBOULITE_FOUND)
    add_executable(cariboulite_recorder cariboulite_recorder.cpp)
    target_link_libraries(cariboulite_recorder PRIVATE cler::desktop_blocks)
else()
    message(STATUS "Skipping cariboulite_recorder example because CARIBOULITE_FOUND is OFF")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SOAPYSDR IMPORTED_TARGET SoapySDR)

if(SOAPYSDR_FOUND AND CLER_BUILD_BLOCKS_LIQUID AND CLER_BUILD_BLOCKS_GUI)
  message(STATUS "SoapySDR found - building SoapySDR example")
  add_executable(soapysdr_device soapysdr_device.cpp)
  target_link_libraries(soapysdr_device PRIVATE cler::desktop_blocks PkgConfig::SOAPYSDR)
else()
  message(STATUS "Skipping soapysdr_device - requires SoapySDR and GUI blocks")
endif()

if(SOAPYSDR_FOUND AND CLER_BUILD_BLOCKS_LIQUID)
  message(STATUS "Building FM receiver example")
  add_executable(fm_receiver fm_receiver.cpp)
  target_link_libraries(fm_receiver PRIVATE cler::desktop_blocks PkgConfig::SOAPYSDR)
else()
  message(STATUS "Skipping fm_receiver - requires SoapySDR and CLER_BUILD_BLOCKS_LIQUID")
endif()

if (UHD_FOUND AND CLER_BUILD_BLOCKS_GUI AND CLER_BUILD_BLOCKS_LIQUID)
    add_executable(uhd_device uhd_device.cpp)
    target_link_libraries(uhd_device PRIVATE cler::desktop_blocks uhd)
else()
    message(STATUS "Skipping usrp_example - requires UHD and GUI blocks")
endif()

if (CLER_BUILD_BLOCKS_GUI AND SOAPYSDR_FOUND)
    add_executable(adsb_receiver adsb_receiver.cpp)
    target_link_libraries(adsb_receiver PRIVATE cler::desktop_blocks PkgConfig::SOAPYSDR)

    # Copy ADSB coastline data to binary directory
    add_custom_command(TARGET adsb_receiver POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/desktop_blocks/adsb/coastlines_map
            $<TARGET_FILE_DIR:adsb_receiver>/adsb_coastlines
        COMMENT "Copying ADSB coastline data to binary directory"
    )

    # Copy ADSB recording file to binary directory
    add_custom_command(TARGET adsb_receiver POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_SOURCE_DIR}/desktop_examples/adsb_recording.bin
            $<TARGET_FILE_DIR:adsb_receiver>/adsb_recording.bin
        COMMENT "Copying ADSB recording file to binary directory"
    )


else()
    message(STATUS "Skipping adsb_receiver example because CLER_BUILD_BLOCKS_GUI or SOAPYSDR_FOUND is OFF")
endif()

if(FFMPEG_FOUND AND PORTAUDIO_FOUND)
    add_executable(audio_file_playback audio_file_playback.cpp)
    target_link_libraries(audio_file_playback PRIVATE cler::desktop_blocks)

    # Copy audio file to binary directory
    add_custom_command(TARGET audio_file_playback POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_SOURCE_DIR}/desktop_examples/Blues_Traveler_Hook.mp3
            $<TARGET_FILE_DIR:audio_file_playback>/Blues_Traveler_Hook.mp3
        COMMENT "Copying audio file to binary directory"
    )
else()
    message(STATUS "Skipping audio_file_playback example - requires FFmpeg and PortAudio")
endif()
# HackRF TX Examples
if (CLER_BUILD_BLOCKS_GUI)
    if (HACKRF_FOUND)        
        add_executable(hackrf_cw hackrf_cw.cpp)
        target_link_libraries(hackrf_cw PRIVATE cler::desktop_blocks)
    else()
        message(STATUS "Skipping hackrf_cw example because HACKRF_FOUND is OFF")
    endif()
else()
    message(STATUS "Skipping hackrf_cw example because CLER_BUILD_BLOCKS_GUI is OFF")
endif()