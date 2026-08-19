if (CLER_BUILD_BLOCKS_GUI AND CLER_BUILD_BLOCKS_LIQUID)
    add_executable(ais_receiver ais_receiver.cpp)
    target_link_libraries(ais_receiver PRIVATE cler::desktop_blocks)
    if (HACKRF_FOUND)
        target_compile_definitions(ais_receiver PRIVATE AIS_HAVE_HACKRF)
    endif()
    add_custom_command(TARGET ais_receiver POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/desktop_blocks/adsb/coastlines_map
            $<TARGET_FILE_DIR:ais_receiver>/adsb_coastlines
        COMMENT "Copying coastline data next to ais_receiver")
else()
    message(STATUS "Skipping ais_receiver example (needs GUI + liquid)")
endif()
