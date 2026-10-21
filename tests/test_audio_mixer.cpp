#include <cmath>
#include <memory>

#include <catch2/catch_test_macros.hpp>

#include "mpp/audio_mixer.h"
#include "mpp/editor.h"
#include "mpp/timeline_commands.h"
#include "project_fixtures.h"

using mpp::Track;

namespace {

// tone.wav (1): one second of 660 Hz at 22.05 kHz. counter.mov (2): two
// seconds with a 440 Hz tone. 30 fps, 16-bit 22.05 kHz.
mpp::Editor tone_editor(int bits = 16) {
  mpp::ProjectSettings s;
  s.audio_rate = 22050;
  s.audio_bits = bits;
  return mpp::Editor(project_with({"tone.wav", "counter.mov"}, s));
}

}  // namespace

TEST_CASE("frames map to the first sample they cover", "[audio]") {
  mpp::ProjectSettings s;
  s.audio_rate = 22050;
  CHECK(mpp::frame_to_sample(s, 1) == 735);
  CHECK(mpp::frame_to_sample(s, 30) == 22050);
  s.rate = {30000, 1001, false};
  s.audio_rate = 48000;
  CHECK(mpp::frame_to_sample(s, 1) == 1602);  // 1601.6
  CHECK(mpp::frame_to_sample(s, 30000) == 48048000);
}

TEST_CASE("a clip's sound is its stretch of the source", "[audio]") {
  mpp::Editor e = tone_editor();
  e.execute(std::make_unique<mpp::PlaceClipCommand>(1, Track::AudioA, 0, 15, 30));
  mpp::MediaCache cache;
  const auto source = cache.audio(fixture("tone.wav"), 22050);
  const mpp::Clip& clip = e.project().clips[0];
  const mpp::AudioBuffer sound = mpp::clip_audio(e.project(), cache, clip);
  REQUIRE(sound.frames() == 11025);
  CHECK(sound.samples[0] == source->samples[11025 * 2]);
  CHECK(sound.samples[5000 * 2 + 1] == source->samples[(11025 + 5000) * 2 + 1]);

  // A rubber band from silent at the in point to full at the out point.
  e.execute(std::make_unique<mpp::SetFadeCommand>(
      clip.id, std::vector<mpp::FadePoint>{{15, 0.0}, {30, 1.0}}));
  const mpp::AudioBuffer faded = mpp::clip_audio(e.project(), cache, e.project().clips[0]);
  CHECK(faded.samples[0] == 0.0f);
  CHECK(std::abs(faded.samples[6000 * 2] - source->samples[(11025 + 6000) * 2] * 6000.0f / 11025) < 1e-6f);

  // Backwards plays the clip from its end.
  e.execute(std::make_unique<mpp::SetFadeCommand>(e.project().clips[0].id, std::vector<mpp::FadePoint>{}));
  e.execute(std::make_unique<mpp::SetFiltersCommand>(
      e.project().clips[0].id, std::vector<mpp::FilterInstance>{mpp::default_instance("backwards")}));
  const mpp::AudioBuffer back = mpp::clip_audio(e.project(), cache, e.project().clips[0]);
  CHECK(back.samples[0] == source->samples[(22050 - 1) * 2]);
}

TEST_CASE("the mixer sums the audio tracks and quantizes", "[audio]") {
  mpp::Editor e = tone_editor();
  e.execute(std::make_unique<mpp::PlaceClipCommand>(1, Track::AudioA, 0, 0, 30));
  e.execute(std::make_unique<mpp::PlaceClipCommand>(1, Track::AudioC, 10, 0, 30));
  mpp::MediaCache cache;
  const auto tone = cache.audio(fixture("tone.wav"), 22050);
  const mpp::AudioBuffer mix = mpp::mix_audio(e.project(), cache, 0, 40);
  REQUIRE(mix.frames() == 29400);
  const auto q16 = [](float v) { return std::round(v * 32768.0f) / 32768.0f; };
  CHECK(mix.samples[100 * 2] == q16(tone->samples[100 * 2]));
  // From frame 10 (sample 7350) both play, the second from its start.
  CHECK(std::abs(mix.samples[8000 * 2] - q16(tone->samples[8000 * 2] + tone->samples[650 * 2])) < 1e-6f);
  // After the first ends at 30, only the second.
  CHECK(mix.samples[25000 * 2] == q16(tone->samples[(25000 - 7350) * 2]));
  // A later range lines up with the whole mix.
  const mpp::AudioBuffer part = mpp::mix_audio(e.project(), cache, 15, 20);
  REQUIRE(part.frames() == 3675);
  CHECK(part.samples[0] == mix.samples[11025 * 2]);
  // Video clips' sound arrives through their linked audio clips.
  e.execute(std::make_unique<mpp::PlaceClipCommand>(2, Track::VideoA, 100, 0, 30));
  CHECK(mpp::mix_audio(e.project(), cache, 100, 101).samples[200] != 0.0f);
}

TEST_CASE("8-bit sound has 256 levels and clips at full scale", "[audio]") {
  mpp::AudioBuffer a;
  a.samples = {0.3f, -0.3f, 1.5f, -2.0f, 0.004f, 0.999f};
  mpp::quantize(a, 8);
  CHECK(a.samples == std::vector<float>{38 / 128.0f, -38 / 128.0f, 127 / 128.0f, -1.0f, 1 / 128.0f, 127 / 128.0f});
  mpp::Editor e = tone_editor(8);
  e.execute(std::make_unique<mpp::PlaceClipCommand>(1, Track::AudioA, 0, 0, 30));
  mpp::MediaCache cache;
  for (float v : mpp::mix_audio(e.project(), cache, 0, 30).samples) {
    REQUIRE(v * 128.0f == std::round(v * 128.0f));
  }
}
