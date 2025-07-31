add_executable(perf_simple_linear_flow perf_simple_linear_flow.cpp)
target_link_libraries(perf_simple_linear_flow cler::cler_desktop_blocks)

add_executable(perf_fanout_workloads perf_fanout_workloads.cpp)
target_link_libraries(perf_fanout_workloads cler::cler_desktop_blocks)

add_executable(perf_read_write_techniques perf_read_write_techniques.cpp)
target_link_libraries(perf_read_write_techniques cler::cler_desktop_blocks)

if(UNIX)
  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/cpugov.sh
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/cpugov.sh ${CMAKE_CURRENT_BINARY_DIR}/cpugov.sh
    COMMAND chmod +x ${CMAKE_CURRENT_BINARY_DIR}/cpugov.sh
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cpugov.sh
    COMMENT "Copying and making cpugov.sh executable"
  )

  add_custom_target(copy_cpugov ALL
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/cpugov.sh
  )
endif()

add_subdirectory(compare_futuresdr)