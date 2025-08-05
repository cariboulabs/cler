include_directories(${CMAKE_CURRENT_SOURCE_DIR}/mock_environment)

# FreeRTOS hello world compilation test
add_executable(frts_hello_world
    frts_hello_world.cpp
)
target_link_libraries(frts_hello_world cler::cler)

# Optional: Create a test target
add_custom_target(frts_test_compilation 
    DEPENDS frts_hello_world
    COMMENT "Testing FreeRTOS example compilation"
)