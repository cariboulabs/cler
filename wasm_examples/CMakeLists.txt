cmake_minimum_required(VERSION 3.16)
project(cler_wasm_examples
        VERSION 0.1.0
        LANGUAGES CXX C
        )

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
add_compile_options(-Wall -Wextra #mainly disable warnings for liquid. but need a better way of this doing
      -Wno-unused-variable 
      -Wno-missing-braces 
      -Wno-unused-function 
      -Wno-unused-parameter
      -Wno-deprecated-declarations
      -Wno-sign-compare
      -Wno-implicit-fallthrough
      -UNDEBUG
      )
message(STATUS "Asserts are ENABLED even in Release builds")

# Verify Emscripten toolchain
if(NOT DEFINED EMSCRIPTEN)
    message(FATAL_ERROR "Emscripten toolchain required. Use: emcmake cmake ..")
endif()

message(STATUS "Building WASM streamlined examples with Emscripten")

# Fetch ImGui and ImPlot for GUI examples
include(FetchContent)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG docking
)

FetchContent_Declare(
    implot
    GIT_REPOSITORY https://github.com/epezent/implot.git
    GIT_TAG master
)

FetchContent_MakeAvailable(imgui implot)

# WASM link flags with function exports
set(WASM_LINK_FLAGS
    "-sWASM=1"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sNO_EXIT_RUNTIME=1"
    "-sEXPORTED_FUNCTIONS=['_main','_start_processing','_stop_processing']"
    "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap']"
)

# GUI-specific WASM link flags (for examples with ImGui)
set(WASM_GUI_LINK_FLAGS
    ${WASM_LINK_FLAGS}
    "-sUSE_GLFW=3"
    "-sUSE_WEBGL2=1" 
    "-sFULL_ES3=1"
    "-sLLD_REPORT_UNDEFINED"
)

# Function to create a WASM example
function(add_wasm_example NAME SOURCE_FILE)
    add_executable(${NAME} ${SOURCE_FILE})
    
    target_include_directories(${NAME} PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../desktop_blocks/gui)
    
    # Apply WASM link flags
    target_link_options(${NAME} PRIVATE ${WASM_LINK_FLAGS})
    
    # Generate HTML page
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/template.html
        ${CMAKE_CURRENT_BINARY_DIR}/${NAME}.html
        @ONLY
    )
endfunction()

# Function to create a WASM GUI example with ImGui
function(add_wasm_gui_example NAME SOURCE_FILE)
    add_executable(${NAME} 
        ${SOURCE_FILE}
        # Include GUI manager source directly
        ${CMAKE_CURRENT_SOURCE_DIR}/../desktop_blocks/gui/gui_manager.cpp
        # ImGui core files
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        # ImGui backends for WASM
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
        # ImPlot core files
        ${implot_SOURCE_DIR}/implot.cpp
        ${implot_SOURCE_DIR}/implot_items.cpp
    )
    
    # Include directories
    target_include_directories(${NAME} PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../desktop_blocks/gui
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
        ${implot_SOURCE_DIR}
    )
    
    # Apply GUI WASM link flags
    target_link_options(${NAME} PRIVATE ${WASM_GUI_LINK_FLAGS})
    
    # Generate HTML page
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/template.html
        ${CMAKE_CURRENT_BINARY_DIR}/${NAME}.html
        @ONLY
    )
endfunction()

# Add examples
add_wasm_example(streamlined_throughput streamlined_throughput.cpp)
add_wasm_gui_example(hello_world_gui hello_world_gui.cpp)