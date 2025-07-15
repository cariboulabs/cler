add_subdirectory(zf_log)

add_library(logger STATIC logger.c
)
target_include_directories(logger PRIVATE zf_log)
target_include_directories(logger PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(logger PRIVATE zf_log)
