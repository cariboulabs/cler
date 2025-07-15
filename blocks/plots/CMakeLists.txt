add_library(blocks_plots STATIC
    plot_cspectrum.cpp
    plot_cspectrogram.cpp
    plot_timeseries.cpp
)
target_link_libraries(blocks_plots PUBLIC cler liquid blocks_gui)
target_include_directories(blocks_plots PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
)