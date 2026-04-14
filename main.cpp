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
  std::filesystem::path directory{"."};
  bool                  json_output{};
  bool                  verbose{};
  double                threshold{0.9};
  bool                  dot_as_decimal{};
};

struct json_file_entry {
  std::string         filename;
  std::vector<double> indices;
};

struct json_report {
  std::string                  directory;
  std::string                  base_name;
  std::size_t                  eligible_file_count{};
  std::size_t                  matched_file_count{};
  double                       coverage{};
  std::vector<json_file_entry> files;
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
      std::print("使い方: sequence_detector [--json] [--verbose] [--threshold <double>] [--dot-as-decimal] <directory>\n");
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
 * @param result 検出結果
 * @return JSON 直列化用のレポート
 */
[[nodiscard]]
auto makeJsonReport(std::filesystem::path const& directory, mojitonpp::detection_result const& result) -> json_report {
  auto files = std::vector<json_file_entry>{};
  files.reserve(result.items.size());
  for (auto const& item : result.items) {
    files.push_back(json_file_entry{
      .filename = item.value,
      .indices  = item.indices,
    });
  }

  return json_report{
    .directory           = std::filesystem::absolute(directory).string(),
    .base_name           = result.base_name,
    .eligible_file_count = result.eligible_count,
    .matched_file_count  = result.matched_count,
    .coverage            = result.coverage(),
    .files               = std::move(files),
  };
}

/**
 * @brief 人間向けの検出結果を表示する
 * @param directory 対象ディレクトリ
 * @param result 検出結果
 */
auto printHumanReadable(std::filesystem::path const& directory, mojitonpp::detection_result const& result) {
  auto const base_name = result.base_name.empty() ? std::string{"(空文字列)"} : result.base_name;

  std::print("対象ディレクトリ: {}\n", std::filesystem::absolute(directory).string());
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

}  // namespace

auto main(int argc, char* argv[]) -> int {
  auto const args = std::span<char* const>{argv, static_cast<std::size_t>(argc)};
  if (std::ranges::any_of(args.subspan(std::min<std::size_t>(1U, args.size())), [](char const* const arg) {
        auto const value = std::string_view{arg};
        return value == "-h" || value == "--help";
      })) {
    std::print("使い方: sequence_detector [--json] [--verbose] [--threshold <double>] [--dot-as-decimal] <directory>\n");
    return EXIT_SUCCESS;
  }

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
  }};
  auto const result   = detector.detect(filenames);
  if (!result) {
    std::cerr << "90%以上を占める連番系列を検出できませんでした。\n";
    return EXIT_FAILURE;
  }

  if (options->verbose) {
    std::cout << "Detected base name '" << result->base_name << "' with " << result->matched_count << " files\n";
  }

  if (options->json_output) {
    auto report = makeJsonReport(options->directory, *result);
    auto buffer = std::string{};
    if (auto const ec = glz::write<glz::opts{.prettify = true}>(report, buffer); ec) {
      std::cerr << "JSON 出力の生成に失敗しました。\n";
      return EXIT_FAILURE;
    }
    std::print("{}\n", buffer);
    return EXIT_SUCCESS;
  }

  printHumanReadable(options->directory, *result);
  return EXIT_SUCCESS;
}
