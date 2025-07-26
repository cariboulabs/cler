# WASM build configuration for Cler examples
# This file is included from the main CMakeLists.txt when CLER_BUILD_WASM_EXAMPLES=ON
# Note: Emscripten SDK should be set up via build_wasm.sh before running CMake

# Ensure we have the required libraries available
if(NOT TARGET cler::cler_desktop_blocks)
    message(FATAL_ERROR "cler::cler_desktop_blocks target not found. Make sure CLER_BUILD_BLOCKS_GUI=ON")
endif()

# Verify we're using Emscripten
if(NOT DEFINED EMSCRIPTEN)
    message(FATAL_ERROR "Emscripten not detected. Please run build_wasm.sh to set up the environment.")
endif()

message(STATUS "Building WASM examples with Emscripten: ${EMSCRIPTEN}")

# Emscripten link flags only (no compile flags needed)
set(WASM_LINK_FLAGS
    "-s USE_GLFW=3"
    "-s USE_WEBGL2=1" 
    "-s FULL_ES3=1"
    "-s WASM=1"
    "-s ALLOW_MEMORY_GROWTH=1"
    "-s NO_EXIT_RUNTIME=1"
    "-s USE_PTHREADS=1"
    "-s PTHREAD_POOL_SIZE=4"
    "-s PROXY_TO_PTHREAD=1"
    "-s ASSERTIONS=1"
    "-s EXPORTED_RUNTIME_METHODS=['ccall','cwrap']"
    "-s MODULARIZE=1"
    "-s EXPORT_NAME=ClerDemo"
    "--preload-file=${CMAKE_SOURCE_DIR}/wasm/assets@/assets"
)

find_package(Threads REQUIRED)

# Function to create a WASM demo from an existing desktop example
function(add_wasm_demo NAME SOURCE_FILE)
    # Create executable from the original source file with wasm_ prefix
    add_executable(wasm_${NAME} ${CMAKE_SOURCE_DIR}/desktop_examples/${SOURCE_FILE})
    
    # Link with Cler libraries
    target_link_libraries(wasm_${NAME} 
        PRIVATE cler::cler_desktop_blocks
        PRIVATE Threads::Threads
    )
    
    # Apply Emscripten flags only for linking
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