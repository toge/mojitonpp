#ifndef MOJITONPP_HPP__
#define MOJITONPP_HPP__

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <concepts>
#include <filesystem>
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
 * @brief 連番検出対象として扱うファイル情報です。
 */
struct detected_file {
  std::filesystem::path path;
  std::string           filename;
  std::vector<double>   indices;

  /**
   * @brief インデックス列の大小比較を行います。
   * @param lhs 左辺です。
   * @param rhs 右辺です。
   * @return lhs < rhs であれば true です。
   */
  friend auto operator<(detected_file const& lhs, detected_file const& rhs) noexcept -> bool {
    return std::lexicographical_compare(lhs.indices.begin(), lhs.indices.end(), rhs.indices.begin(), rhs.indices.end());
  }
};

/**
 * @brief 連番検出の結果を表します。
 */
struct detection_result {
  std::string               base_name;
  std::vector<detected_file> files;
  std::size_t               eligible_file_count{};
  std::size_t               matched_file_count{};

  /**
   * @brief 検出成功率を返します。
   * @return 対象ファイルに対する検出件数の比率です。
   */
  [[nodiscard]] auto coverage() const noexcept -> double {
    if (eligible_file_count == 0U) {
      return 0.0;
    }
    return static_cast<double>(matched_file_count) / static_cast<double>(eligible_file_count);
  }
};

/**
 * @brief `std::filesystem::path` を要素に持つ入力範囲を表す Concept です。
 */
template <typename Range>
concept path_range = std::ranges::input_range<Range> && std::same_as<std::remove_cvref_t<std::ranges::range_value_t<Range>>, std::filesystem::path>;

/**
 * @brief 支配的な連番系列を検出するためのオプションです。
 */
struct DetectorOptions {
  double threshold{0.9};
  bool   treat_dot_as_decimal{false};
};

/**
 * @brief 支配的な連番系列を検出するクラスです。
 */
class SequenceDetector {
public:
  explicit SequenceDetector(DetectorOptions const& opts = {}) : options_(opts) {}

  /**
   * @brief パス集合から連番系列を検出します。
   * @tparam Range `std::filesystem::path` の入力範囲です。
   * @param files 検出対象ファイル群です。
   * @return 連番系列が見つかった場合は結果、見つからない場合は `std::nullopt` です。
   */
  template <path_range Range>
  [[nodiscard]] auto detect(Range const& files) const -> std::optional<detection_result> {
    auto buffer = std::vector<std::filesystem::path>{};
    if constexpr (std::ranges::sized_range<Range>) {
      buffer.reserve(std::ranges::size(files));
    }

    for (auto const& file : files) {
      buffer.emplace_back(file);
    }
    return detectFromPaths(buffer);
  }

  /**
   * @brief パス配列から連番系列を検出します。
   * @param files 検出対象ファイル群です。
   * @return 連番系列が見つかった場合は結果、見つからない場合は `std::nullopt` です。
   */
  [[nodiscard]] auto detectFromPaths(std::span<std::filesystem::path const> files) const -> std::optional<detection_result> {
    if (files.empty()) {
      return std::nullopt;
    }

    auto names = std::vector<std::string>{};
    names.reserve(files.size());
    for (auto const& path : files) {
      names.emplace_back(path.filename().string());
    }

    auto const snapshot  = buildTrie(names);
    auto       base_name = chooseBaseName(snapshot);
    auto       result    = detection_result{};
    result.base_name     = std::move(base_name);
    result.eligible_file_count = files.size();

    for (auto const& path : files) {
      auto const filename = path.filename().string();
      if (auto const indices = extractIndices(filename, result.base_name, options_.treat_dot_as_decimal)) {
        result.files.push_back(detected_file{
          .path     = path,
          .filename = filename,
          .indices  = *indices,
        });
      }
    }

    std::ranges::sort(result.files, [](detected_file const& lhs, detected_file const& rhs) {
      return lhs < rhs;
    });

    result.matched_file_count = result.files.size();
    if (result.matched_file_count < coverageThreshold(result.eligible_file_count)) {
      return std::nullopt;
    }
    return result;
  }

private:
  DetectorOptions options_;

  struct trie_snapshot {
    marisa::Trie            trie;
    std::vector<std::string> names;
  };

  /**
   * @brief 閾値を満たすために必要な件数を返します。
   * @param total 総件数です。
   * @return 必要件数です。
   */
  [[nodiscard]] auto coverageThreshold(std::size_t const total) const noexcept -> std::size_t {
    return static_cast<std::size_t>(std::ceil(static_cast<double>(total) * options_.threshold));
  }

