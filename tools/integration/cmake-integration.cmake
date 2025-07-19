# Cler Validator CMake Integration
#
# Include this file in your CMakeLists.txt:
#   include(tools/integration/cmake-integration.cmake)
#
# Or add the validation target manually using the code below

# Find Python interpreter
find_package(Python3 COMPONENTS Interpreter)

if(Python3_FOUND)
    # Set validator path
    set(CLER_VALIDATOR "${CMAKE_SOURCE_DIR}/tools/cler-validate.py")
    
    if(EXISTS ${CLER_VALIDATOR})
        # Create a custom target for validation
        add_custom_target(validate-cler
            COMMAND ${Python3_EXECUTABLE} ${CLER_VALIDATOR} 
                    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
                    ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Validating Cler flowgraphs..."
        )
        
        # Option to run validation before every build
        option(CLER_VALIDATE_ON_BUILD "Run Cler validation before building" OFF)
        
        if(CLER_VALIDATE_ON_BUILD)
            # Add validation as a pre-build step for all targets
            add_custom_command(
                TARGET ${PROJECT_NAME}
                PRE_BUILD
                COMMAND ${Python3_EXECUTABLE} ${CLER_VALIDATOR}
                        ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
                        ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
                        --werror  # Treat warnings as errors in build
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                COMMENT "Validating Cler flowgraphs..."
            )
        endif()
        
        # Create a validation test
        if(BUILD_TESTING)
            add_test(
                NAME cler_validation
                COMMAND ${Python3_EXECUTABLE} ${CLER_VALIDATOR}
                        ${CMAKE_SOURCE_DIR}/desktop_examples/*.cpp
                        ${CMAKE_SOURCE_DIR}/examples/*.cpp
                        --json
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            )
        endif()
        
        message(STATUS "Cler validator found - 'make validate-cler' available")
    else()
        message(STATUS "Cler validator not found at ${CLER_VALIDATOR}")
    endif()
else()
    message(STATUS "Python3 not found - Cler validation disabled")
endif()

# Function to add validation for specific targets
function(add_cler_validation TARGET_NAME)
    if(Python3_FOUND AND EXISTS ${CLER_VALIDATOR})
        add_custom_command(
            TARGET ${TARGET_NAME}
            PRE_BUILD
            COMMAND ${Python3_EXECUTABLE} ${CLER_VALIDATOR}
                    $<TARGET_PROPERTY:${TARGET_NAME},SOURCES>
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Validating Cler flowgraphs for ${TARGET_NAME}..."
        )
    endif()
endfunction()