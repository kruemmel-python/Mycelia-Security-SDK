cmake_minimum_required(VERSION 3.18.1)

project(mycelia_native LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(mycelia_native SHARED
    mycelia_jni.cpp
    mycelia_vulkan_compute.cpp
)

target_include_directories(mycelia_native PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(mycelia_native
    android
    log
    vulkan
)
