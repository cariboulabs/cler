# Kaiser LPF and other filters - header-only wrappers around liquid-dsp
add_library(blocks_filters INTERFACE)
add_library(cler::blocks_filters ALIAS blocks_filters)

target_link_libraries(blocks_filters INTERFACE liquid)
