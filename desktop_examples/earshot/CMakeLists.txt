if (CLER_BUILD_BLOCKS_WEB)
    include(${PROJECT_SOURCE_DIR}/cmake/EmbedFiles.cmake)
    cler_embed_dir(earshot_client ${CMAKE_CURRENT_SOURCE_DIR}/client earshot_client_files.hpp EARSHOT_CLIENT_FILES)

    add_executable(web_demo web_demo.cpp)
    target_link_libraries(web_demo PRIVATE cler::desktop_blocks cler::blocks_web)
    cler_embed_link(web_demo earshot_client)

    if (CLER_BUILD_BLOCKS_LIQUID)
        set(_version_hpp ${CMAKE_CURRENT_BINARY_DIR}/earshot_version.hpp)
        add_custom_target(earshot_version
            COMMAND ${CMAKE_COMMAND}
                -DOUT=${_version_hpp} -DSRC=${PROJECT_SOURCE_DIR}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/version.cmake
            BYPRODUCTS ${_version_hpp})
        add_executable(earshot earshot.cpp)
        add_dependencies(earshot earshot_version)
        target_include_directories(earshot PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
        target_link_libraries(earshot PRIVATE cler::desktop_blocks cler::blocks_web)
        cler_embed_link(earshot earshot_client)
    endif()
else()
    message(STATUS "Skipping earshot examples because CLER_BUILD_BLOCKS_WEB is OFF")
endif()
