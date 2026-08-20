# AIS: bit-level deframer/parser (header-only), GMSK decoder block (liquid),
# ship map (GUI)
add_library(blocks_ais INTERFACE)
add_library(cler::blocks_ais ALIAS blocks_ais)
target_link_libraries(blocks_ais INTERFACE cler::blocks_liquid)

if (CLER_BUILD_BLOCKS_GUI)
  add_library(blocks_ais_map INTERFACE)
  add_library(cler::blocks_ais_map ALIAS blocks_ais_map)
  target_link_libraries(blocks_ais_map INTERFACE cler::blocks_ais cler::blocks_gui)
endif()
