// wasm32-unknown-unknown フリースタンディングゲストのサンプル
//
// wasm3 等のホストからは以下の手順で使用する:
//   1. mojiton_scratch(len) で入力バッファを確保し、'\n' 区切りの文字列を書き込む
//   2. mojiton_detect(ptr, len) を呼ぶ
//   3. 戻り値のポインタから NUL 終端の結果文字列を読む
//      (1系統につき1行: "base_name|item1|item2|...")
//
// バンプアロケータのメモリは解放されないため、戻り値のポインタはプロセス存続中
// 常に有効。
#include "mojitonpp.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace {
// '\n' 区切りの入力を1行ずつ取り出す (空行は無視)
auto splitLines(std::string_view const view) {
  auto lines = std::vector<std::string>{};
  auto pos   = std::size_t{0};
  while (pos < view.size()) {
    auto const nl   = view.find('\n', pos);
    auto const last = (nl == std::string_view::npos) ? view.size() : nl;
    if (last > pos) {
      lines.emplace_back(view.substr(pos, last - pos));
    }
    if (nl == std::string_view::npos) {
      break;
    }
    pos = nl + 1;
  }
  return lines;
}
}  // namespace

extern "C" {

// ホストが入力を書き込むためのバッファを確保する
void* mojiton_scratch(unsigned const len) {
  return ::operator new(len);
}

// '\n' 区切りの文字列リストから系列を検出し、結果を NUL 終端文字列で返す
const char* mojiton_detect(unsigned const ptr, unsigned const len) {
  auto const view = std::string_view{reinterpret_cast<const char*>(ptr), len};
  auto const detector = mojitonpp::SequenceDetector{};
  auto const result   = detector.detect(splitLines(view));

  auto out = std::string{};
  for (auto const& seq : result.sequences) {
    out += seq.base_name;
    for (auto const& item : seq.items) {
      out += '|';
      out += item.value;
    }
    out += '\n';
  }
  // バンプアロケータのため、ローカル文字列の領域は関数を抜けても無効化されない
  // (実際の実体はスタックではなくリニアメモリ上のヒープにある)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-stack-address"
  return out.c_str();
#pragma clang diagnostic pop
}
}
