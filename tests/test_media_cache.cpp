#include <atomic>
#include <map>
#include <memory>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "golden.h"
#include "mpp/media_cache.h"

namespace {

// A synthetic movie: frame N is grey N; counts how often it decodes.
class FakeSource : public mpp::MediaSource {
 public:
  explicit FakeSource(std::atomic<int>* decodes) : decodes_(decodes) {
    info_.width = 8;
    info_.height = 4;
    info_.frames = 100;
    info_.has_video = true;
  }
  const mpp::MediaInfo& info() const override { return info_; }
  mpp::Image frame(std::int64_t index) override {
    ++*decodes_;
    const auto v = static_cast<std::uint8_t>(index);
    return mpp::Image(info_.width, info_.height, {v, v, v, 255});
  }
  mpp::AudioBuffer audio(int rate) override {
    ++*decodes_;
    return {rate, std::vector<float>(rate * 2, 0.5f)};
  }

 private:
  std::atomic<int>* decodes_;
  mpp::MediaInfo info_;
};

}  // namespace

TEST_CASE("the cache decodes each frame once and evicts the oldest", "[cache]") {
  std::atomic<int> decodes{0};
  std::atomic<int> opens{0};
  const std::size_t frame_bytes = 8 * 4 * 4;
  mpp::MediaCache cache(frame_bytes * 3, [&](const std::string&) {
    ++opens;
    return std::make_unique<FakeSource>(&decodes);
  });

  CHECK(cache.frame("a", 5).pixel(0, 0).r == 5);
  CHECK(cache.frame("a", 5).pixel(0, 0).r == 5);
  CHECK(decodes == 1);
  CHECK(opens == 1);

  cache.frame("a", 6);
  cache.frame("a", 7);
  cache.frame("a", 5);  // touched, so 6 is now the oldest
  cache.frame("a", 8);  // evicts 6
  CHECK(cache.cached_frames() == 3);
  const int before = decodes;
  cache.frame("a", 5);
  CHECK(decodes == before);
  cache.frame("a", 6);
  CHECK(decodes == before + 1);

  // Indexes clamp before caching, so they share entries.
  CHECK(cache.frame("a", 1000).pixel(0, 0).r == 99);
  CHECK(cache.frame("a", 99).pixel(0, 0).r == 99);

  const int with_frames = decodes;
  cache.audio("a", 100);
  cache.audio("a", 100);
  CHECK(decodes == with_frames + 1);

  cache.forget("a");
  cache.frame("a", 5);
  CHECK(opens == 2);
}

TEST_CASE("the cache serves several threads", "[cache]") {
  mpp::MediaCache cache;
  std::vector<std::thread> threads;
  std::atomic<int> wrong{0};
  for (int t = 0; t < 4; ++t) {
    threads.emplace_back([&, t] {
      for (int i = 0; i < 30; ++i) {
        const int n = (i * 7 + t * 3) % 30;
        const mpp::Color c = cache.frame(fixture("counter.mov"), n).pixel(5, 5);
        if (c.r != n * 8 % 256) ++wrong;
      }
    });
  }
  for (auto& t : threads) t.join();
  CHECK(wrong == 0);
}

TEST_CASE("a file that can't be opened throws every time", "[cache]") {
  mpp::MediaCache cache;
  CHECK_THROWS(cache.info(fixture("missing.mov")));
  CHECK_THROWS(cache.frame(fixture("missing.mov"), 0));
}
