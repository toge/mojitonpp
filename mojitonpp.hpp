#ifndef MOJITONPP_HPP__
#define MOJITONPP_HPP__

// フリースタンディングモード (wasm32-unknown-unknown + -nostdlib 等) では
// libc の関数 (isdigit / ceil / strtod / from_chars) を一切使用しない。
// ホスト環境との挙動を一致させるため、常時ロケール非依存の自前実装を用いる。
#include <algorithm>
#include <array>
#include <concepts>
#include <cstdint>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>
#if !defined(MOJITONPP_FREESTANDING)
#include <charconv>
#include <cstdlib>
#endif

namespace mojitonpp {
namespace detail {
[[nodiscard]]
constexpr auto isDigit(char const c) noexcept -> bool {
  return c >= '0' && c <= '9';
}

// ponytail: parseDecimal は 16 桁程度までの 10 進数を想定 (hex / inf / nan 非対応、
// 桁数が多くなると double 精度で丸めが発生する)。index 抽出用途では十分。
[[nodiscard]]
inline auto parseDecimal(const char* ptr, const char* const last, double& value) noexcept -> const char* {
  auto const start  = ptr;
  auto       mant   = 0.0;
  while (ptr < last && isDigit(*ptr)) {
    mant = mant * 10.0 + static_cast<double>(*ptr - '0');
    ++ptr;
  }
  auto frac        = 0.0;
  auto frac_digits = 0;
  if (ptr < last && *ptr == '.') {
    ++ptr;
    while (ptr < last && isDigit(*ptr)) {
      frac = frac * 10.0 + static_cast<double>(*ptr - '0');
      ++frac_digits;
      ++ptr;
    }
  }
  // 数字を1つも読めなければ失敗
  if (ptr == start || (ptr == start + 1 && *start == '.')) {
    return nullptr;
  }
  auto val = mant;
  for (auto i = 0; i < frac_digits; ++i) {
    val *= 10.0;
  }
  val += frac;
  for (auto i = 0; i < frac_digits; ++i) {
    val /= 10.0;
  }
  // 指数部 (strtod / from_chars と同様に 1e3 形式を処理する)
  if (ptr < last && (*ptr == 'e' || *ptr == 'E')) {
    auto       p      = ptr + 1;
    auto       neg    = false;
    if (p < last && (*p == '+' || *p == '-')) {
      neg = (*p == '-');
      ++p;
    }
    auto       exp    = 0;
    auto const estart = p;
    while (p < last && isDigit(*p)) {
      exp = exp * 10 + (*p - '0');
      ++p;
    }
    if (p != estart) {
      for (auto i = 0; i < exp; ++i) {
        val = neg ? (val / 10.0) : (val * 10.0);
      }
      ptr = p;
    }
  }
  value = val;
  return ptr;
}

// 整数列の抽出用。オーバーフロー時は値を格納せず nullptr を返す (from_chars の
// result_out_of_range 相当)
[[nodiscard]]
inline auto parseUint64(const char* ptr, const char* const last, std::uint64_t& value) noexcept -> const char* {
  auto const start  = ptr;
  auto       val    = std::uint64_t{0};
  auto       oom    = false;
  while (ptr < last && isDigit(*ptr)) {
    auto const d = static_cast<std::uint64_t>(*ptr - '0');
    if (val > (UINT64_MAX - d) / 10U) {
      oom = true;
    } else {
      val = val * 10U + d;
    }
    ++ptr;
  }
  if (ptr == start || oom) {
    return nullptr;
  }
  value = val;
  return ptr;
}

// ponytail: 非負かつ 2^53 未満の値を想定した ceil (total * threshold の計算用)。
// 負の値や巨大値は想定外 (threshold は 0.0～1.0 を前提)。
[[nodiscard]]
constexpr auto ceilToSize(double const x) noexcept -> std::size_t {
  auto const t = static_cast<std::uint64_t>(x);
  return static_cast<std::size_t>(t + ((static_cast<double>(t) < x) ? 1U : 0U));
}

#if !defined(MOJITONPP_FREESTANDING)
[[nodiscard]]
inline auto fromChars(const char* first, const char* last, double& value) noexcept -> std::from_chars_result {
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 202306L
  return std::from_chars(first, last, value);
#else
  auto       buf    = std::array<char, 64>{};
  auto const len    = static_cast<std::size_t>(last - first);
  auto const ncopy  = (std::min)(len, buf.size() - 1U);
  std::copy_n(first, ncopy, buf.data());
  buf[ncopy] = '\0';
  char* end{};
  auto const val = std::strtod(buf.data(), &end);
  if (end == buf.data()) {
    return {first, std::errc::invalid_argument};
  }
  value = val;
  return {first + (end - buf.data()), std::errc{}};
#endif
}
#endif
}  // namespace detail

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
 * @brief 検出結果のまとめ
 */
struct detect_result {
  std::vector<detection_result> sequences{}; // <- 検出された系列のリスト
  std::vector<std::string>      excluded{};  // <- どの系列にも含まれなかった文字列
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
  bool                     build_excluded{true}; // <- detect_result.excluded を構築するかどうか（false にすると除外リストを空にする）
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
  auto detect(Range const& inputs) const -> detect_result {
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
      return detect_result{};
    }

    auto results = std::vector<detection_result>{};

