cmake_minimum_required(VERSION 3.10)
project(s10carplay CXX)
set(CMAKE_CXX_STANDARD 17)
set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/..)
add_library(s10carplay SHARED
  ${SRC}/src/iap2_link.cpp ${SRC}/src/bplist.cpp ${SRC}/src/airplay.cpp ${SRC}/src/airplay_client.cpp
  ${SRC}/src/airplay_audio.cpp ${SRC}/src/session_runner.cpp ${SRC}/src/crypto_portable.cpp
  jni/s10carplay_jni.cpp)
target_include_directories(s10carplay PRIVATE ${SRC}/include)
target_link_libraries(s10carplay log)
