include(FetchContent)
set(USE_TLS OFF CACHE BOOL "" FORCE)
set(USE_ZLIB OFF CACHE BOOL "" FORCE)
set(IXWEBSOCKET_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF)
FetchContent_Declare(ixwebsocket
  GIT_REPOSITORY https://github.com/machinezone/IXWebSocket
  GIT_TAG        v11.4.5
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(ixwebsocket)

add_library(blocks_web STATIC web_server.cpp)
add_library(cler::blocks_web ALIAS blocks_web)
target_link_libraries(blocks_web PUBLIC cler::blocks_core ixwebsocket)
