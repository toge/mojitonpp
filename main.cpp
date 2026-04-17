#include "mojitonpp.hpp"

#include "glaze/glaze.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <print>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct cli_options {
  std::filesystem::path    directory{"."};
  bool                     json_output{};
  bool                     verbose{};
  double                   threshold{0.9};
  bool                     dot_as_decimal{};
  std::vector<std::string> extensions{};
};

struct json_file_entry {
  std::string         filename;
  std::vector<double> indices;
};

struct json_sequence_report {
  std::string                  base_name;
  std::size_t                  matched_file_count{};
  double                       coverage{};
  std::vector<json_file_entry> files;
};

struct json_root_report {
  std::string                       directory;
  std::size_t                       eligible_file_count{};
  std::vector<json_sequence_report> sequences;
};

/**
 * @brief コマンドライン引数を解析する
 * @param args 引数列
 * @return 解析済みオプション 失敗時は `std::nullopt` を返す
 */
[[nodiscard]]
auto parseArgs(std::span<char* const> const args) -> std::optional<cli_options> {
  auto options = cli_options{};

  for (auto index = std::size_t{1U}; index < args.size(); ++index) {
    auto const arg = std::string_view{args[index]};
    if (arg == "-h" || arg == "--help") {
      std::print("使い方: sequence_detector [options] <directory>\n");
      std::print("オプション:\n");
      std::print("  --json                 JSON 形式で出力する\n");
      std::print("  --verbose              詳細な情報を出力する\n");
      std::print("  --threshold <double>   系列とみなす閾値 (デフォルト: 0.9)\n");
      std::print("  --dot-as-decimal       ドットを小数点として扱う\n");
      std::print("  --extension <ext>      対象とする拡張子 (例: .png)\n");
      return std::nullopt;
    }
    if (arg == "--json") {
      options.json_output = true;
      continue;
    }
    if (arg == "--verbose") {
      options.verbose = true;
      continue;
    }
    if (arg == "--dot-as-decimal") {
      options.dot_as_decimal = true;
      continue;
    }
    if (arg == "--threshold") {
      if (index + 1U >= args.size()) {
        std::cerr << "--threshold には値を指定してください。\n";
        return std::nullopt;
      }
      options.threshold = std::stod(args[++index]);
      continue;
    }
    if (arg == "--extension") {
      if (index + 1U >= args.size()) {
        std::cerr << "--extension には拡張子を指定してください。\n";
        return std::nullopt;
      }
      options.extensions.emplace_back(args[++index]);
      continue;
    }
    if (arg.starts_with('-')) {
      std::cerr << "不明なオプションです: " << arg << '\n';
      return std::nullopt;
    }
    if (options.directory != std::filesystem::path{"."}) {
      std::cerr << "ディレクトリは 1 つだけ指定してください。\n";
      return std::nullopt;
    }
    options.directory = std::filesystem::path{arg};
  }

  return options;
}

/**
 * @brief 対象ディレクトリから検出候補ファイルを収集する
 * @param directory 対象ディレクトリ
 * @param verbose 詳細な情報を出力するかどうか
 * @return 収集した通常ファイル名一覧
 */
[[nodiscard]]
auto collectCandidateFilenames(std::filesystem::path const& directory, bool verbose) -> std::vector<std::string> {
  auto filenames = std::vector<std::string>{};

  for (auto const& entry : std::filesystem::directory_iterator{directory}) {
    if (!entry.is_regular_file()) {
      continue;
    }

    auto const filename = entry.path().filename().string();
    if (mojitonpp::isMetadata(filename)) {
      if (verbose) {
        std::cout << "Skip metadata file " << filename << '\n';
      }
      continue;
    }
    filenames.emplace_back(filename);
  }

  return filenames;
}

/**
 * @brief JSON 出力用の構造体へ変換する
 * @param directory 対象ディレクトリ
 * @param eligible_count 対象ファイル総数
 * @param results 検出結果リスト
 * @return JSON 直列化用のレポート
 */
