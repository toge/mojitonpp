#include "catch2/catch_all.hpp"

#include "mojitonpp.hpp"

#include <format>
#include <random>
#include <ranges>
#include <string>
#include <vector>

namespace {

[[nodiscard]] auto makeSequence(std::string_view prefix, int begin, int end, std::string_view ext = ".png") -> std::vector<std::string> {
  auto items = std::vector<std::string>{};
  items.reserve(static_cast<std::size_t>(end - begin + 1));
  for (auto const value : std::views::iota(begin, end + 1)) {
    items.emplace_back(std::format("{}{:03}{}", prefix, value, ext));
  }
  return items;
}

[[nodiscard]] auto makeNoise(std::size_t count) -> std::vector<std::string> {
  auto rng = std::mt19937{42};
  auto dist = std::uniform_int_distribution<int>{0, 999999};
  auto items = std::vector<std::string>{};
  items.reserve(count);
  for (auto const _ : std::views::iota(std::size_t{0}, count)) {
    items.emplace_back(std::format("noise_{:06}.tmp", dist(rng)));
  }
  return items;
}

}  // namespace

TEST_CASE("100 files with 10% noise", "[benchmark][small]") {
  auto items = makeSequence("frame_", 1, 90);
  auto noise = makeNoise(10);
  items.insert(items.end(), noise.begin(), noise.end());
  std::ranges::shuffle(items, std::mt19937{42});

  auto const detector = mojitonpp::SequenceDetector{};
  BENCHMARK("detect 100 items") {
    return detector.detect(items);
  };
}

TEST_CASE("500 files with 10% noise", "[benchmark][medium]") {
  auto items = makeSequence("frame_", 1, 450);
  auto noise = makeNoise(50);
  items.insert(items.end(), noise.begin(), noise.end());
  std::ranges::shuffle(items, std::mt19937{42});

  auto const detector = mojitonpp::SequenceDetector{};
  BENCHMARK("detect 500 items") {
    return detector.detect(items);
  };
}

TEST_CASE("1000 files with 10% noise", "[benchmark][large]") {
  auto items = makeSequence("frame_", 1, 900);
  auto noise = makeNoise(100);
  items.insert(items.end(), noise.begin(), noise.end());
  std::ranges::shuffle(items, std::mt19937{42});

  auto const detector = mojitonpp::SequenceDetector{};
  BENCHMARK("detect 1000 items") {
    return detector.detect(items);
  };
}

TEST_CASE("5000 files with 10% noise", "[benchmark][huge]") {
  auto items = makeSequence("frame_", 1, 4500);
  auto noise = makeNoise(500);
  items.insert(items.end(), noise.begin(), noise.end());
  std::ranges::shuffle(items, std::mt19937{42});

  auto const detector = mojitonpp::SequenceDetector{};
  BENCHMARK("detect 5000 items") {
    return detector.detect(items);
  };
}

TEST_CASE("Two sequences 500+500 with noise", "[benchmark][multi]") {
  auto items = makeSequence("shot_A_", 1, 500);
  auto b_items = makeSequence("shot_B_", 1, 500);
  items.insert(items.end(), b_items.begin(), b_items.end());
  auto noise = makeNoise(100);
  items.insert(items.end(), noise.begin(), noise.end());
  std::ranges::shuffle(items, std::mt19937{42});

  auto const detector = mojitonpp::SequenceDetector{mojitonpp::DetectorOptions{.threshold = 0.45}};
  BENCHMARK("detect multi 1100 items") {
    return detector.detect(items);
  };
}
