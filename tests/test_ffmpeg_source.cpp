#include <cmath>
#include <cstdlib>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "golden.h"
#include "mpp/ffmpeg_source.h"
#include "mpp/media.h"

using mpp::Color;
using mpp::FfmpegSource;

namespace {

// counter.mov: frame N is (N*8, 255-N*8, 64).
Color counter_color(int n) {
  return {static_cast<std::uint8_t>(n * 8 % 256), static_cast<std::uint8_t>(255 - n * 8 % 256),
          64, 255};
}

}  // namespace

TEST_CASE("a movie's info comes from its streams", "[media]") {
  FfmpegSource source(fixture("counter.mov"));
  const mpp::MediaInfo& info = source.info();
  CHECK(info.kind == mpp::MediaKind::Movie);
  CHECK(info.width == 160);
  CHECK(info.height == 120);
  CHECK(info.rate == mpp::Rate{15, 1, false});
  CHECK(info.frames == 30);
  CHECK(info.has_video);
  CHECK(info.has_audio);
  CHECK(info.sample_rate == 22050);
  CHECK(info.channels == 1);
  CHECK(info.duration_seconds() == 2.0);
}

TEST_CASE("lossless frames decode exactly, in order and by seeking", "[media]") {
  FfmpegSource source(fixture("counter.mov"));
  for (int n = 0; n < 30; ++n) {
    REQUIRE(source.frame(n).pixel(10, 10) == counter_color(n));
  }
  for (int n : {17, 3, 29, 0, 28, 5, 5, 6}) {
    const mpp::Image frame = source.frame(n);
    CHECK(frame.width() == 160);
    CHECK(frame.pixel(80, 60) == counter_color(n));
  }
  // Past either end clamps.
  CHECK(source.frame(500).pixel(0, 0) == counter_color(29));
  CHECK(source.frame(-4).pixel(0, 0) == counter_color(0));
}

TEST_CASE("an alpha channel survives decoding", "[media]") {
  FfmpegSource source(fixture("alpha.mov"));
  const mpp::Image frame = source.frame(7);
  CHECK(frame.pixel(10, 60) == Color{255, 0, 0, 255});
  CHECK(frame.pixel(150, 60).a == 0);
}

TEST_CASE("H.264 with B-frames seeks to the right frame", "[media]") {
  FfmpegSource source(fixture("h264.mp4"));
  CHECK(source.info().frames == 90);
  CHECK(source.info().rate == mpp::Rate{30, 1, false});
  CHECK_FALSE(source.info().has_audio);
  for (int n : {0, 1, 2, 44, 45, 31, 89, 60, 61, 62, 10}) {
    const int grey = source.frame(n).pixel(160, 120).r;
    INFO("frame " << n << " grey " << grey);
    CHECK(std::abs(grey - 2 * n) <= 4);
  }
}

TEST_CASE("sound decodes to stereo at the asked-for rate", "[media]") {
  FfmpegSource source(fixture("counter.mov"));
  const mpp::AudioBuffer same = source.audio(22050);
  CHECK(same.sample_rate == 22050);
  CHECK(same.frames() == 44100);
  // Mono becomes two equal channels; the tone has peaks near full scale.
  float peak = 0;
  for (std::size_t i = 0; i < same.frames(); ++i) {
    REQUIRE(same.samples[i * 2] == same.samples[i * 2 + 1]);
    peak = std::max(peak, std::abs(same.samples[i * 2]));
  }
  CHECK(peak > 0.1f);
  CHECK(peak <= 1.0f);

  const mpp::AudioBuffer doubled = source.audio(44100);
  CHECK(std::llabs(static_cast<long long>(doubled.frames()) - 88200) <= 64);
}

TEST_CASE("a sound file has its sample rate as its rate", "[media]") {
  FfmpegSource source(fixture("tone.wav"));
  const mpp::MediaInfo& info = source.info();
  CHECK_FALSE(info.has_video);
  CHECK(info.has_audio);
  CHECK(info.rate == mpp::Rate{22050, 1, false});
  CHECK(info.frames == 22050);
  CHECK(info.duration_seconds() == 1.0);
  CHECK(source.frame(0).pixel(0, 0) == mpp::kBlack);
}

TEST_CASE("stills and bad files", "[media]") {
  auto still = mpp::open_media(fixture("still.png"));
  CHECK(still->info().kind == mpp::MediaKind::Still);
  CHECK(still->info().is_still());
  CHECK(still->info().width == 200);
  CHECK(still->frame(1000).pixel(10, 10) == Color{0, 0, 255, 255});
  CHECK(still->audio(22050).samples.empty());

  CHECK_THROWS(mpp::open_media(fixture("missing.mov")));
  CHECK_THROWS(mpp::open_media(fixture("make_fixtures.sh")));
}
