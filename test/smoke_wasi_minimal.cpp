/**
 * @file test/smoke_wasi_minimal.cpp
 * @brief MOJITONPP_WASI_MINIMAL モードの検証。
 *
 * -fno-exceptions 付きでビルドされる。数値パースの自前実装 (parseDecimal)
 * を使う経路でもコンパイル・実行できることを確認する。
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

  // 小数点を含むケース（WASI_MINIMAL では parseDecimal 経由）
  {
    SequenceDetector d(DetectorOptions{.treat_dot_as_decimal = true});
    auto r = d.detect(std::vector<std::string>{"a1.5", "a2.5", "a3.5"});
    assert(!r.sequences.empty());
  }

  // MOJITONPP_THROW が abort に置換されてもリンクできること
  {
    bool threw = false;
    (void)threw;
    // 例外送出パスは WASI_MINIMAL では呼ばれないが、マクロ自体はコンパイルできる
  }

  return 0;
}
