# APRS: AX.25 UI deframer/parser on top of the AIS HDLC layer (header-only),
# AFSK1200 demodulator block, station map (GUI)
add_library(blocks_aprs INTERFACE)
add_library(cler::blocks_aprs ALIAS blocks_aprs)
target_link_libraries(blocks_aprs INTERFACE cler::blocks_ais)

if (CLER_BUILD_BLOCKS_GUI)
  add_library(blocks_aprs_map INTERFACE)
  add_library(cler::blocks_aprs_map ALIAS blocks_aprs_map)
  target_link_libraries(blocks_aprs_map INTERFACE cler::blocks_aprs cler::blocks_gui)
endif()
