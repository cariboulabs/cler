# FM demodulator block - header-only wrapper around liquid-dsp's freqdem
add_library(blocks_fm INTERFACE)
add_library(cler::blocks_fm ALIAS blocks_fm)

target_link_libraries(blocks_fm INTERFACE liquid)
