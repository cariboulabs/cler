add_library(clgmskframesync
    clgmskframesync.c
)
target_include_directories(clgmskframesync PUBLIC
  /home/alon/repos/cler/build/_deps/liquid-src/include
  /home/alon/repos/cler/build/_deps/liquid-src/scripts
)
target_link_libraries(clgmskframesync PUBLIC
  liquid
)

add_executable(main main.cpp)

target_link_libraries(main PRIVATE 
    clgmskframesync
    ${CMAKE_THREAD_LIBS_INIT}
)

set(RECORDED_STREAM1 recordings/recorded_stream_0x55904E.bin)
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${RECORDED_STREAM1}
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_SOURCE_DIR}/${RECORDED_STREAM1}
            ${CMAKE_CURRENT_BINARY_DIR}/${RECORDED_STREAM1}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${RECORDED_STREAM1}
)
set (RECORDED_STREAM2 recordings/recorded_stream_0xD391A6.bin)
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${RECORDED_STREAM2}
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_SOURCE_DIR}/${RECORDED_STREAM2}
            ${CMAKE_CURRENT_BINARY_DIR}/${RECORDED_STREAM2}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${RECORDED_STREAM2}
)

# Create a custom target that triggers both copy commands
add_custom_target(copy_assets ALL
    DEPENDS
        ${CMAKE_CURRENT_BINARY_DIR}/${RECORDED_STREAM1}
        ${CMAKE_CURRENT_BINARY_DIR}/${RECORDED_STREAM2}
    COMMENT "Copying recorded streams to binary directory"
)

# Make your executable depend on this custom target
add_dependencies(main copy_assets)