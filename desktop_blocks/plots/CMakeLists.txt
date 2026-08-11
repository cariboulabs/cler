add_library(blocks_plot_timeseries STATIC plot_timeseries.cpp)
add_library(cler::blocks_plot_timeseries ALIAS blocks_plot_timeseries)
target_link_libraries(blocks_plot_timeseries PUBLIC cler::blocks_gui)
target_include_directories(blocks_plot_timeseries PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(blocks_plots INTERFACE)
add_library(cler::blocks_plots ALIAS blocks_plots)
target_link_libraries(blocks_plots INTERFACE cler::blocks_plot_timeseries)

if(CLER_BUILD_BLOCKS_LIQUID)
  add_library(blocks_plots_spectral STATIC
    plot_cspectrum.cpp
    plot_cspectrogram.cpp
  )
  add_library(cler::blocks_plots_spectral ALIAS blocks_plots_spectral)
  target_link_libraries(blocks_plots_spectral PUBLIC cler::blocks_gui cler::blocks_liquid)
  target_include_directories(blocks_plots_spectral PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(blocks_plots INTERFACE cler::blocks_plots_spectral)
else()
  message(STATUS "Skipping cspectrum and cspectrogram plots because CLER_BUILD_BLOCKS_LIQUID is OFF")
endif()
