include_directories(
  ${PROJECT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/include
)

add_executable(cler_throughput cler_throughput.cpp)
target_link_libraries(cler_throughput)
