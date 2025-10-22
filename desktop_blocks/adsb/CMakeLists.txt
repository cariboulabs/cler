# ADSB blocks - requires libmodes
add_subdirectory(libmodes)

# ADSB blocks are header-only but depend on libmodes
add_library(cler_blocks_adsb INTERFACE)
add_library(cler::blocks_adsb ALIAS cler_blocks_adsb)

target_link_libraries(cler_blocks_adsb INTERFACE
    cler::cler
    cler::libmodes
)

target_include_directories(cler_blocks_adsb INTERFACE
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
)
