# 連番ファイル検出ツール

指定ディレクトリ内の通常ファイルから、READMEや設定ファイルなどのメタデータを除外し、**90%以上(変更可能)を占める支配的な 1 系列**の連番ファイルを検出する C++26 のライブラリとそれを利用した CLI ツールです。

## 特徴

- [marisa-trie](https://github.com/s-yata/marisa-trie)に全ファイル名を格納した静的Trieを構築
- 辞書順に並べたファイル名集合の **最小 / 最大** から共通接頭辞を求め、90% ウィンドウごとに候補を生成
- Trie 上の接頭辞一致件数を **二分探索** で検証し、ノイズに強い最大長ベース名を特定
- ベース名直後の数値列を抽出し、**数値順（自然順）** にソートして出力
- `glaze` による JSON 出力
- `quill` によるログ出力

## ビルド

このリポジトリは vcpkg manifest mode を前提にしています。

```bash
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build
ctest --test-dir build --output-on-failure
```

## 使い方

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
       1  frame_001.png
       2  frame_002.jpg
       3  frame_003.png
```

### JSON 出力例

```json
{
  "directory": "/data/frames",
  "base_name": "frame_",
  "eligible_file_count": 123,
  "matched_file_count": 120,
  "coverage": 0.975609756097561,
  "files": [
    {
      "filename": "frame_001.png",
      "index": 1
    }
  ]
}
```

## アルゴリズム概要

1. 指定ディレクトリを走査し、通常ファイルのみを収集します。
2. メタデータ系ファイルを除外した残りのファイル名で `marisa::Trie` を構築します。
3. ファイル名を辞書順にソートし、全体の 90% を満たす連続ウィンドウごとに先頭と末尾の最長共通接頭辞を求めます。
4. 共通接頭辞末尾の数字を落としてベース名候補を作り、Trie 上で「その接頭辞を共有する件数」を二分探索で検証します。
5. 最も長く、かつ 90% 条件を満たすベース名を採用します。
6. ベース名直後の数字列を整数として抽出し、自然順で並べて出力します。

## テスト

Catch2で以下を検証しています。

- ノイズを含むディレクトリでも支配的系列を検出できること
- `frame1`, `frame2`, `frame10` を数値順に並べること
- 90% 未満の系列は失敗扱いになること
