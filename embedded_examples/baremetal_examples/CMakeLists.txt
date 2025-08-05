add_executable(bm_hello_world
    bm_hello_world.cpp
)
target_link_libraries(bm_hello_world cler::cler)

# Optional: Create a test target
add_custom_target(bm_test_compilation 
    DEPENDS bm_hello_world
    COMMENT "Testing bare metal example compilation"
)