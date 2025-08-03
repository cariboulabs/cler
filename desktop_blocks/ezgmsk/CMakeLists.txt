add_library(blocks_ezgmsk_demod _ezgmsk_demod.c)
add_library(cler::blocks_ezgmsk_demod ALIAS blocks_ezgmsk_demod)

target_link_libraries(blocks_ezgmsk_demod PUBLIC liquid)
target_include_directories(blocks_ezgmsk_demod PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_library(blocks_ezgmsk_mod _ezgmsk_mod.c)
add_library(cler::blocks_ezgmsk_mod ALIAS blocks_ezgmsk_mod)

target_link_libraries(blocks_ezgmsk_mod PUBLIC liquid)
target_include_directories(blocks_ezgmsk_mod PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
)