include_directories(
  ${PROJECT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/include
  SYSTEM ${liquid_SOURCE_DIR}/include #system ignore warnings from liquid
)

add_executable(streamlined streamlined.cpp)
add_executable(flowgraph flowgraph.cpp)

add_executable(timeseries timeseries.cpp)
target_link_libraries(timeseries cler_gui)

# add_executable(polyphase_channelizer polyphase_channelizer.cpp)
# target_link_libraries(polyphase_channelizer PRIVATE liquid cler_gui)

# add_executable(freqplot freqplot.cpp)
# target_link_libraries(freqplot cler_gui liquid)
