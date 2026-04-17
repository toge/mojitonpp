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
  auto const results  = detector.detect(items);

  REQUIRE(results.size() == 1U);
  CHECK(results[0].base_name == "episode_");
  CHECK(results[0].matched_count == 90U);
}

TEST_CASE("複数系列を同時に検出できる", "[sequence]") {
  auto items = makeSequence("shot_A_", 1, 40);
  auto b_items = makeSequence("shot_B_", 1, 40);
  items.insert(items.end(), b_items.begin(), b_items.end());
  
  items.emplace_back("noise1.txt");
  items.emplace_back("noise2.txt");

  // 閾値を 0.3 に下げて、各 40% 占める系列を見つけられるようにする
  auto const detector = mojitonpp::SequenceDetector{mojitonpp::DetectorOptions{.threshold = 0.3}};
  auto const results  = detector.detect(items);

  REQUIRE(results.size() == 2U);
  
  auto const has_base = [&](std::string_view name) {
    return std::ranges::any_of(results, [name](auto const& r) { return r.base_name == name; });
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
    auto const results = detector.detect(items);
    REQUIRE(results.size() == 1U);
    CHECK(results[0].base_name == "frame_");
    for (auto const& item : results[0].items) {
      CHECK(item.value.ends_with(".png"));
    }
  }

  SECTION(".jpg のみを対象にする") {
    auto const detector = mojitonpp::SequenceDetector{mojitonpp::DetectorOptions{
      .threshold = 0.9,
      .allowed_extensions = {".jpg"}
    }};
    auto const results = detector.detect(items);
    REQUIRE(results.size() == 1U);
    CHECK(results[0].base_name == "image_");
  }
}

TEST_CASE("自然順でソートされる", "[sequence]") {
  auto const items = std::vector<std::string>{
    "frame10.png",
    "frame2.jpg",
    "frame1.png",
  };

  auto const detector = mojitonpp::SequenceDetector{};
  auto const results  = detector.detect(items);

  REQUIRE(results.size() == 1U);
  REQUIRE(results[0].items.size() == 3U);
  CHECK(results[0].items[0].indices.front() == 1.0);
  CHECK(results[0].items[1].indices.front() == 2.0);
  CHECK(results[0].items[2].indices.front() == 10.0);
}

TEST_CASE("メタデータ（除外対象）判定の強化", "[metadata]") {
  CHECK(mojitonpp::isMetadata(".gitignore"));
  CHECK(mojitonpp::isMetadata("Thumbs.db"));
  CHECK(mojitonpp::isMetadata("desktop.ini"));
  CHECK_FALSE(mojitonpp::isMetadata("README.md"));
}
