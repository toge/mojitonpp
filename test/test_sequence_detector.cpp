#include "catch2/catch_all.hpp"

#include "mojitonpp.hpp"

#include <format>
#include <ranges>
#include <string>
#include <vector>

namespace {

[[nodiscard]] auto makeSequence(std::string_view const prefix, int const begin, int const end) -> std::vector<std::string> {
  auto items = std::vector<std::string>{};
  items.reserve(static_cast<std::size_t>(end - begin + 1));

  for (auto const value : std::views::iota(begin, end + 1)) {
    auto const extension = value % 2 == 0 ? ".png" : ".jpg";
    items.emplace_back(std::format("{}{:03}{}", prefix, value, extension));
  }

  return items;
}

}  // namespace

TEST_CASE("支配的な連番系列を安定して検出できる", "[sequence]") {
  auto items = makeSequence("episode_", 1, 90);
  items.emplace_back("aaa-noise.bin");
  items.emplace_back("zzz-noise.bin");
  items.emplace_back("misc-file.txt");
  items.emplace_back("episode_notes.bin");
  items.emplace_back("another.out");
  items.emplace_back("episode-index.dat");
  items.emplace_back("noise000.tmp");
  items.emplace_back("x.bin");
  items.emplace_back("y.bin");
  items.emplace_back("z.bin");

  auto const detector = mojitonpp::SequenceDetector{};
  auto const result   = detector.detect(items);

  REQUIRE(result.has_value());
  CHECK(result->base_name == "episode_");
  CHECK(result->matched_count == 90U);
  CHECK(result->eligible_count == 100U);
  CHECK(result->items.front().indices.front() == 1.0);
  CHECK(result->items.back().indices.front() == 90.0);
}

TEST_CASE("自然順でソートされる", "[sequence]") {
  auto const items = std::vector<std::string>{
    "frame10.png",
    "frame2.jpg",
    "frame1.png",
  };

  auto const detector = mojitonpp::SequenceDetector{};
  auto const result   = detector.detect(items);

  REQUIRE(result.has_value());
  REQUIRE(result->items.size() == 3U);
  CHECK(result->items[0].indices.front() == 1.0);
  CHECK(result->items[1].indices.front() == 2.0);
  CHECK(result->items[2].indices.front() == 10.0);
}

TEST_CASE("先頭の数値から順に比較される", "[sequence]") {
  auto const items = std::vector<std::string>{
    "v1.2.3.png",
    "v1.5.png",
    "v1.5.5.png",
    "v2.1.png",
  };

  auto const detector = mojitonpp::SequenceDetector{};
  auto const result   = detector.detect(items);

  REQUIRE(result.has_value());
  REQUIRE(result->items.size() == 4U);
  // v1.2.3 vs v1.5   -> 1==1, 2<5 -> v1.2.3 is smaller
  // v1.5 vs v1.5.5   -> 1==1, 5==5, prefix matches -> v1.5 is smaller
  // v1.5.5 vs v2.1   -> 1<2 -> v1.5.5 is smaller
  CHECK(result->items[0].value == "v1.2.3.png");
  CHECK(result->items[1].value == "v1.5.png");
  CHECK(result->items[2].value == "v1.5.5.png");
  CHECK(result->items[3].value == "v2.1.png");
}

TEST_CASE("小数点として比較するか選択できる", "[sequence]") {
  auto const items = std::vector<std::string>{
    "frame1.20.png",
    "frame1.3.png",
  };

  SECTION("別々の数値として比較（デフォルト）") {
    auto const detector = mojitonpp::SequenceDetector{};
    auto const result   = detector.detect(items);
    REQUIRE(result.has_value());
    // 1.3 -> [1, 3]
    // 1.20 -> [1, 20]
    CHECK(result->items[0].value == "frame1.3.png");
    CHECK(result->items[1].value == "frame1.20.png");
  }

  SECTION("小数点として比較") {
    auto const detector = mojitonpp::SequenceDetector{mojitonpp::DetectorOptions{.treat_dot_as_decimal = true}};
    auto const result   = detector.detect(items);
    REQUIRE(result.has_value());
    // 1.3 -> [1.3]
    // 1.20 -> [1.2] (Note: std::strtod parses 1.20 as 1.2)
    CHECK(result->items[0].value == "frame1.20.png");
    CHECK(result->items[1].value == "frame1.3.png");
  }
}

TEST_CASE("閾値を指定できる", "[sequence]") {
  auto items = makeSequence("shot_", 1, 8);
  items.emplace_back("noise_a.bin");
  items.emplace_back("noise_b.bin");

  SECTION("デフォルト(90%)では失敗") {
    auto const detector = mojitonpp::SequenceDetector{};
    auto const result   = detector.detect(items);
    CHECK_FALSE(result.has_value());
  }

  SECTION("80% を指定すれば成功") {
    auto const detector = mojitonpp::SequenceDetector{mojitonpp::DetectorOptions{.threshold = 0.8}};
    auto const result   = detector.detect(items);
    CHECK(result.has_value());
  }
}

TEST_CASE("メタデータ（除外対象）判定の変更", "[metadata]") {
  // 拡張子や接頭辞による判定は行わなくなった
  CHECK_FALSE(mojitonpp::isMetadata("README.md"));
  CHECK_FALSE(mojitonpp::isMetadata("vcpkg.json"));
  CHECK_FALSE(mojitonpp::isMetadata("config.yaml"));

  // ドットで始まるファイルは除外される
  CHECK(mojitonpp::isMetadata(".gitignore"));
  CHECK(mojitonpp::isMetadata(".config"));
}
