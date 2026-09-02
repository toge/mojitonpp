# WASI_MINIMAL 向けスモークは常時ビルド可能（hosted 依存なし）
# frozenchars と同一ポリシー: 例外なしでヘッダがコンパイルできることを検証。
add_executable(smoke_wasi_minimal smoke_wasi_minimal.cpp)
target_link_libraries(smoke_wasi_minimal PRIVATE mojitonpp)
target_compile_features(smoke_wasi_minimal PRIVATE cxx_std_20)
target_compile_definitions(smoke_wasi_minimal PRIVATE MOJITONPP_WASI_MINIMAL=1)
target_compile_options(smoke_wasi_minimal PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fno-exceptions>)
add_test(NAME smoke_wasi_minimal COMMAND smoke_wasi_minimal)

if(ENABLE_WASI_MINIMAL)
  return()
endif()

find_package(Catch2 CONFIG REQUIRED)

file(GLOB test_src CONFIGURE_DEPENDS test_*.cpp)

add_executable(all_test ${test_src})
target_link_libraries(all_test PRIVATE Catch2::Catch2WithMain mojitonpp)
target_include_directories(all_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR})
target_compile_features(all_test PRIVATE cxx_std_20)

add_test(NAME all_test COMMAND all_test -r junit)
