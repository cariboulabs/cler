if (CLER_BUILD_BLOCKS_WEB)
    include(${PROJECT_SOURCE_DIR}/cmake/EmbedFiles.cmake)
    cler_embed_dir(websdr_client ${CMAKE_CURRENT_SOURCE_DIR}/client websdr_client_files.hpp WEBSDR_CLIENT_FILES)

    add_executable(web_demo web_demo.cpp)
    target_link_libraries(web_demo PRIVATE cler::desktop_blocks cler::blocks_web)
    cler_embed_link(web_demo websdr_client)

    if (CLER_BUILD_BLOCKS_LIQUID)
        set(_version_hpp ${CMAKE_CURRENT_BINARY_DIR}/websdr_version.hpp)
        add_custom_target(websdr_version
            COMMAND ${CMAKE_COMMAND}
                -DOUT=${_version_hpp} -DSRC=${PROJECT_SOURCE_DIR}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/version.cmake
            BYPRODUCTS ${_version_hpp})
        add_executable(websdr websdr.cpp)
        add_dependencies(websdr websdr_version)
        target_include_directories(websdr PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
        target_link_libraries(websdr PRIVATE cler::desktop_blocks cler::blocks_web)
        cler_embed_link(websdr websdr_client)
    endif()
else()
    message(STATUS "Skipping websdr examples because CLER_BUILD_BLOCKS_WEB is OFF")
endif()
