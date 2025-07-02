# add_executable(02_main 02_main.cpp)

# target_include_directories(02_main PRIVATE 
#     ${PROJECT_ROOT}/src
#     ${PROJECT_ROOT}/src/logger
#     ${LIQUID_INCLUDE_DIRS}
# )

# target_link_libraries(02_main PRIVATE 
#     clgmskframesync
#     logger
#     ${CMAKE_THREAD_LIBS_INIT}
# )

# set(RECORDED_STREAM recorded_stream_0x55904E.bin)

# # Custom command to copy the binary file at build time
# add_custom_command(
#     OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${RECORDED_STREAM}
#     COMMAND ${CMAKE_COMMAND} -E copy
#             ${CMAKE_CURRENT_SOURCE_DIR}/${RECORDED_STREAM}
#             ${CMAKE_CURRENT_BINARY_DIR}/${RECORDED_STREAM}
#     DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${RECORDED_STREAM}
#     COMMENT "Copying recorded_stream_0x55904E.bin"
# )

# # Create a custom target that triggers both copy commands
# add_custom_target(02_copy_assets ALL
#     DEPENDS
#         ${CMAKE_CURRENT_BINARY_DIR}/${RECORDED_STREAM}
# )

# # Make your executable depend on this custom target
# add_dependencies(02_main 02_copy_assets)