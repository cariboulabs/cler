# Digital modem blocks - header-only wrappers around liquid-dsp's modem/symsync/nco
add_library(blocks_linear_modem INTERFACE)
add_library(cler::blocks_linear_modem ALIAS blocks_linear_modem)
target_link_libraries(blocks_linear_modem INTERFACE cler::blocks_liquid)

if (CLER_BUILD_BLOCKS_GUI)
  add_library(blocks_linear_modem_plot INTERFACE)
  add_library(cler::blocks_linear_modem_plot ALIAS blocks_linear_modem_plot)
  target_link_libraries(blocks_linear_modem_plot INTERFACE cler::blocks_gui)
  target_link_libraries(blocks_linear_modem INTERFACE cler::blocks_linear_modem_plot)
endif()
