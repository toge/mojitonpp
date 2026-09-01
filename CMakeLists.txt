cmake_minimum_required(VERSION 3.25)
project(mojitonpp LANGUAGES CXX)

option(BUILD_TEST "Build tests" ON)
option(BUILD_TOOL "Build tool" ON)
option(ENABLE_FREESTANDING "Build wasm32-unknown-unknown freestanding guest (for wasm3 embedding)" OFF)

## add warning
if (CMAKE_COMPILER_IS_GNUCXX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g3")
endif()

if (MSVC)
    if(CMAKE_CXX_FLAGS MATCHES "/W[0-4]")
        string(REGEX REPLACE "/W[0-4]" "/W4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    else()
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
    endif()
endif()

## optimization
if (CMAKE_COMPILER_IS_GNUCXX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native")
endif()
if (MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Ox")
endif()

## other flag
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(BUILD_TOOL)
    find_package(glaze CONFIG REQUIRED)
endif()

include(GNUInstallDirs)

add_library(mojitonpp INTERFACE)
target_include_directories(mojitonpp INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_features(mojitonpp INTERFACE cxx_std_20)

if(BUILD_TOOL)
    add_executable(sequence_detector main.cpp)
    target_link_libraries(sequence_detector PRIVATE mojitonpp glaze::glaze)
    # glazeのためC++23が必要
    target_compile_features(sequence_detector PRIVATE cxx_std_23)
endif()

# Installation rules
install(TARGETS mojitonpp
    EXPORT mojitonppTargets
)

install(FILES mojitonpp.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT mojitonppTargets
    FILE mojitonppTargets.cmake
    NAMESPACE mojitonpp::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mojitonpp
)

if(BUILD_TOOL)
    install(TARGETS sequence_detector
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

if(BUILD_TEST)
    enable_testing()
    add_subdirectory(test)
endif()

if(ENABLE_FREESTANDING)
    # wasm32-unknown-unknown (freestanding, -nostdlib) でゲストモジュールをビルドする。
    # clang は emsdk 同梱のものを優先して検索し、libc++ ヘッダは emsdk の sysroot を使う。
    find_program(MOJITONPP_WASM_CLANG
        NAMES clang++
        HINTS "$ENV{EMSDK}/upstream/bin"
        REQUIRED
    )
    set(MOJITONPP_WASM_SYSROOT "$ENV{EMSDK}/upstream/emscripten/cache/sysroot"
        CACHE PATH "wasm32 freestanding ビルド用 sysroot (libc++ ヘッダを含むこと)")
    if(NOT EXISTS "${MOJITONPP_WASM_SYSROOT}/include/c++/v1/string")
        message(FATAL_ERROR
            "libc++ headers not found in MOJITONPP_WASM_SYSROOT: ${MOJITONPP_WASM_SYSROOT}\n"
            "emsdk の install/activate を実行して sysroot を作成するか、-DMOJITONPP_WASM_SYSROOT=<path> で指定してください")
    endif()

    set(FREESTANDING_CXXFLAGS
        --target=wasm32-unknown-unknown
        -std=c++20
        -Os
        -fno-exceptions
        -fno-rtti
        -nostdlib
        -DMOJITONPP_FREESTANDING
        "-I${CMAKE_CURRENT_SOURCE_DIR}"
        "-isystem${MOJITONPP_WASM_SYSROOT}/include/c++/v1"
        "-isystem${MOJITONPP_WASM_SYSROOT}/include/wasm32-wasi"
        "-isystem${MOJITONPP_WASM_SYSROOT}/include"
    )
    set(FREESTANDING_OUT ${CMAKE_CURRENT_BINARY_DIR}/mojitonpp_guest.wasm)

    add_custom_command(OUTPUT ${FREESTANDING_OUT}
        COMMAND ${MOJITONPP_WASM_CLANG} ${FREESTANDING_CXXFLAGS} -c
            ${CMAKE_CURRENT_SOURCE_DIR}/wasm/guest.cpp
            -o ${CMAKE_CURRENT_BINARY_DIR}/freestanding_guest.o
        COMMAND ${MOJITONPP_WASM_CLANG} ${FREESTANDING_CXXFLAGS} -c
            ${CMAKE_CURRENT_SOURCE_DIR}/wasm/freestanding_support.cpp
            -o ${CMAKE_CURRENT_BINARY_DIR}/freestanding_support.o
        COMMAND ${MOJITONPP_WASM_CLANG} ${FREESTANDING_CXXFLAGS}
            ${CMAKE_CURRENT_BINARY_DIR}/freestanding_guest.o
            ${CMAKE_CURRENT_BINARY_DIR}/freestanding_support.o
            -o ${FREESTANDING_OUT}
            -Wl,--no-entry
            -Wl,--export=mojiton_detect
            -Wl,--export=mojiton_scratch
            -Wl,--export-memory
        DEPENDS wasm/guest.cpp wasm/freestanding_support.cpp mojitonpp.hpp
        VERBATIM
    )
    add_custom_target(freestanding_guest ALL DEPENDS ${FREESTANDING_OUT})

    # スモークテスト: node が使える環境でゲストモジュールの動作を検証する
    find_program(MOJITONPP_NODE node)
    if(MOJITONPP_NODE)
        add_test(NAME freestanding_smoke
            COMMAND ${MOJITONPP_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/wasm/run_guest.mjs ${FREESTANDING_OUT}
        )
    endif()
endif()
