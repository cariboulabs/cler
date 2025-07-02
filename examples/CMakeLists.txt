include_directories(
  ${PROJECT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/include
  ${LIQUID_INCLUDE_DIRS}/blocks/gui
)

add_executable(streamlined streamlined.cpp)
add_executable(flowgraph flowgraph.cpp)

add_executable(polyphase_channelizer polyphase_channelizer.cpp)
target_link_libraries(polyphase_channelizer PRIVATE liquid cler_gui)

add_executable(freqplot freqplot.cpp)
target_link_libraries(freqplot cler_gui liquid)
