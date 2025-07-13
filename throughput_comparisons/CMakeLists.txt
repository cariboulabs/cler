include_directories(
  ${PROJECT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/include
)

add_executable(cler_throughput cler_throughput.cpp)
target_link_libraries(cler_throughput)

if(UNIX)
  set(CHMOD_COMMAND chmod +x ${CMAKE_CURRENT_BINARY_DIR}/futuresdr.sh)
else()
  set(CHMOD_COMMAND ${CMAKE_COMMAND} -E true)  # always safe fallback
endif()

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/futuresdr.sh
  COMMAND ${CMAKE_COMMAND} -E copy
          ${CMAKE_CURRENT_SOURCE_DIR}/futuresdr.sh
          ${CMAKE_CURRENT_BINARY_DIR}/futuresdr.sh
  COMMAND ${CHMOD_COMMAND}
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/futuresdr.sh
  COMMENT "Copying and conditionally chmod +x futuresdr.sh"
)

add_custom_target(copy_futuresdr ALL
  DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/futuresdr.sh
)

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