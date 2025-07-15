add_executable(hello_world hello_world.cpp)
target_link_libraries(hello_world PRIVATE cler::cler_blocks)

add_executable(streamlined streamlined.cpp)
target_link_libraries(streamlined PRIVATE cler::cler)

add_executable(flowgraph flowgraph.cpp)
target_link_libraries(flowgraph PRIVATE cler::cler)

add_executable(plots plots.cpp)
target_link_libraries(plots PRIVATE cler::cler_blocks)

add_subdirectory(ezgmsk_demod)

add_executable(mass_spring_damper mass_spring_damper.cpp)
target_link_libraries(mass_spring_damper PRIVATE cler::cler_blocks)

add_executable(udp udp.cpp)
target_link_libraries(udp PRIVATE cler::cler_blocks)

add_executable(polyphase_channelizer polyphase_channelizer.cpp)
target_link_libraries(polyphase_channelizer PRIVATE cler::cler_blocks)

if (CARIBOULITE_FOUND)
    add_executable(cariboulite_receiver cariboulite_receiver.cpp)
    target_link_libraries(cariboulite_receiver PRIVATE cler::cler_blocks)
endif()


