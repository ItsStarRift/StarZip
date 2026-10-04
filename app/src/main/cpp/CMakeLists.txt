cmake_minimum_required(VERSION 3.22.1)
project(starzip_native C CXX)
set(CMAKE_CXX_STANDARD 17)
set(SZ ${CMAKE_CURRENT_SOURCE_DIR}/7zip)
include(${CMAKE_CURRENT_SOURCE_DIR}/sources.cmake)

add_library(sevenzip SHARED ${SZ_SOURCES})
target_include_directories(sevenzip PRIVATE ${SZ}/C ${SZ}/CPP ${SZ}/CPP/7zip/Bundles/Format7zF)
target_compile_definitions(sevenzip PRIVATE
    Z7_EXTERNAL_CODECS _REENTRANT _FILE_OFFSET_BITS=64 _LARGEFILE_SOURCE NDEBUG)
target_compile_options(sevenzip PRIVATE -O2 -fPIC)
target_link_libraries(sevenzip log)

add_library(starzip_native SHARED native-lib.cpp storage.cpp)
target_link_libraries(starzip_native sevenzip log android)
