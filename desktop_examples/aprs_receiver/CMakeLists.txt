if (CLER_BUILD_BLOCKS_GUI AND CLER_BUILD_BLOCKS_LIQUID)
    add_executable(aprs_receiver aprs_receiver.cpp)
    target_link_libraries(aprs_receiver PRIVATE cler::desktop_blocks)
    if (HACKRF_FOUND)
        target_compile_definitions(aprs_receiver PRIVATE APRS_HAVE_HACKRF)
    endif()
    add_custom_command(TARGET aprs_receiver POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/desktop_blocks/adsb/coastlines_map
            $<TARGET_FILE_DIR:aprs_receiver>/adsb_coastlines
        COMMENT "Copying coastline data next to aprs_receiver")
else()
    message(STATUS "Skipping aprs_receiver example (needs GUI + liquid)")
endif()
