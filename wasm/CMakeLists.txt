# WASM build configuration for Cler examples
# This file is included from the main CMakeLists.txt when CLER_BUILD_WASM_EXAMPLES=ON
# Requires: emcmake cmake .. -DCLER_BUILD_WASM_EXAMPLES=ON -DCLER_BUILD_BLOCKS_GUI=ON

# Ensure we have the required libraries available
if(NOT TARGET cler::cler_desktop_blocks)
    message(FATAL_ERROR "cler::cler_desktop_blocks target not found. Make sure CLER_BUILD_BLOCKS_GUI=ON")
endif()

# Verify we're using Emscripten toolchain
if(NOT DEFINED EMSCRIPTEN)
    message(FATAL_ERROR "Emscripten toolchain not detected. Use: emcmake cmake .. -DCLER_BUILD_WASM_EXAMPLES=ON")
endif()

message(STATUS "Building WASM examples with Emscripten")
message(STATUS "Note: Using Emscripten's built-in GLFW/OpenGL (not desktop libraries)")

# Important reminder for users
message(STATUS "")
message(STATUS "===============================================")
message(STATUS "⚠️  IMPORTANT: Use 'emmake make' not 'make'")
message(STATUS "===============================================")
message(STATUS "After emcmake cmake, you MUST use:")
message(STATUS "  emmake make")
message(STATUS "Regular 'make' will fail with undefined symbols")
message(STATUS "===============================================")
message(STATUS "")

# Check if we're being built with emmake (EMMAKE_JUST_CONFIGURE is set by emmake)
# This helps catch the common mistake of using 'make' instead of 'emmake make'
if(NOT DEFINED EMMAKE_JUST_CONFIGURE AND NOT "$ENV{EMMAKE_JUST_CONFIGURE}" STREQUAL "")
    # We're in emmake context during build
    message(STATUS "✅ Building with emmake - good!")
endif()

# Set up emsdk via FetchContent if not available
if(NOT EXISTS "${CMAKE_BINARY_DIR}/_deps/emsdk-src/emsdk")
    include(FetchContent)
    FetchContent_Declare(
        emsdk
        GIT_REPOSITORY https://github.com/emscripten-core/emsdk.git
        GIT_TAG main
    )
    FetchContent_MakeAvailable(emsdk)
endif()

# Emscripten link flags following WebGUI pattern
# Note: Use Emscripten's built-in GLFW/OpenGL, not desktop libraries
set(WASM_LINK_FLAGS
    "-sUSE_GLFW=3"
    "-sUSE_WEBGL2=1" 
    "-sFULL_ES3=1"
    "-sWASM=1"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sNO_EXIT_RUNTIME=1"
    "-sUSE_PTHREADS=1"
    "-sPTHREAD_POOL_SIZE=4"
    "-sPROXY_TO_PTHREAD=1"
    "-sASSERTIONS=1"
    "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap']"
    "-sMODULARIZE=1"
    "-sEXPORT_NAME=ClerDemo"
    "-sERROR_ON_UNDEFINED_SYMBOLS=0"
    "-sLLD_REPORT_UNDEFINED"
    "-sFORCE_FILESYSTEM=1"
)

find_package(Threads REQUIRED)

# Function to create a WASM demo from an existing desktop example  
function(add_wasm_demo NAME SOURCE_FILE)
    # Create executable from the original source file with wasm_ prefix
    add_executable(wasm_${NAME} ${CMAKE_SOURCE_DIR}/desktop_examples/${SOURCE_FILE})
    
    # Link with Cler desktop blocks (includes all necessary libraries and include paths)
    target_link_libraries(wasm_${NAME} PRIVATE 
        cler::cler_desktop_blocks
        Threads::Threads
    )
    
    # Enable pthread support for both compile and link (required for shared memory)
    target_compile_options(wasm_${NAME} PRIVATE -pthread)
    target_link_options(wasm_${NAME} PRIVATE -pthread)
    
    # Apply Emscripten link flags following WebGUI pattern
    target_link_options(wasm_${NAME} PRIVATE ${WASM_LINK_FLAGS})
    
    # Generate HTML from template
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/html/demo_template.html
        ${CMAKE_SOURCE_DIR}/docs/demos/${NAME}/index.html
        @ONLY
    )
    
    # Post-build: copy generated files to docs
    add_custom_command(TARGET wasm_${NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory 
            ${CMAKE_SOURCE_DIR}/docs/demos/${NAME}
        COMMAND ${CMAKE_COMMAND} -E copy 
            $<TARGET_FILE:wasm_${NAME}>
            ${CMAKE_SOURCE_DIR}/docs/demos/${NAME}/demo.wasm
        COMMAND ${CMAKE_COMMAND} -E copy 
            $<TARGET_FILE_DIR:wasm_${NAME}>/wasm_${NAME}.js
            ${CMAKE_SOURCE_DIR}/docs/demos/${NAME}/demo.js
        COMMAND ${CMAKE_COMMAND} -E copy 
            $<TARGET_FILE_DIR:wasm_${NAME}>/wasm_${NAME}.worker.js
            ${CMAKE_SOURCE_DIR}/docs/demos/${NAME}/demo.worker.js
    )
endfunction()

# Add demos - referencing original source files
add_wasm_demo(mass_spring_damper mass_spring_damper.cpp)
add_wasm_demo(hello_world hello_world.cpp)

# Generate gallery page
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/html/demo_gallery.html
    ${CMAKE_SOURCE_DIR}/docs/demos/index.html
    @ONLY
)