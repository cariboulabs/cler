add_executable(create_reference_signal create_reference_signal.cpp BitSequence.cpp)

target_link_libraries(create_reference_signal PRIVATE
    liquid
)

if(UNIX)
    set(CHMOD_COMMAND chmod +x ${CMAKE_CURRENT_BINARY_DIR}/analyze_create_reference_signal.py)
else()
    set(CHMOD_COMMAND "")
endif()

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/analyze_create_reference_signal.py
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_SOURCE_DIR}/analyze_create_reference_signal.py
            ${CMAKE_CURRENT_BINARY_DIR}/analyze_create_reference_signal.py
    COMMAND ${CHMOD_COMMAND}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/analyze_create_reference_signal.py
    COMMENT "Copying and setting executable permission on analyze_create_reference_signal.py"
)

# Create a custom target that triggers both copy commands
add_custom_target(ezgmsk_demod_sandbox_copy_assets ALL
    DEPENDS
        ${CMAKE_CURRENT_BINARY_DIR}/analyze_create_reference_signal.py
)

# Make your executable depend on this custom target
add_dependencies(create_reference_signal ezgmsk_demod_sandbox_copy_assets)