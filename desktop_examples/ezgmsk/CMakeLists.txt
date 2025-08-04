add_executable(demod_iq_recording demod_iq_recording.cpp)

target_link_libraries(demod_iq_recording PRIVATE cler_desktop_blocks)

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

if(UNIX)
    set(CHMOD_COMMAND chmod +x ${CMAKE_CURRENT_BINARY_DIR}/analyze_demod_outs.py)
else()
    set(CHMOD_COMMAND "")
endif()
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/analyze_demod_outs.py
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_SOURCE_DIR}/analyze_demod_outs.py
            ${CMAKE_CURRENT_BINARY_DIR}/analyze_demod_outs.py
    COMMAND ${CHMOD_COMMAND}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/analyze_demod_outs.py
    COMMENT "Copying and setting executable permission on analyze_demod_outs.py"
)

# Create a custom target that triggers both copy commands
add_custom_target(ezgmsk_copy_assets ALL
    DEPENDS
        ${CMAKE_CURRENT_BINARY_DIR}/${RECORDED_STREAM1}
        ${CMAKE_CURRENT_BINARY_DIR}/${RECORDED_STREAM2}
        ${CMAKE_CURRENT_BINARY_DIR}/analyze_demod_outs.py
    COMMENT "Copying recorded streams to binary directory"
)

# # Make your executable depend on this custom target
add_dependencies(demod_iq_recording ezgmsk_copy_assets)

add_executable(mod_demod_simulation mod_demod_simulation.cpp)
target_link_libraries(mod_demod_simulation PRIVATE cler_desktop_blocks)

#was for developing.. now it just slows compilation down
# add_subdirectory(sandbox)