[[nodiscard]]
auto makeJsonReport(std::filesystem::path const& directory, std::size_t eligible_count, std::vector<mojitonpp::detection_result> const& results) -> json_root_report {
  auto sequences = std::vector<json_sequence_report>{};
  sequences.reserve(results.size());

  for (auto const& result : results) {
    auto files = std::vector<json_file_entry>{};
    files.reserve(result.items.size());
    for (auto const& item : result.items) {
      files.push_back(json_file_entry{
        .filename = item.value,
        .indices  = item.indices,
      });
    }

    sequences.push_back(json_sequence_report{
      .base_name           = result.base_name,
      .matched_file_count  = result.matched_count,
      .coverage            = result.coverage(),
      .files               = std::move(files),
    });
  }

  return json_root_report{
    .directory           = std::filesystem::absolute(directory).string(),
    .eligible_file_count = eligible_count,
    .sequences           = std::move(sequences),
  };
}

/**
 * @brief 人間向けの検出結果を表示する
 * @param directory 対象ディレクトリ
 * @param results 検出結果リスト
 */
auto printHumanReadable(std::filesystem::path const& directory, std::vector<mojitonpp::detection_result> const& results) {
  std::print("対象ディレクトリ: {}\n", std::filesystem::absolute(directory).string());
  if (results.empty()) {
    std::print("系列は検出されませんでした。\n");
    return;
  }

  for (auto const& [index, result] : std::views::enumerate(results)) {
    auto const base_name = result.base_name.empty() ? std::string{"(空文字列)"} : result.base_name;
    std::print("\n--- 系列 #{} ---\n", index + 1);
    std::print("ベース名: {}\n", base_name);
    std::print("検出件数: {}/{} ({:.2f}%)\n", result.matched_count, result.eligible_count, result.coverage() * 100.0);
    std::print("連番ファイル一覧:\n");
    for (auto const& item : result.items) {
      auto indices_str = std::string{};
      for (auto const val : item.indices) {
        if (!indices_str.empty()) {
          indices_str += ", ";
        }
        indices_str += std::format("{}", val);
      }
      std::print("[{:>12}]  {}\n", indices_str, item.value);
    }
  }
}

}  // namespace

auto main(int argc, char* argv[]) -> int {
  auto const args = std::span<char* const>{argv, static_cast<std::size_t>(argc)};
  auto const options = parseArgs(args);
  if (!options) {
    return EXIT_FAILURE;
  }

  if (!std::filesystem::exists(options->directory)) {
    std::cerr << "指定ディレクトリが存在しません: " << options->directory.string() << '\n';
    return EXIT_FAILURE;
  }
  if (!std::filesystem::is_directory(options->directory)) {
    std::cerr << "指定パスはディレクトリではありません: " << options->directory.string() << '\n';
    return EXIT_FAILURE;
  }

  auto const filenames = collectCandidateFilenames(options->directory, options->verbose);

  if (options->verbose) {
    std::cout << "Collected " << filenames.size() << " candidate files from " << std::filesystem::absolute(options->directory).string() << '\n';
  }

  auto const detector = mojitonpp::SequenceDetector{mojitonpp::DetectorOptions{
    .threshold            = options->threshold,
    .treat_dot_as_decimal = options->dot_as_decimal,
    .allowed_extensions   = options->extensions,
  }};
  auto const results  = detector.detect(filenames);

  if (options->json_output) {
    // フィルタリング後の実際の対象ファイル数を計算
    auto filtered_count = std::size_t{0};
    for (auto const& f : filenames) {
        if (options->extensions.empty()) {
            filtered_count++;
        } else {
            if (std::ranges::any_of(options->extensions, [&](auto const& ext) { return f.ends_with(ext); })) {
                filtered_count++;
            }
        }
    }

    auto report = makeJsonReport(options->directory, filtered_count, results);
    auto buffer = std::string{};
    if (auto const ec = glz::write<glz::opts{.prettify = true}>(report, buffer); ec) {
      std::cerr << "JSON 出力の生成に失敗しました。\n";
      return EXIT_FAILURE;
    }
    std::print("{}\n", buffer);
    return EXIT_SUCCESS;
  }

  printHumanReadable(options->directory, results);
  return results.empty() ? EXIT_FAILURE : EXIT_SUCCESS;
}
