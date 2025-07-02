add_executable(streamlined streamlined.cpp)
add_executable(flowgraph flowgraph.cpp)

add_executable(polyphase_channelizer polyphase_channelizer.cpp)
target_link_libraries(polyphase_channelizer liquid cler_gui)
target_include_directories(polyphase_channelizer PRIVATE
  ${liquid_INCLUDE_DIR}
)


add_executable(freqplot freqplot.cpp)
target_link_libraries(freqplot cler_gui fftw3f)
