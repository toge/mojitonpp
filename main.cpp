#include "mojitonpp.hpp"

#include "glaze/glaze.hpp"

#include <cstdlib>
#include <filesystem>
#include <format>
#include <iostream>
#include <optional>

#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct cli_options {
  std::filesystem::path    directory{"."};
  bool                     json_output{};
  bool                     verbose{};
  double                   threshold{0.9};
  bool                     dot_as_decimal{};
  bool                     recursive{};
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

namespace {

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
      std::cout << "使い方: sequence_detector [options] <directory>\n";
      std::cout << "オプション:\n";
      std::cout << "  --json                 JSON 形式で出力する\n";
      std::cout << "  --verbose              詳細な情報を出力する\n";
      std::cout << "  --threshold <double>   系列とみなす閾値 (デフォルト: 0.9)\n";
      std::cout << "  --dot-as-decimal       ドットを小数点として扱う\n";
      std::cout << "  --recursive            サブディレクトリも再帰的に走査する\n";
      std::cout << "  --extension <ext>      対象とする拡張子 (例: .png)\n";
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
    if (arg == "--recursive") {
      options.recursive = true;
      continue;
    }
    if (arg == "--threshold") {
      if (index + 1U >= args.size()) {
        std::cerr << "--threshold には値を指定してください。\n";
        return std::nullopt;
      }
      auto const threshold_arg = std::string_view{args[++index]};
      auto [ptr, ec] = std::from_chars(threshold_arg.data(), threshold_arg.data() + threshold_arg.size(), options.threshold);
      if (ec != std::errc{}) {
        std::cerr << "--threshold には数値を指定してください。\n";
        return std::nullopt;
      }
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
 * @param recursive サブディレクトリを再帰的に走査するかどうか
 * @return 収集した通常ファイル名一覧
 */
[[nodiscard]]
auto collectCandidateFilenames(std::filesystem::path const& directory, bool verbose, bool recursive) -> std::vector<std::string> {
  auto filenames = std::vector<std::string>{};

  auto collect = [&](auto&& iterator) {
    for (auto const& entry : iterator) {
      if (!entry.is_regular_file()) {
        continue;
      }
      auto const filename = entry.path().filename().string();
      if (mojitonpp::isMetadata(filename)) {
        if (verbose) {
          std::cout << std::format("Skip metadata file {}\n", filename);
        }
        continue;
      }
      filenames.emplace_back(filename);
    }
  };

  if (recursive) {
    collect(std::filesystem::recursive_directory_iterator{directory});
  } else {
    collect(std::filesystem::directory_iterator{directory});
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
  std::cout << std::format("対象ディレクトリ: {}\n", std::filesystem::absolute(directory).string());
  if (results.empty()) {
    std::cout << "系列は検出されませんでした。\n";
    return;
  }

  for (auto const& [index, result] : std::views::enumerate(results)) {
    auto const base_name = result.base_name.empty() ? std::string{"(空文字列)"} : result.base_name;
    std::cout << std::format("\n--- 系列 #{} ---\n", index + 1);
    std::cout << std::format("ベース名: {}\n", base_name);
    std::cout << std::format("検出件数: {}/{} ({:.2f}%)\n", result.matched_count, result.eligible_count, result.coverage() * 100.0);
    std::cout << "連番ファイル一覧:\n";
    for (auto const& item : result.items) {
      auto indices_str = std::string{};
      for (auto const val : item.indices) {
        if (!indices_str.empty()) {
          indices_str += ", ";
        }
        indices_str += std::format("{}", val);
      }
      std::cout << std::format("[{:>12}]  {}\n", indices_str, item.value);
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

  auto const filenames = collectCandidateFilenames(options->directory, options->verbose, options->recursive);

  if (options->verbose) {
    std::cout << std::format("Collected {} candidate files from {}\n", filenames.size(), std::filesystem::absolute(options->directory).string());
  }

  auto const detector = mojitonpp::SequenceDetector{mojitonpp::DetectorOptions{
    .threshold            = options->threshold,
    .treat_dot_as_decimal = options->dot_as_decimal,
    .allowed_extensions   = options->extensions,
  }};
  auto const results  = detector.detect(filenames);

  if (options->json_output) {
    auto const eligible_count = [&]() -> std::size_t {
      if (options->extensions.empty()) {
        return filenames.size();
      }
      return std::ranges::count_if(filenames, [&](auto const& f) {
        return std::ranges::any_of(options->extensions, [&](auto const& ext) { return f.ends_with(ext); });
      });
    }();

    auto report = makeJsonReport(options->directory, eligible_count, results);
    auto buffer = std::string{};
    if (auto const ec = glz::write<glz::opts{.prettify = true}>(report, buffer); ec) {
      std::cerr << "JSON 出力の生成に失敗しました。\n";
      return EXIT_FAILURE;
    }
    std::cout << buffer << '\n';
    return EXIT_SUCCESS;
  }

  printHumanReadable(options->directory, results);
  return EXIT_SUCCESS;
}
