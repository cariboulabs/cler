add_subdirectory(zf_log)

add_library(cler_logger STATIC logger.cpp)
add_library(cler::logger ALIAS cler_logger)

target_include_directories(cler_logger PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} zf_log)

target_link_libraries(cler_logger PRIVATE zf_log)