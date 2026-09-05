# Catch2 は wasm32-wasip1/wasip2 ではビルドできないため find_package 自体をスキップする。
# Emscripten は別途対応するため、WASI のみ早期 return する。
if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
  return()
endif()

# 例外なしスモーク: frozenchars と同一ポリシー。
# -fno-exceptions でもヘッダがコンパイルできることを検証。
add_executable(smoke_no_exceptions smoke_wasi_minimal.cpp)
target_link_libraries(smoke_no_exceptions PRIVATE mojitonpp)
target_compile_features(smoke_no_exceptions PRIVATE cxx_std_23)
target_compile_options(smoke_no_exceptions PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fno-exceptions>)
add_test(NAME smoke_no_exceptions COMMAND smoke_no_exceptions)

find_package(Catch2 CONFIG REQUIRED)

file(GLOB test_src CONFIGURE_DEPENDS test_*.cpp)

add_executable(all_test ${test_src})
target_link_libraries(all_test PRIVATE Catch2::Catch2WithMain mojitonpp)
target_include_directories(all_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR})
target_compile_features(all_test PRIVATE cxx_std_23)

add_test(NAME all_test COMMAND all_test -r junit)
