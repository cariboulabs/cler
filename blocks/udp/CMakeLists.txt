add_library(blocks_udp STATIC
    sink_udp.cpp
    source_udp.cpp
    shared.cpp
)
target_include_directories(blocks_udp PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)