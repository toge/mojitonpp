#ifndef MOJITONPP_HPP__
#define MOJITONPP_HPP__

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <marisa.h>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace mojitonpp {

/**
 * @brief 検出された要素の情報
 */
struct detected_item {
  std::string         value;
  std::vector<double> indices;

  /**
   * @brief インデックス列の大小比較
   * @return lhs < rhs であれば true
   */
  [[nodiscard]]
  friend auto operator<(detected_item const& lhs, detected_item const& rhs) noexcept {
    return std::lexicographical_compare(lhs.indices.begin(), lhs.indices.end(), rhs.indices.begin(), rhs.indices.end());
  }
};

/**
 * @brief 検出結果
 */
struct detection_result {
  std::string                base_name; // <- 共通となる接頭辞
  std::vector<detected_item> items{};      // <- ベース名に続く数値列を抽出できたファイル一覧（インデックス順にソート済み）
  std::size_t                eligible_count{}; // <- 対象となった全ファイル数
  std::size_t                matched_count{}; // <- ベース名に続く数値列を抽出できたファイルの数

  /**
   * @brief 検出成功率を返す
   * @return 対象要素に対する検出件数の比率
   */
  [[nodiscard]]
  auto coverage() const noexcept {
    if (eligible_count == 0U) {
      return 0.0;
    }
    return static_cast<double>(matched_count) / static_cast<double>(eligible_count);
  }
};

/**
 * @brief 文字列として扱える要素を持つ入力範囲を表す Concept
 */
template <typename Range>
concept string_range = std::ranges::input_range<Range> && std::convertible_to<std::ranges::range_value_t<Range>, std::string_view>;

/**
 * @brief 支配的な系列を検出するためのオプション
 */
struct DetectorOptions {
  double                   threshold{0.9}; // <- 系列とみなすための最低限の検出率（0.0～1.0）
  bool                     treat_dot_as_decimal{false}; // <- ドットを小数点として扱うかどうか
  std::vector<std::string> allowed_extensions{}; // <- 許可する拡張子（空の場合はすべて許可）
};

/**
 * @brief 支配的な系列を検出するクラス
 */
class SequenceDetector {
public:
  explicit SequenceDetector(DetectorOptions const& opts = {}) : options_(opts) {}

  /**
   * @brief 文字列集合から系列を検出する
   * @tparam Range 文字列の入力範囲
   * @param inputs 検出対象文字列群
   * @return 検出された系列のリスト
   */
  template <string_range Range>
  [[nodiscard]]
  auto detect(Range const& inputs) const -> std::vector<detection_result> {
    auto pool = std::vector<std::string>{};
    if constexpr (std::ranges::sized_range<Range>) {
      pool.reserve(std::ranges::size(inputs));
    }

    for (auto const& input : inputs) {
      auto const view = static_cast<std::string_view>(input);
      if (!options_.allowed_extensions.empty()) {
        auto const matched = std::ranges::any_of(options_.allowed_extensions, [view](auto const& ext) {
          return view.ends_with(ext);
        });
        if (!matched) {
          continue;
        }
      }
      pool.emplace_back(view);
    }

    if (pool.empty()) {
      return {};
    }

    auto const total_eligible = pool.size();
    auto       results        = std::vector<detection_result>{};

    while (!pool.empty()) {
      auto const snapshot = buildTrie(pool);
      auto const base_name = chooseBaseName(snapshot, total_eligible);

      // 有効なベース名が見つからない、または残りのプールが閾値に満たない場合は終了
      if (base_name.empty() && pool.size() < coverageThreshold(total_eligible)) {
        break;
      }

      auto result = detection_result{
        .base_name      = base_name,
        .items          = {},
        .eligible_count = total_eligible,
      };

      auto next_pool = std::vector<std::string>{};
      for (auto const& input : pool) {
        if (auto const indices = extractIndices(input, result.base_name, options_.treat_dot_as_decimal)) {
          result.items.push_back(detected_item{
            .value   = input,
            .indices = *indices,
          });
        } else {
          next_pool.push_back(input);
        }
      }

      result.matched_count = result.items.size();
      if (result.matched_count >= coverageThreshold(total_eligible)) {
        std::ranges::sort(result.items, [](auto const& lhs, auto const& rhs) { return lhs < rhs; });
        results.push_back(std::move(result));
        pool = std::move(next_pool);
      } else {
        // 最も支配的な候補でも閾値を満たさない場合は終了
        break;
      }
    }

    return results;
  }

private:
  DetectorOptions options_;

  struct trie_snapshot {
    marisa::Trie             trie;
    std::vector<std::string> names;
  };

  /**
   * @brief 閾値を満たすために必要な件数を返す
   * @param total 総件数
   * @return 必要件数
   */
  [[nodiscard]]
  auto coverageThreshold(std::size_t const total) const noexcept -> std::size_t {
    return static_cast<std::size_t>(std::ceil(static_cast<double>(total) * options_.threshold));
  }

  /**
   * @brief 静的 Trie と辞書順ソート済み名前一覧を構築する
   * @param names 文字列一覧
   * @return 構築済み Trie と辞書順一覧
   */
  [[nodiscard]]
  static auto buildTrie(std::span<std::string const> names) -> trie_snapshot {
    auto snapshot = trie_snapshot{};
    snapshot.names.reserve(names.size());
    for (auto const& name : names) {
      snapshot.names.emplace_back(name);
    }
    std::ranges::sort(snapshot.names);

    {
      auto keyset = marisa::Keyset{};
      for (auto const& name : snapshot.names) {
        keyset.push_back(name.c_str());
      }
      snapshot.trie.build(keyset);
    }

    return snapshot;
  }

  /**
   * @brief 2つの文字列の最長共通接頭辞を返す
   * @return 共通接頭辞
   */
  [[nodiscard]]
  static auto longestCommonPrefix(std::string_view const lhs, std::string_view const rhs) -> std::string {
    auto const mismatch = std::ranges::mismatch(lhs, rhs);
    return std::string{lhs.begin(), mismatch.in1};
  }

  /**
   * @brief 接頭辞末尾の数値要素を除去してベース名へ正規化する
   * @param text 接頭辞候補
   * @return ベース名候補
   */
  [[nodiscard]]
  auto trimTrailingNumericParts(std::string_view text) const {
    auto changed = true;
    while (changed) {
      changed = false;
      while (!text.empty() && std::isdigit(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1U);
        changed = true;
      }
      if (options_.treat_dot_as_decimal && !text.empty() && text.back() == '.') {
        auto temp = text;
        temp.remove_suffix(1U);
        if (!temp.empty() && std::isdigit(static_cast<unsigned char>(temp.back())) != 0) {
          text.remove_suffix(1U);
          changed = true;
        }
      }
    }
    return std::string{text};
  }

  /**
   * @brief Trie 上で接頭辞一致件数を数える
   * @param trie 構築済み Trie
   * @param prefix 調べる接頭辞
   * @return 一致件数
   */
  [[nodiscard]]
  static auto countPrefixMatches(marisa::Trie const& trie, std::string_view const prefix) {
    auto agent = marisa::Agent{};
    agent.set_query(prefix.data(), prefix.size());

    auto count = std::size_t{0U};
    while (trie.predictive_search(agent)) {
      ++count;
    }
    return count;
  }

  /**
   * @brief 閾値を満たす最大長のベース名を選ぶ
   * @param snapshot Trie と辞書順文字列一覧
   * @param total_count 全体数
   * @return ベース名
   */
  [[nodiscard]]
  auto chooseBaseName(trie_snapshot const& snapshot, std::size_t const total_count) const -> std::string {
    if (snapshot.names.empty()) {
      return {};
    }

    auto const threshold  = coverageThreshold(total_count);
    if (threshold == 0) {
      return {};
    }
    // snapshot.names.size() が threshold より小さい場合、スライディングウィンドウは組めない
    if (snapshot.names.size() < threshold) {
      return {};
    }

    auto const window_end = snapshot.names.size() - threshold + 1U;
    auto       best       = std::string{};
    auto       best_count = std::size_t{0U};

    for (auto const start : std::views::iota(std::size_t{0U}, window_end)) {
      auto const& min_name  = snapshot.names[start];
      auto const& max_name  = snapshot.names[start + threshold - 1U];
      auto const  raw_lcp   = longestCommonPrefix(min_name, max_name);
      auto const  candidate = trimTrailingNumericParts(raw_lcp);

      auto low  = std::size_t{0U};
      auto high = candidate.size();
      while (low < high) {
        auto const mid   = (low + high + 1U) / 2U;
        auto const count = countPrefixMatches(snapshot.trie, std::string_view{candidate}.substr(0U, mid));
        if (count >= threshold) {
          low = mid;
        } else {
          high = mid - 1U;
        }
      }

      auto const verified = candidate.substr(0U, low);
      auto const matches  = verified.empty() ? snapshot.names.size() : countPrefixMatches(snapshot.trie, verified);
      if (verified.size() > best.size() || (verified.size() == best.size() && matches > best_count) || (verified.size() == best.size() && matches == best_count && verified < best)) {
        best       = verified;
        best_count = matches;
      }
    }

    return best;
  }

  /**
   * @brief ベース名直後の数値列を抽出する
   * @param input 対象文字列
   * @param base_name ベース名
   * @param treat_dot_as_decimal 小数点として扱うかどうか
   * @return インデックス列を抽出できた場合はその値、できない場合は `std::nullopt`
   */
  [[nodiscard]]
  static auto extractIndices(std::string_view const input, std::string_view const base_name, bool const treat_dot_as_decimal) -> std::optional<std::vector<double>> {
    if (!input.starts_with(base_name)) {
      return std::nullopt;
    }

    auto suffix = input.substr(base_name.size());
    if (suffix.empty() || (std::isdigit(static_cast<unsigned char>(suffix.front())) == 0)) {
      return std::nullopt;
    }

    auto indices = std::vector<double>{};
    auto ptr     = suffix.data();
    auto end     = suffix.data() + suffix.size();

    while (ptr < end) {
      if (std::isdigit(static_cast<unsigned char>(*ptr)) != 0 || (treat_dot_as_decimal && *ptr == '.')) {
        auto  val      = 0.0;
        auto const res = std::from_chars(ptr, end, val);
        if (res.ec == std::errc{}) {
          indices.push_back(val);
          ptr = res.ptr;
        } else if (std::isdigit(static_cast<unsigned char>(*ptr)) != 0) {
          // 整数としてのフォールバック
          auto const start_ptr = ptr;
          while (ptr < end && std::isdigit(static_cast<unsigned char>(*ptr)) != 0) {
            ++ptr;
          }
          auto       val_int = std::uint64_t{};
          auto const res_int = std::from_chars(start_ptr, ptr, val_int);
          if (res_int.ec == std::errc{}) {
            indices.push_back(static_cast<double>(val_int));
          }
        } else {
          ++ptr;
        }
      } else {
        ++ptr;
      }
    }

    if (indices.empty()) {
      return std::nullopt;
    }

    return indices;
  }
};

/**
 * @brief メタデータ（除外対象）かどうかを判定する
 * @param input 判定対象文字列
 * @return メタデータであれば `true`
 */
[[nodiscard]]
inline auto isMetadata(std::string_view const input) {
  if (input.empty()) {
    return true;
  }
  if (input.front() == '.') {
    return true;
  }
  static constexpr auto metadata_files = std::array<std::string_view, 2>{
    "Thumbs.db",
    "desktop.ini"
  };
  return std::ranges::any_of(metadata_files, [input](auto const& m) { return input == m; });
}

}  // namespace mojitonpp

#endif /* MOJITONPP_HPP__ */
