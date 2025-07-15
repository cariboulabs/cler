add_library(blocks_udp STATIC
    sink_udp.cpp
    source_udp.cpp
    shared.cpp
)
target_link_libraries(blocks_udp PUBLIC cler)
target_include_directories(blocks_udp PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)