  /**
   * @brief 静的 Trie と辞書順ソート済み名前一覧を構築します。
   * @param names ファイル名一覧です。
   * @return 構築済み Trie と辞書順一覧です。
   */
  [[nodiscard]] static auto buildTrie(std::span<std::string const> names) -> trie_snapshot {
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
   * @brief 二つの文字列の最長共通接頭辞を返します。
   * @param lhs 左辺です。
   * @param rhs 右辺です。
   * @return 共通接頭辞です。
   */
  [[nodiscard]] static auto longestCommonPrefix(std::string_view const lhs, std::string_view const rhs) -> std::string {
    auto const mismatch = std::ranges::mismatch(lhs, rhs);
    return std::string{lhs.begin(), mismatch.in1};
  }

  /**
   * @brief 接頭辞末尾の数値要素を除去してベース名へ正規化します。
   * @param text 接頭辞候補です。
   * @return ベース名候補です。
   */
  [[nodiscard]] auto trimTrailingNumericParts(std::string_view text) const -> std::string {
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
   * @brief Trie 上で接頭辞一致件数を数えます。
   * @param trie 構築済み Trie です。
   * @param prefix 調べる接頭辞です。
   * @return 一致件数です。
   */
  [[nodiscard]] static auto countPrefixMatches(marisa::Trie const& trie, std::string_view const prefix) -> std::size_t {
    auto agent = marisa::Agent{};
    agent.set_query(prefix.data(), prefix.size());

    auto count = std::size_t{0U};
    while (trie.predictive_search(agent)) {
      ++count;
    }
    return count;
  }

  /**
   * @brief 閾値を満たす最大長のベース名を選びます。
   * @param snapshot Trie と辞書順ファイル名一覧です。
   * @return ベース名です。
   */
  [[nodiscard]] auto chooseBaseName(trie_snapshot const& snapshot) const -> std::string {
    if (snapshot.names.empty()) {
      return {};
    }

    auto const threshold  = coverageThreshold(snapshot.names.size());
    if (threshold == 0) {
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
   * @brief ベース名直後の数値列を抽出します。
   * @param filename 対象ファイル名です。
   * @param base_name ベース名です。
   * @param treat_dot_as_decimal 小数点として扱うかどうかです。
   * @return インデックス列を抽出できた場合はその値、できない場合は `std::nullopt` です。
   */
  [[nodiscard]] static auto extractIndices(std::string_view const filename, std::string_view const base_name, bool const treat_dot_as_decimal) -> std::optional<std::vector<double>> {
    if (!filename.starts_with(base_name)) {
      return std::nullopt;
    }

    auto suffix = filename.substr(base_name.size());
    if (suffix.empty() || (std::isdigit(static_cast<unsigned char>(suffix.front())) == 0)) {
      return std::nullopt;
    }

    auto indices = std::vector<double>{};
    auto ptr     = suffix.data();
    auto end     = suffix.data() + suffix.size();

    while (ptr < end) {
      if (std::isdigit(static_cast<unsigned char>(*ptr)) != 0) {
        auto  val      = double{};
        char* next_ptr = nullptr;
        if (treat_dot_as_decimal) {
          val      = std::strtod(ptr, &next_ptr);
          indices.push_back(val);
          ptr = next_ptr;
        } else {
          // 整数として読み込む
          auto const start_ptr = ptr;
          while (ptr < end && std::isdigit(static_cast<unsigned char>(*ptr)) != 0) {
            ++ptr;
          }
          auto       val_int = std::uint64_t{};
          auto const res     = std::from_chars(start_ptr, ptr, val_int);
          if (res.ec == std::errc{}) {
            indices.push_back(static_cast<double>(val_int));
          }
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
 * @brief メタデータ系ファイルかどうかを判定します。
 * @param path 判定対象パスです。
 * @return メタデータ系ファイルであれば `true`、それ以外は `false` です。
 */
[[nodiscard]] inline auto isMetadataFile(std::filesystem::path const& path) -> bool {
  auto const filename = path.filename().string();
  if (filename.empty()) {
    return true;
  }
  // ドットで始まるファイル（隠しファイル）のみを除外対象とする
  if (filename.front() == '.') {
    return true;
  }

  return false;
}

}  // namespace mojitonpp

#endif /* MOJITONPP_HPP__ */
