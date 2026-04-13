#include "catch2/catch_all.hpp"

#include "mojitonpp.hpp"

#include <filesystem>
#include <format>
#include <ranges>
#include <string>
#include <vector>

namespace {

[[nodiscard]] auto makeSequence(std::string_view const prefix, int const begin, int const end) -> std::vector<std::filesystem::path> {
  auto files = std::vector<std::filesystem::path>{};
  files.reserve(static_cast<std::size_t>(end - begin + 1));

  for (auto const value : std::views::iota(begin, end + 1)) {
    auto const extension = value % 2 == 0 ? ".png" : ".jpg";
    files.emplace_back(std::filesystem::path{std::format("{}{:03}{}", prefix, value, extension)});
  }

  return files;
}

}  // namespace

TEST_CASE("支配的な連番系列を安定して検出できる", "[sequence]") {
  auto files = makeSequence("episode_", 1, 90);
  files.emplace_back("aaa-noise.bin");
  files.emplace_back("zzz-noise.bin");
  files.emplace_back("misc-file.txt");
  files.emplace_back("episode_notes.bin");
  files.emplace_back("another.out");
  files.emplace_back("episode-index.dat");
  files.emplace_back("noise000.tmp");
  files.emplace_back("x.bin");
  files.emplace_back("y.bin");
  files.emplace_back("z.bin");

  auto const detector = mojitonpp::SequenceDetector{};
  auto const result   = detector.detect(files);

  REQUIRE(result.has_value());
  CHECK(result->base_name == "episode_");
  CHECK(result->matched_file_count == 90U);
  CHECK(result->eligible_file_count == 100U);
  CHECK(result->files.front().indices.front() == 1.0);
  CHECK(result->files.back().indices.front() == 90.0);
}

TEST_CASE("自然順でソートされる", "[sequence]") {
  auto const files = std::vector<std::filesystem::path>{
    "frame10.png",
    "frame2.jpg",
    "frame1.png",
  };

  auto const detector = mojitonpp::SequenceDetector{};
  auto const result   = detector.detect(files);

  REQUIRE(result.has_value());
  REQUIRE(result->files.size() == 3U);
  CHECK(result->files[0].indices.front() == 1.0);
  CHECK(result->files[1].indices.front() == 2.0);
  CHECK(result->files[2].indices.front() == 10.0);
}

TEST_CASE("先頭の数値から順に比較される", "[sequence]") {
  auto const files = std::vector<std::filesystem::path>{
    "v1.2.3.png",
    "v1.5.png",
    "v1.5.5.png",
    "v2.1.png",
  };

  auto const detector = mojitonpp::SequenceDetector{};
  auto const result   = detector.detect(files);

  REQUIRE(result.has_value());
  REQUIRE(result->files.size() == 4U);
  // v1.2.3 vs v1.5   -> 1==1, 2<5 -> v1.2.3 is smaller
  // v1.5 vs v1.5.5   -> 1==1, 5==5, prefix matches -> v1.5 is smaller
  // v1.5.5 vs v2.1   -> 1<2 -> v1.5.5 is smaller
  CHECK(result->files[0].filename == "v1.2.3.png");
  CHECK(result->files[1].filename == "v1.5.png");
  CHECK(result->files[2].filename == "v1.5.5.png");
  CHECK(result->files[3].filename == "v2.1.png");
}

TEST_CASE("小数点として比較するか選択できる", "[sequence]") {
  auto const files = std::vector<std::filesystem::path>{
    "frame1.20.png",
    "frame1.3.png",
  };

  SECTION("別々の数値として比較（デフォルト）") {
    auto const detector = mojitonpp::SequenceDetector{};
    auto const result   = detector.detect(files);
    REQUIRE(result.has_value());
    // 1.3 -> [1, 3]
    // 1.20 -> [1, 20]
    CHECK(result->files[0].filename == "frame1.3.png");
    CHECK(result->files[1].filename == "frame1.20.png");
  }

  SECTION("小数点として比較") {
    auto const detector = mojitonpp::SequenceDetector{mojitonpp::DetectorOptions{.treat_dot_as_decimal = true}};
    auto const result   = detector.detect(files);
    REQUIRE(result.has_value());
    // 1.3 -> [1.3]
    // 1.20 -> [1.2] (Note: std::strtod parses 1.20 as 1.2)
    CHECK(result->files[0].filename == "frame1.20.png");
    CHECK(result->files[1].filename == "frame1.3.png");
  }
}

TEST_CASE("閾値を指定できる", "[sequence]") {
  auto files = makeSequence("shot_", 1, 8);
  files.emplace_back("noise_a.bin");
  files.emplace_back("noise_b.bin");

  SECTION("デフォルト(90%)では失敗") {
    auto const detector = mojitonpp::SequenceDetector{};
    auto const result   = detector.detect(files);
    CHECK_FALSE(result.has_value());
  }

  SECTION("80% を指定すれば成功") {
    auto const detector = mojitonpp::SequenceDetector{mojitonpp::DetectorOptions{.threshold = 0.8}};
    auto const result   = detector.detect(files);
    CHECK(result.has_value());
  }
}

TEST_CASE("メタデータ系ファイル判定の変更", "[metadata]") {
  // 拡張子や接頭辞による判定は行わなくなった
  CHECK_FALSE(mojitonpp::isMetadataFile("README.md"));
  CHECK_FALSE(mojitonpp::isMetadataFile("vcpkg.json"));
  CHECK_FALSE(mojitonpp::isMetadataFile("config.yaml"));

  // ドットで始まるファイルは除外される
  CHECK(mojitonpp::isMetadataFile(".gitignore"));
  CHECK(mojitonpp::isMetadataFile(".config"));
}
