cmake_minimum_required(VERSION 3.20)

project("roblox-dumper" VERSION 2.7 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

configure_file(
    "${PROJECT_SOURCE_DIR}/src/config.h.in"
    "${PROJECT_BINARY_DIR}/config.h"
)

include(FetchContent)

set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
if(APPLE)
    set(SPDLOG_ENABLE_PCH OFF CACHE BOOL "" FORCE)
else()
    set(SPDLOG_ENABLE_PCH ON CACHE BOOL "" FORCE)
endif()

set(BUILD_CPR_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(CURL_STATICLIB ON CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
set(USE_LIBIDN2 OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_LDAP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_LDAPS ON CACHE BOOL "" FORCE)
set(USE_NGHTTP2 OFF CACHE BOOL "" FORCE)

if(WIN32)
    set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
else()
    set(CURL_USE_SCHANNEL OFF CACHE BOOL "" FORCE)
endif()

if(APPLE)
    add_compile_definitions(FMT_USE_CONSTEVAL=0)
endif()

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.15.1
)

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
)

FetchContent_Declare(
    cpr
    GIT_REPOSITORY https://github.com/libcpr/cpr.git
    GIT_TAG 1.10.5
)

FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.15.3
)

FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)

FetchContent_Declare(
    zydis
    GIT_REPOSITORY https://github.com/zyantific/zydis.git
    GIT_TAG v4.1.1
)

if(APPLE)
    FetchContent_Declare(
        capstone
        GIT_REPOSITORY https://github.com/capstone-engine/capstone.git
        GIT_TAG 5.0.3
    )
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(CAPSTONE_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(CAPSTONE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(CAPSTONE_ARCHITECTURE "aarch64" CACHE STRING "" FORCE)
endif()

if(APPLE)
    FetchContent_MakeAvailable(spdlog glm cpr httplib json zydis capstone)
else()
    FetchContent_MakeAvailable(spdlog glm cpr httplib json zydis)
endif()

include_directories(src src/dumper ${PROJECT_BINARY_DIR})

file(GLOB_RECURSE SOURCES "src/*.cpp" "src/*.cxx" "src/*.cc")

add_executable(roblox-dumper ${SOURCES})
target_compile_definitions(roblox-dumper PRIVATE FMT_USE_CONSTEVAL=0)
target_link_libraries(roblox-dumper PRIVATE spdlog::spdlog glm::glm cpr::cpr httplib::httplib
                                              nlohmann_json::nlohmann_json Zydis)

if(MSVC)
    set_property(TARGET roblox-dumper PROPERTY MSVC_RUNTIME_LIBRARY
                                               "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(APPLE)
    target_link_libraries(roblox-dumper PRIVATE capstone)
    target_link_libraries(
        roblox-dumper
        PRIVATE "-framework ApplicationServices" "-framework CoreGraphics"
                "-framework CoreFoundation")

    add_custom_command(
        TARGET roblox-dumper
        POST_BUILD
        COMMAND codesign -s - --entitlements "${PROJECT_SOURCE_DIR}/entitlements.plist"
                --force "$<TARGET_FILE:roblox-dumper>"
        COMMENT "Ad-hoc signing roblox-dumper with debugger entitlements")
endif()
