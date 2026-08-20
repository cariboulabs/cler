# SigMF: metadata reader/writer plus source/sink blocks (header-only, no liquid)
add_library(blocks_sigmf INTERFACE)
add_library(cler::blocks_sigmf ALIAS blocks_sigmf)
target_link_libraries(blocks_sigmf INTERFACE cler::blocks_core)
