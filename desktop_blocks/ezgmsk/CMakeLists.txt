add_library(blocks_ezgmsk_demod _ezgmsk_demod.c)
add_library(cler::blocks_ezgmsk_demod ALIAS blocks_ezgmsk_demod)


target_link_libraries(blocks_ezgmsk_demod PUBLIC liquid)
target_include_directories(blocks_ezgmsk_demod PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
)