cmake_minimum_required(VERSION 3.22)

project(SDLAgent C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Third-party sources and the arm64 prefix live outside the repository.
# Default: <repo>/../android_deps
if(DEFINED ENV{SDLAGENT_DEPS})
    set(SDLAGENT_DEPS "$ENV{SDLAGENT_DEPS}")
else()
    get_filename_component(SDLAGENT_DEPS
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../android_deps" ABSOLUTE)
endif()

get_filename_component(SDLAGENT_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(SDLAGENT_PREFIX "${SDLAGENT_DEPS}/prefix/${ANDROID_ABI}")

message(STATUS "SDLAGENT_ROOT   = ${SDLAGENT_ROOT}")
message(STATUS "SDLAGENT_DEPS   = ${SDLAGENT_DEPS}")
message(STATUS "SDLAGENT_PREFIX = ${SDLAGENT_PREFIX}")

# ---------------------------------------------------------------- SDL3 (shared)
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${SDLAGENT_DEPS}/SDL" "${CMAKE_BINARY_DIR}/SDL3")

# ------------------------------------------------------------ SDL3_ttf (shared)
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
set(SDLTTF_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDLTTF_INSTALL OFF CACHE BOOL "" FORCE)
set(SDLTTF_PLUTOSVG OFF CACHE BOOL "" FORCE)
set(SDLTTF_HARFBUZZ ON CACHE BOOL "" FORCE)
add_subdirectory("${SDLAGENT_DEPS}/SDL_ttf" "${CMAKE_BINARY_DIR}/SDL3_ttf")

# --------------------------------------------------------------------- cJSON
# The project includes <cjson/cJSON.h>; stage the header in that layout.
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/include/cjson")
configure_file("${SDLAGENT_DEPS}/cjson/cJSON.h"
               "${CMAKE_BINARY_DIR}/include/cjson/cJSON.h" COPYONLY)

# ---------------------------------------------------------------------- base64
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BASE64_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(BASE64_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BASE64_WITH_OpenMP OFF CACHE BOOL "" FORCE)
set(BASE64_WERROR OFF CACHE BOOL "" FORCE)
add_subdirectory("${SDLAGENT_ROOT}/third_party/base64"
                 "${CMAKE_BINARY_DIR}/base64")
set_target_properties(base64 PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------- curl
set(CURL_INCLUDE_DIR "${SDLAGENT_PREFIX}/include")
set(CURL_LIBRARY "${SDLAGENT_PREFIX}/lib/libcurl.a")
find_package(CURL REQUIRED)

# ----------------------------------------------------------------------- app
file(GLOB SDLAGENT_SOURCES CONFIGURE_DEPENDS
     "${SDLAGENT_ROOT}/src/*.cc")

# Embedded assets are produced by the host copies of tools/embed_*.cc, since
# the NDK compiler cannot run its own output.
if(CMAKE_HOST_WIN32)
    set(HOST_EXE ".exe")
else()
    set(HOST_EXE "")
endif()

find_program(SDLAGENT_HOST_CXX NAMES g++ clang++ c++ REQUIRED)

set(HOST_TOOLS_DIR "${CMAKE_BINARY_DIR}/host-tools")
set(GEN_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${HOST_TOOLS_DIR}" "${GEN_DIR}")

set(EMBED_BINARY "${HOST_TOOLS_DIR}/embed_binary${HOST_EXE}")
add_custom_command(
    OUTPUT "${EMBED_BINARY}"
    COMMAND "${SDLAGENT_HOST_CXX}" -std=c++20 -O2
            "${SDLAGENT_ROOT}/tools/embed_binary.cc" -o "${EMBED_BINARY}"
    DEPENDS "${SDLAGENT_ROOT}/tools/embed_binary.cc"
    VERBATIM)

set(EMBED_PROMPTS "${HOST_TOOLS_DIR}/embed_prompts${HOST_EXE}")
add_custom_command(
    OUTPUT "${EMBED_PROMPTS}"
    COMMAND "${SDLAGENT_HOST_CXX}" -std=c++20 -O2
            "${SDLAGENT_ROOT}/tools/embed_prompts.cc" -o "${EMBED_PROMPTS}"
    DEPENDS "${SDLAGENT_ROOT}/tools/embed_prompts.cc"
    VERBATIM)

set(ASSET_ICON "${SDLAGENT_ROOT}/assets/icon/icon.png")
set(ASSET_ICON_BAR "${SDLAGENT_ROOT}/assets/icon/icon_bar.png")
set(ASSET_CA "${SDLAGENT_ROOT}/assets/ca-bundle.crt")
set(ASSET_SYS "${SDLAGENT_ROOT}/assets/prompts/sys.md")
set(ASSET_TOOL "${SDLAGENT_ROOT}/assets/prompts/tool.md")
set(ASSET_TITLE "${SDLAGENT_ROOT}/assets/prompts/title.md")

add_custom_command(
    OUTPUT "${GEN_DIR}/icon_data.inc"
    COMMAND "${EMBED_BINARY}" "${GEN_DIR}/icon_data.inc" icon
            "kPng=${ASSET_ICON}"
    DEPENDS "${EMBED_BINARY}" "${ASSET_ICON}" VERBATIM)

add_custom_command(
    OUTPUT "${GEN_DIR}/icon_bar_data.inc"
    COMMAND "${EMBED_BINARY}" "${GEN_DIR}/icon_bar_data.inc" icon
            "kBarPng=${ASSET_ICON_BAR}"
    DEPENDS "${EMBED_BINARY}" "${ASSET_ICON_BAR}" VERBATIM)

add_custom_command(
    OUTPUT "${GEN_DIR}/ca_data.inc"
    COMMAND "${EMBED_BINARY}" "${GEN_DIR}/ca_data.inc" ca
            "kCa=${ASSET_CA}"
    DEPENDS "${EMBED_BINARY}" "${ASSET_CA}" VERBATIM)

add_custom_command(
    OUTPUT "${GEN_DIR}/default_prompts.inc"
    COMMAND "${EMBED_PROMPTS}" "${GEN_DIR}/default_prompts.inc"
            "kSys=${ASSET_SYS}" "kTool=${ASSET_TOOL}" "kTitle=${ASSET_TITLE}"
    DEPENDS "${EMBED_PROMPTS}" "${ASSET_SYS}" "${ASSET_TOOL}" "${ASSET_TITLE}"
    VERBATIM)

add_custom_target(sdlagent_embedded DEPENDS
    "${GEN_DIR}/icon_data.inc"
    "${GEN_DIR}/icon_bar_data.inc"
    "${GEN_DIR}/ca_data.inc"
    "${GEN_DIR}/default_prompts.inc")

add_library(main SHARED
    ${SDLAGENT_SOURCES}
    "${SDLAGENT_ROOT}/third_party/libllmapi/llmapi.c"
    "${SDLAGENT_DEPS}/cjson/cJSON.c"
    "${SDLAGENT_DEPS}/md4c/src/md4c.c"
)
add_dependencies(main sdlagent_embedded)

target_include_directories(main PRIVATE
    "${SDLAGENT_ROOT}/src"
    "${SDLAGENT_ROOT}/third_party/SDLGui"
    "${SDLAGENT_ROOT}/third_party/libllmapi"
    "${SDLAGENT_DEPS}/md4c/src"
    "${CMAKE_BINARY_DIR}/include"
    "${GEN_DIR}"
    "${SDLAGENT_PREFIX}/include"
)

target_compile_options(main PRIVATE -Wall -Wextra -Wno-unused-parameter)

target_link_libraries(main PRIVATE
    SDL3::SDL3
    SDL3_ttf::SDL3_ttf
    base64
    CURL::libcurl
    "${SDLAGENT_PREFIX}/lib/libmbedtls.a"
    "${SDLAGENT_PREFIX}/lib/libmbedx509.a"
    "${SDLAGENT_PREFIX}/lib/libmbedcrypto.a"
    z
    log
)
