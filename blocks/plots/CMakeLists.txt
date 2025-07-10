add_library(blocks_plots STATIC
    plot_cspectrum.cpp
    plot_cspectrogram.cpp
    plot_timeseries.cpp
)

target_link_libraries(blocks_plots PUBLIC liquid blocks_gui)
target_include_directories(blocks_plots PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    SYSTEM ${liquid_SOURCE_DIR}/include #system ignore warnings from liquid
    SYSTEM ${liquid_SOURCE_DIR}/scripts #for config.h
)