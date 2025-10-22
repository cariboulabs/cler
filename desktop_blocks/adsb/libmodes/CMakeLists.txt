# libmodes - Mode S decoder library
add_library(libmodes STATIC mode-s.c)
add_library(cler::libmodes ALIAS libmodes)

target_include_directories(libmodes PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

# libmodes is C code, no C++ requirements
set_target_properties(libmodes PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
)
