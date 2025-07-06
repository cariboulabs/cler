add_library(ezgmsk_demod _ezgmsk_demod.c)

target_link_libraries(ezgmsk_demod PUBLIC liquid)
target_include_directories(ezgmsk_demod PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    SYSTEM ${liquid_SOURCE_DIR}/include #system ignore warnings from liquid
    SYSTEM ${liquid_SOURCE_DIR}/scripts #for config.h
)