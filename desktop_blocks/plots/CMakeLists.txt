add_library(blocks_plots STATIC
    plot_timeseries.cpp
)

if (CLER_BUILD_BLOCKS_LIQUID)
    target_sources(blocks_plots PRIVATE
        plot_cspectrum.cpp
        plot_cspectrogram.cpp
    )
else()
    message(STATUS "Skipping cspectrum and cspectrogram plots because CLER_BUILD_BLOCKS_LIQUID is OFF")
endif()

add_library(cler::blocks_plots ALIAS blocks_plots)
target_link_libraries(blocks_plots PUBLIC cler::cler liquid cler::blocks_gui)
target_include_directories(blocks_plots PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
)