# ADSB blocks - includes mode-s decoder (2.0 MSPS and 2.4 MSPS) and CPR decoder
add_library(cler_blocks_adsb STATIC modes.c modes_2400.c cpr.c)
add_library(cler::blocks_adsb ALIAS cler_blocks_adsb)

target_link_libraries(cler_blocks_adsb PUBLIC
    cler::cler
)

target_include_directories(cler_blocks_adsb PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
)

# modes.c and cpr.c are C code
set_target_properties(cler_blocks_adsb PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
)
