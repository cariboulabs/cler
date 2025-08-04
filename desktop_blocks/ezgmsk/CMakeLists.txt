add_library(blocks_ezgmsk _ezgmsk_demod.c _ezgmsk_mod.c)
add_library(cler::blocks_ezgmsk ALIAS blocks_ezgmsk)
target_link_libraries(blocks_ezgmsk PUBLIC liquid)
