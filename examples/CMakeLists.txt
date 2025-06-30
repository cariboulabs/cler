add_executable(streamlined streamlined.cpp)
add_executable(flowgraph flowgraph.cpp)

add_executable(polyphase_channelizer polyphase_channelizer.cpp)
target_link_libraries(polyphase_channelizer liquid)
target_include_directories(polyphase_channelizer PRIVATE
  ${liquid_SOURCE_DIR}/include
)


add_executable(freqplot freqplot.cpp)
target_link_libraries(freqplot cler_gui)
