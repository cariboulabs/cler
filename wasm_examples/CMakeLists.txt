# WASM Examples - Streamlined Mode Only
# Build with: emcmake cmake .. -DCMAKE_BUILD_TYPE=Release && emmake make

# Verify Emscripten toolchain
if(NOT DEFINED EMSCRIPTEN)
    message(FATAL_ERROR "Emscripten toolchain required. Use: emcmake cmake ..")
endif()

message(STATUS "Building WASM streamlined examples with Emscripten")

# WASM link flags with function exports
set(WASM_LINK_FLAGS
    "-sWASM=1"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sNO_EXIT_RUNTIME=1"
    "-sEXPORTED_FUNCTIONS=['_main','_start_processing','_stop_processing']"
    "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap']"
)

# Function to create a WASM example
function(add_wasm_example NAME SOURCE_FILE)
    add_executable(${NAME} ${SOURCE_FILE})
    
    target_include_directories(${NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
    
    # Apply WASM link flags
    target_link_options(${NAME} PRIVATE ${WASM_LINK_FLAGS})
    
    # Generate HTML page
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/template.html
        ${CMAKE_CURRENT_BINARY_DIR}/${NAME}.html
        @ONLY
    )
endfunction()

# Add streamlined throughput example
add_wasm_example(streamlined_throughput streamlined_throughput.cpp)