# runtime-switchable analog demodulator (liquid)
add_library(blocks_demod INTERFACE)
add_library(cler::blocks_demod ALIAS blocks_demod)
target_link_libraries(blocks_demod INTERFACE cler::blocks_liquid)
