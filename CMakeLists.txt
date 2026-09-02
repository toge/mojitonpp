cmake_minimum_required(VERSION 3.25)
project(mojitonpp LANGUAGES CXX)

option(BUILD_TEST "Build tests" ON)
option(BUILD_TOOL "Build tool" ON)
option(ENABLE_WASI_MINIMAL "Enable WASI minimal (no exceptions) mode - wasi-sdk wasm32-wasip1 assumed" OFF)

if(ENABLE_WASI_MINIMAL)
    add_compile_definitions(MOJITONPP_WASI_MINIMAL=1)
endif()

# wasip1 では glaze を含む hosted ツールはビルドしない（frozenchars と同様に分岐）
if(ENABLE_WASI_MINIMAL AND CMAKE_SYSTEM_NAME STREQUAL "WASI")
    set(BUILD_TOOL OFF CACHE BOOL "Disable tool for WASI minimal" FORCE)
endif()

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

# wasip1 ゲスト (wasi-sdk) は WASI ツールチェインでのみビルド。
# frozenchars と同一手順: `cmake -DCMAKE_TOOLCHAIN_FILE=/opt/wasi-sdk/share/cmake/wasi-sdk-p1.cmake -DENABLE_WASI_MINIMAL=ON`
if(ENABLE_WASI_MINIMAL AND CMAKE_SYSTEM_NAME STREQUAL "WASI")
    add_executable(mojitonpp_guest wasm/guest.cpp)
    target_link_libraries(mojitonpp_guest PRIVATE mojitonpp)
    target_compile_features(mojitonpp_guest PRIVATE cxx_std_20)
    target_compile_options(mojitonpp_guest PRIVATE -fno-exceptions -fno-rtti)
    # ponytail: WASI libc を使うため -nostdlib 不要。wasi-sdk の通常リンクで wasip1 モジュールを生成。
    set_target_properties(mojitonpp_guest PROPERTIES
        SUFFIX ".wasm"
        LINK_FLAGS "-Wl,--no-entry -Wl,--export=mojiton_detect -Wl,--export=mojiton_scratch -Wl,--export-memory"
    )
endif()
