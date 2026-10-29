cmake_minimum_required(VERSION 3.25)

option(OPENWIND_BUILD_TESTS "Build the existing CPU wind tests" OFF)
if(OPENWIND_BUILD_TESTS)
    list(APPEND VCPKG_MANIFEST_FEATURES tests)
endif()

project(OpenWind VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Open Wind requires a Windows x64 toolchain.")
endif()

set(OPENWIND_COMMONLIB_SOURCE
    ""
    CACHE PATH
    "Existing CommonLibVR source checkout"
)
set(BUILD_TESTS OFF CACHE BOOL "Build CommonLib tests" FORCE)
set(ENABLE_SKYRIM_SE ON CACHE BOOL "Support Skyrim SE" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "Support Skyrim AE" FORCE)
set(ENABLE_SKYRIM_VR ON CACHE BOOL "Support Skyrim VR" FORCE)

if(OPENWIND_COMMONLIB_SOURCE)
    add_subdirectory(
        "${OPENWIND_COMMONLIB_SOURCE}"
        "${CMAKE_CURRENT_BINARY_DIR}/CommonLibSSE"
        EXCLUDE_FROM_ALL
    )
    include("${OPENWIND_COMMONLIB_SOURCE}/cmake/CommonLibSSE.cmake")
else()
    include(FetchContent)
    FetchContent_Declare(
        CommonLibSSE
        GIT_REPOSITORY https://github.com/alandtse/CommonLibVR.git
        GIT_TAG 8ffc2b74af015f837de53cc02b0cb470a15678e4
        GIT_SUBMODULES extern/openvr
        GIT_SUBMODULES_RECURSE FALSE
    )
    FetchContent_MakeAvailable(CommonLibSSE)
    set_property(
        DIRECTORY "${commonlibsse_SOURCE_DIR}"
        PROPERTY EXCLUDE_FROM_ALL TRUE
    )
    include("${commonlibsse_SOURCE_DIR}/cmake/CommonLibSSE.cmake")
endif()

option(OPENWIND_BUILD_HDT_BRIDGE "Build the optional HDT-SMP bridge" ON)
option(OPENWIND_BUILD_DYNAMIC_WIND_BRIDGE "Build the optional Dynamic Wind bridge" ON)

find_package(nlohmann_json CONFIG REQUIRED)
find_package(directxtk CONFIG REQUIRED)

file(GLOB_RECURSE OPENWIND_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
list(FILTER OPENWIND_SOURCES EXCLUDE REGEX "/Integrations/(HDT|DynamicWind)/|/Integrations/BridgeRuntime[.]cpp$")
add_commonlibsse_plugin(OpenWind NAME "OpenWind" AUTHOR "Open Wind contributors" SOURCES ${OPENWIND_SOURCES})
set(OPENWIND_TARGETS OpenWind)
set(OPENWIND_COMPONENTS Runtime)

if(OPENWIND_BUILD_HDT_BRIDGE)
    file(GLOB_RECURSE OPENWIND_HDT_SOURCES CONFIGURE_DEPENDS "src/Integrations/HDT/*.cpp")
    add_commonlibsse_plugin(OpenWindHDT NAME "OpenWindHDT" AUTHOR "Open Wind contributors"
        SOURCES ${OPENWIND_HDT_SOURCES} src/Integrations/BridgeRuntime.cpp
    )
    list(APPEND OPENWIND_TARGETS OpenWindHDT)
    list(APPEND OPENWIND_COMPONENTS HDTBridge)
endif()

if(OPENWIND_BUILD_DYNAMIC_WIND_BRIDGE)
    file(GLOB_RECURSE OPENWIND_DYNAMIC_WIND_SOURCES CONFIGURE_DEPENDS "src/Integrations/DynamicWind/*.cpp")
    add_commonlibsse_plugin(OpenWindDynamicWind NAME "OpenWindDynamicWind" AUTHOR "Open Wind contributors"
        SOURCES ${OPENWIND_DYNAMIC_WIND_SOURCES} src/Integrations/BridgeRuntime.cpp
    )
    list(APPEND OPENWIND_TARGETS OpenWindDynamicWind)
    list(APPEND OPENWIND_COMPONENTS DynamicWindBridge)
endif()

foreach(target IN LISTS OPENWIND_TARGETS)
    target_compile_features(${target} PRIVATE cxx_std_23)
    target_compile_definitions(
        ${target}
        PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
    )
    target_include_directories(${target} PRIVATE include src src/Runtime extern)
    target_precompile_headers(${target} PRIVATE src/PCH.h)
    target_link_libraries(
        ${target}
        PRIVATE nlohmann_json::nlohmann_json Microsoft::DirectXTK
    )

    if(MSVC)
        target_compile_options(
            ${target}
            PRIVATE /W4 /permissive- /Zc:__cplusplus /Zc:preprocessor
        )
    endif()
endforeach()

if(OPENWIND_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX
        "${CMAKE_CURRENT_BINARY_DIR}/package"
        CACHE PATH
        "Open Wind package destination"
        FORCE
    )
endif()

install(TARGETS OpenWind RUNTIME DESTINATION SKSE/Plugins COMPONENT Runtime)
install(
    FILES README.md COPYING NOTICE.md
    DESTINATION SKSE/Plugins/OpenWind
    COMPONENT Runtime
)
if(OPENWIND_BUILD_HDT_BRIDGE)
    install(TARGETS OpenWindHDT RUNTIME DESTINATION SKSE/Plugins COMPONENT HDTBridge)
    install(
        FILES extern/FSMP/README.md extern/FSMP/COPYING
        DESTINATION SKSE/Plugins/OpenWind/licenses/FSMP
        COMPONENT HDTBridge
    )
endif()
if(OPENWIND_BUILD_DYNAMIC_WIND_BRIDGE)
    install(TARGETS OpenWindDynamicWind RUNTIME DESTINATION SKSE/Plugins COMPONENT DynamicWindBridge)
    install(
        FILES extern/DynamicWind/README.md
        DESTINATION SKSE/Plugins/OpenWind/licenses/DynamicWind
        COMPONENT DynamicWindBridge
    )
endif()
foreach(bridge HDT DynamicWind)
    if(TARGET OpenWind${bridge})
        install(
            FILES README.md COPYING NOTICE.md
            DESTINATION SKSE/Plugins/OpenWind/licenses/OpenWind${bridge}
            COMPONENT ${bridge}Bridge
        )
    endif()
endforeach()
install(
    DIRECTORY include/OpenWind
    DESTINATION include
    COMPONENT SDK
    EXCLUDE_FROM_ALL
    FILES_MATCHING PATTERN "*.h"
)

set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_NAME "OpenWind")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "OpenWind-${PROJECT_VERSION}")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
set(CPACK_COMPONENTS_GROUPING IGNORE)
set(CPACK_COMPONENTS_ALL ${OPENWIND_COMPONENTS})
set(CPACK_ARCHIVE_RUNTIME_FILE_NAME "OpenWind-${PROJECT_VERSION}")
set(CPACK_ARCHIVE_HDTBRIDGE_FILE_NAME "OpenWindHDT-${PROJECT_VERSION}")
set(CPACK_ARCHIVE_DYNAMICWINDBRIDGE_FILE_NAME "OpenWindDynamicWind-${PROJECT_VERSION}")
include(CPack)
