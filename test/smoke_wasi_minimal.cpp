/**
 * @file test/smoke_wasi_minimal.cpp
 * @brief 例外なしモードの検証。
 *
 * -fno-exceptions 付きでビルドされる。ヘッダがコンパイルできることを確認する。
 * frozenchars/test/smoke_wasi_minimal.cpp と同一方針。
 */
#include "mojitonpp.hpp"

#include <cassert>
#include <string>
#include <vector>

int main() {
  using namespace mojitonpp;

  // 基本検出（整数）
  {
    SequenceDetector d;
    auto r = d.detect(std::vector<std::string>{"img_001.png", "img_002.png", "img_003.png"});
    assert(!r.sequences.empty());
    assert(r.sequences[0].base_name == "img_");
    assert(r.sequences[0].items.size() == 3);
  }

  // 小数点を含むケース
  {
    SequenceDetector d(DetectorOptions{.treat_dot_as_decimal = true});
    auto r = d.detect(std::vector<std::string>{"a1.5", "a2.5", "a3.5"});
    assert(!r.sequences.empty());
  }

  return 0;
}
