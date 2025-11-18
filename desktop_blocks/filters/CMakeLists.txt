# Kaiser LPF and decimating filters - header-only wrappers around liquid-dsp
# Includes:
#   - kaiser_lpf.hpp: Low-pass filter (no decimation)
#   - kaiser_decim_lpf.hpp: Decimating low-pass filter (filter + downsample)
add_library(blocks_filters INTERFACE)
add_library(cler::blocks_filters ALIAS blocks_filters)

target_link_libraries(blocks_filters INTERFACE liquid)
