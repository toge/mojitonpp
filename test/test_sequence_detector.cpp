#include "catch2/catch_all.hpp"

#include "mojitonpp.hpp"

#include <format>
#include <ranges>
#include <string>
#include <vector>

namespace {

[[nodiscard]] auto makeSequence(std::string_view const prefix, int const begin, int const end, std::string_view const ext = ".png") -> std::vector<std::string> {
  auto items = std::vector<std::string>{};
  items.reserve(static_cast<std::size_t>(end - begin + 1));

  for (auto const value : std::views::iota(begin, end + 1)) {
    items.emplace_back(std::format("{}{:03}{}", prefix, value, ext));
  }

  return items;
}

}  // namespace

TEST_CASE("支配的な連番系列を安定して検出できる", "[sequence]") {
  auto items = makeSequence("episode_", 1, 90);
  items.emplace_back("aaa-noise.bin");
  items.emplace_back("zzz-noise.bin");
  items.emplace_back("misc-file.txt");

  auto const detector = mojitonpp::SequenceDetector{};
  auto const result   = detector.detect(items);

  REQUIRE(result.sequences.size() == 1U);
  CHECK(result.sequences[0].base_name == "episode_");
  CHECK(result.sequences[0].matched_count == 90U);
}

TEST_CASE("複数系列を同時に検出できる", "[sequence]") {
  auto items = makeSequence("shot_A_", 1, 40);
  auto b_items = makeSequence("shot_B_", 1, 40);
  items.insert(items.end(), b_items.begin(), b_items.end());
  
  items.emplace_back("noise1.txt");
  items.emplace_back("noise2.txt");

  // 閾値を 0.3 に下げて、各 40% 占める系列を見つけられるようにする
  auto const detector = mojitonpp::SequenceDetector{mojitonpp::DetectorOptions{.threshold = 0.3}};
  auto const result   = detector.detect(items);

  REQUIRE(result.sequences.size() == 2U);
  
  auto const has_base = [&](std::string_view name) {
    return std::ranges::any_of(result.sequences, [name](auto const& r) { return r.base_name == name; });
  };
  
  CHECK(has_base("shot_A_"));
  CHECK(has_base("shot_B_"));
}

TEST_CASE("拡張子でフィルタリングできる", "[filter]") {
  auto items = makeSequence("frame_", 1, 10, ".png");
  auto jpgs = makeSequence("image_", 1, 10, ".jpg");
  items.insert(items.end(), jpgs.begin(), jpgs.end());

  SECTION(".png のみを対象にする") {
    auto const detector = mojitonpp::SequenceDetector{mojitonpp::DetectorOptions{
      .threshold = 0.9,
      .allowed_extensions = {".png"}
    }};
    auto const result = detector.detect(items);
    REQUIRE(result.sequences.size() == 1U);
    CHECK(result.sequences[0].base_name == "frame_");
    for (auto const& item : result.sequences[0].items) {
      CHECK(item.value.ends_with(".png"));
    }
  }

  SECTION(".jpg のみを対象にする") {
    auto const detector = mojitonpp::SequenceDetector{mojitonpp::DetectorOptions{
      .threshold = 0.9,
      .allowed_extensions = {".jpg"}
    }};
    auto const result = detector.detect(items);
    REQUIRE(result.sequences.size() == 1U);
    CHECK(result.sequences[0].base_name == "image_");
  }
}

TEST_CASE("自然順でソートされる", "[sequence]") {
  auto const items = std::vector<std::string>{
    "frame10.png",
    "frame2.jpg",
    "frame1.png",
  };

  auto const detector = mojitonpp::SequenceDetector{};
  auto const result   = detector.detect(items);

  REQUIRE(result.sequences.size() == 1U);
  REQUIRE(result.sequences[0].items.size() == 3U);
  CHECK(result.sequences[0].items[0].indices.front() == 1.0);
  CHECK(result.sequences[0].items[1].indices.front() == 2.0);
  CHECK(result.sequences[0].items[2].indices.front() == 10.0);
}

