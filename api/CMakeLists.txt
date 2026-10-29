cmake_minimum_required(VERSION 3.25)

add_library(OpenWindSDK INTERFACE)
add_library(OpenWind::SDK ALIAS OpenWindSDK)
target_include_directories(
    OpenWindSDK
    SYSTEM
    INTERFACE "${CMAKE_CURRENT_LIST_DIR}/../include"
)
target_compile_features(OpenWindSDK INTERFACE cxx_std_20)
