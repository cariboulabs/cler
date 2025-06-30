#Basic Examples!
add_executable(streamlined streamlined.cpp)
add_executable(flowgraph flowgraph.cpp)

#GUI Examples!
add_executable(freqplot freqplot.cpp)
target_link_libraries(freqplot cler_gui)
