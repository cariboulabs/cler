add_executable(streamlined streamlined.cpp)
add_executable(flowgraph flowgraph.cpp)

find_package(SDL2 REQUIRED)
add_executable(freqplot freqplot.cpp)
target_include_directories(freqplot PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(freqplot PRIVATE ${SDL2_LIBRARIES})