TEST_CASE("メタデータ（除外対象）判定の強化", "[metadata]") {
  CHECK(mojitonpp::isMetadata(".gitignore"));
  CHECK(mojitonpp::isMetadata("Thumbs.db"));
  CHECK(mojitonpp::isMetadata("desktop.ini"));
  CHECK_FALSE(mojitonpp::isMetadata("README.md"));
  CHECK(mojitonpp::isMetadata(""));
}

TEST_CASE("空の入力は空の結果を返す", "[edge]") {
  auto const detector = mojitonpp::SequenceDetector{};
  CHECK(detector.detect(std::vector<std::string>{}).sequences.empty());
}

TEST_CASE("最小限の入力（2件）で系列を検出できる", "[edge]") {
  auto const items = std::vector<std::string>{
    "file_001.txt",
    "file_002.txt",
  };
  auto const detector = mojitonpp::SequenceDetector{};
  auto const result   = detector.detect(items);
  REQUIRE(result.sequences.size() == 1U);
  CHECK(result.sequences[0].base_name == "file_");
  CHECK(result.sequences[0].matched_count == 2U);
  CHECK(result.sequences[0].items[0].indices.front() == 1.0);
  CHECK(result.sequences[0].items[1].indices.front() == 2.0);
}

TEST_CASE("数字を含まない文字列は系列にならない", "[edge]") {
  auto const items = std::vector<std::string>{
    "alpha.txt",
    "beta.txt",
    "gamma.txt",
  };
  auto const detector = mojitonpp::SequenceDetector{};
  auto const result   = detector.detect(items);
  // 共通接頭辞はあるが数字が末尾にない → 空のベース名を返す場合がある
  CHECK(result.sequences.empty());
}

TEST_CASE("treat_dot_as_decimal で小数点を含む連番を扱える", "[decimal]") {
  auto const items = std::vector<std::string>{
    "data_1.5.csv",
    "data_2.5.csv",
    "data_3.5.csv",
  };
  auto const detector = mojitonpp::SequenceDetector{mojitonpp::DetectorOptions{
    .treat_dot_as_decimal = true,
  }};
  auto const result = detector.detect(items);
  REQUIRE(result.sequences.size() == 1U);
  CHECK(result.sequences[0].base_name == "data_");
  REQUIRE(result.sequences[0].items.size() == 3U);
  CHECK(result.sequences[0].items[0].indices.front() == 1.5);
  CHECK(result.sequences[0].items[1].indices.front() == 2.5);
  CHECK(result.sequences[0].items[2].indices.front() == 3.5);
}

TEST_CASE("全件が同一系列の 100% カバレッジでも動作する", "[edge]") {
  auto items = std::vector<std::string>{};
  for (auto const i : std::views::iota(1, 11)) {
    items.emplace_back(std::format("item_{:03}.png", i));
  }
  auto const detector = mojitonpp::SequenceDetector{};
  auto const result   = detector.detect(items);
  REQUIRE(result.sequences.size() == 1U);
  CHECK(result.sequences[0].base_name == "item_");
  CHECK(result.sequences[0].matched_count == 10U);
}

TEST_CASE("除外された文字列を取得できる", "[excluded]") {
  auto items = makeSequence("episode_", 1, 90);
  items.emplace_back("aaa-noise.bin");
  items.emplace_back("zzz-noise.bin");
  items.emplace_back("misc-file.txt");

  auto const detector = mojitonpp::SequenceDetector{};
  auto const result   = detector.detect(items);

  REQUIRE(result.sequences.size() == 1U);
  REQUIRE(result.excluded.size() == 3U);
  CHECK(result.excluded[0] == "aaa-noise.bin");
  CHECK(result.excluded[1] == "misc-file.txt");
  CHECK(result.excluded[2] == "zzz-noise.bin");
}

TEST_CASE("全件が系列の場合は除外リストが空", "[excluded]") {
  auto const items = std::vector<std::string>{
    "file_001.txt",
    "file_002.txt",
  };
  auto const detector = mojitonpp::SequenceDetector{};
  auto const result   = detector.detect(items);
  REQUIRE(result.sequences.size() == 1U);
  CHECK(result.excluded.empty());
}