    while (!pool.empty()) {
      auto const iteration_eligible = pool.size();
      std::ranges::sort(pool);
      auto const base_name = chooseBaseName(pool, iteration_eligible);

      if (base_name.empty()) {
        break;
      }

      auto result = detection_result{
        .base_name      = base_name,
        .items          = {},
        .eligible_count = iteration_eligible,
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
      if (result.matched_count >= coverageThreshold(iteration_eligible)) {
        std::ranges::sort(result.items, [](auto const& lhs, auto const& rhs) { return lhs < rhs; });
        results.push_back(std::move(result));
        pool = std::move(next_pool);
      } else {
        break;
      }
    }

    if (options_.build_excluded) {
      return detect_result{
        .sequences = std::move(results),
        .excluded  = std::move(pool),
      };
    }

    return detect_result{
      .sequences = std::move(results),
      .excluded  = {},
    };
  }

private:
  DetectorOptions options_;

  /**
   * @brief 閾値を満たすために必要な件数を返す
   * @param total 総件数
   * @return 必要件数
   */
  [[nodiscard]]
  auto coverageThreshold(std::size_t const total) const noexcept -> std::size_t {
    return detail::ceilToSize(static_cast<double>(total) * options_.threshold);
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
  auto trimTrailingNumericParts(std::string_view text) const noexcept -> std::string {
    auto changed = true;
    while (changed) {
      changed = false;
      while (!text.empty() && detail::isDigit(text.back())) {
        text.remove_suffix(1U);
        changed = true;
      }
      if (changed && options_.treat_dot_as_decimal && !text.empty() && text.back() == '.') {
        text.remove_suffix(1U);
      }
    }
    return std::string{text};
  }

  /**
   * @brief ソート済み文字列配列上で接頭辞一致件数を二分探索で数える
   * @param sorted ソート済み文字列一覧
   * @param prefix 調べる接頭辞
   * @return 一致件数
   */
  [[nodiscard]]
  static auto countPrefixMatches(std::vector<std::string> const& sorted, std::string_view const prefix) -> std::size_t {
    if (prefix.empty()) {
      return sorted.size();
    }
    auto const first = std::lower_bound(sorted.begin(), sorted.end(), prefix);
    auto sentinel = std::string{prefix};
    ++sentinel.back();
    auto const last = std::lower_bound(sorted.begin(), sorted.end(), sentinel);
    return static_cast<std::size_t>(last - first);
  }

  /**
   * @brief 閾値を満たす最大長のベース名を選ぶ
   * @param sorted ソート済み文字列一覧
   * @param total_count 全体数
   * @return ベース名
   */
  [[nodiscard]]
  auto chooseBaseName(std::vector<std::string> const& sorted, std::size_t const total_count) const -> std::string {
    if (sorted.empty()) {
      return {};
    }

    auto const threshold  = coverageThreshold(total_count);
    if (threshold == 0) {
      return {};
    }
    // sorted.size() が threshold より小さい場合、スライディングウィンドウは組めない
    if (sorted.size() < threshold) {
      return {};
    }

    auto const window_end = sorted.size() - threshold + 1U;
    auto       best       = std::string{};
    auto       best_count = std::size_t{0U};

    for (auto const start : std::views::iota(std::size_t{0U}, window_end)) {
      auto const& min_name  = sorted[start];
      auto const& max_name  = sorted[start + threshold - 1U];
      auto const  raw_lcp   = longestCommonPrefix(min_name, max_name);
      auto const  candidate = trimTrailingNumericParts(raw_lcp);

      auto low  = std::size_t{0U};
      auto high = candidate.size();
      while (low < high) {
        auto const mid   = (low + high + 1U) / 2U;
        auto const count = countPrefixMatches(sorted, std::string_view{candidate}.substr(0U, mid));
        if (count >= threshold) {
          low = mid;
        } else {
          high = mid - 1U;
        }
      }

      auto const verified = candidate.substr(0U, low);
      auto const matches  = verified.empty() ? sorted.size() : countPrefixMatches(sorted, verified);
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
    if (suffix.empty() || !detail::isDigit(suffix.front())) {
      return std::nullopt;
    }

    auto indices = std::vector<double>{};
    auto ptr     = suffix.data();
    auto end     = suffix.data() + suffix.size();

    while (ptr < end) {
      if (detail::isDigit(*ptr) || (treat_dot_as_decimal && *ptr == '.')) {
#if defined(MOJITONPP_FREESTANDING)
        auto val = 0.0;
        if (auto const parsed = detail::parseDecimal(ptr, end, val); parsed != nullptr) {
          indices.push_back(val);
          ptr = parsed;
        } else if (detail::isDigit(*ptr)) {
          // 整数としてのフォールバック
          auto const start_ptr = ptr;
          while (ptr < end && detail::isDigit(*ptr)) {
            ++ptr;
          }
          auto val_int = std::uint64_t{};
          if (detail::parseUint64(start_ptr, ptr, val_int) != nullptr) {
            indices.push_back(static_cast<double>(val_int));
          }
        } else {
          ++ptr;
        }
#else
        auto  val      = 0.0;
        auto const res = detail::fromChars(ptr, end, val);
        if (res.ec == std::errc{}) {
          indices.push_back(val);
          ptr = res.ptr;
        } else if (detail::isDigit(*ptr)) {
          // 整数としてのフォールバック
          auto const start_ptr = ptr;
          while (ptr < end && detail::isDigit(*ptr)) {
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
#endif
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
