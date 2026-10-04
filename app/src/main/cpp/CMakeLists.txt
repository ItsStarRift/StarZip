cmake_minimum_required(VERSION 3.22.1)
project(starzip_native C CXX)
set(CMAKE_CXX_STANDARD 17)
set(SZ ${CMAKE_CURRENT_SOURCE_DIR}/7zip)

add_library(starzip_native SHARED native-lib.cpp storage.cpp)
target_include_directories(starzip_native PRIVATE ${SZ}/C ${SZ}/CPP)
target_link_libraries(starzip_native log android)
