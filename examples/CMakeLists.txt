include_directories(
  ${PROJECT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/include
  SYSTEM ${liquid_SOURCE_DIR}/include #system ignore warnings from liquid
  SYSTEM ${zf_log_SOURCE_DIR}/zf_log
)

add_executable(streamlined streamlined.cpp)
add_executable(flowgraph flowgraph.cpp)

add_executable(plots plots.cpp)
target_link_libraries(plots cler_gui liquid)

add_subdirectory(ezgmsk_demod)

# add_executable(polyphase_channelizer polyphase_channelizer.cpp)
# target_link_libraries(polyphase_channelizer PRIVATE liquid cler_gui)

# add_executable(freqplot freqplot.cpp)
# target_link_libraries(freqplot cler_gui liquid)
