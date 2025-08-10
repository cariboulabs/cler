add_subdirectory(zf_log)

add_library(cler_desktop_logger STATIC desktop_logger.cpp)
add_library(cler::desktop_logger ALIAS cler_desktop_logger)

target_include_directories(cler_desktop_logger PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} zf_log)

target_link_libraries(cler_desktop_logger PRIVATE zf_log)