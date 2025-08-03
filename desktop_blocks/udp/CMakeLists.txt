add_library(blocks_udp STATIC
    sink_udp.cpp
    source_udp.cpp
    shared.cpp
)
add_library(cler::blocks_udp ALIAS blocks_udp)
target_link_libraries(blocks_udp PUBLIC cler::blocks_blob)