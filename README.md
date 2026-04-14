# mojitonpp (連番文字列検出ライブラリ)

文字列群の中から、**90%以上(変更可能)を占める支配的な 1 系列**の連番パターンを検出し、共通接頭辞（ベース名）と可変数値部分を分離してソートするための C++26 ライブラリ、およびそれを利用した連番ファイル検出 CLI ツールです。

## 特徴

- **純粋な文字列処理**: ライブラリ自体はファイルシステムに依存せず、与えられた文字列集合に対して動作します
- [marisa-trie](https://github.com/s-yata/marisa-trie)による高速な接頭辞一致検索
- 辞書順に並べた文字列集合の **最小 / 最大** から共通接頭辞を求め、ウィンドウごとに候補を生成
- Trie 上の接頭辞一致件数を **二分探索** で検証し、ノイズに強い最大長ベース名を特定
- ベース名直後の数値列を抽出し、**数値順（自然順）** にソート可能
- `glaze` による JSON 出力 (CLI ツール)
- `quill` によるログ出力 (CLI ツール)

## ビルド

このリポジトリは vcpkg manifest mode を前提にしています。

```bash
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build
ctest --test-dir build --output-on-failure
```

## 使い方 (CLI ツール)

```bash
./build/sequence_detector <directory>
./build/sequence_detector --json <directory>
./build/sequence_detector --verbose <directory>
```

### 通常出力例

```text
対象ディレクトリ: /data/frames
ベース名: frame_
検出件数: 120/123 (97.56%)
連番ファイル一覧:
[           1]  frame_001.png
[           2]  frame_002.jpg
[           3]  frame_003.png
```

## アルゴリズム概要

1. (CLI) 指定ディレクトリを走査し、通常ファイル名のみを収集します。
2. ライブラリに文字列集合を渡し、`marisa::Trie` を構築します。
3. 文字列を辞書順にソートし、全体の 90% を満たす連続ウィンドウごとに先頭と末尾の最長共通接頭辞を求めます。
4. 共通接頭辞末尾の数字を落としてベース名候補を作り、Trie 上で「その接頭辞を共有する件数」を二分探索で検証します。
5. 最も長く、かつ 90% 条件を満たすベース名を採用します。
6. ベース名直後の数字列を数値として抽出し、自然順で並べて出力します。

## ライブラリの利用

`mojitonpp::SequenceDetector` を使用して、任意の文字列集合からパターンを検出できます。

```cpp
#include "mojitonpp.hpp"
#include <vector>
#include <string>

int main() {
  std::vector<std::string> inputs = {"img_01.png", "img_02.png", "other.txt"};
  mojitonpp::SequenceDetector detector;
  auto result = detector.detect(inputs);
  if (result) {
    // result->base_name == "img_"
    // result->items[0].value == "img_01.png"
  }
}
```

## テスト

Catch2で以下を検証しています。

- ノイズを含む集合でも支配的系列を検出できること
- `frame1`, `frame2`, `frame10` を数値順に並べること
- 90% 未満の系列は失敗扱いになること

## ライセンス

MIT License
