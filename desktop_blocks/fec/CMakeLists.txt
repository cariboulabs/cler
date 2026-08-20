# FEC and packet framing blocks - header-only wrappers around liquid-dsp's
# fec/flexframegen/flexframesync
add_library(blocks_fec INTERFACE)
add_library(cler::blocks_fec ALIAS blocks_fec)
target_link_libraries(blocks_fec INTERFACE cler::blocks_liquid)
