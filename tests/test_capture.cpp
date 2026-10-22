#include <chrono>
#include <filesystem>
#include <functional>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "mpp/audio_mixer.h"
#include "mpp/capture.h"
#include "mpp/ffmpeg_source.h"

namespace fs = std::filesystem;

namespace {

std::string out_path(const char* name) { return (fs::path(MPP_TEST_OUTPUT_DIR) / name).string(); }

mpp::CaptureInput lavfi(const std::string& graph) { return {"lavfi", graph, {}}; }

// Waits up to ten seconds for `done`.
bool eventually(const std::function<bool()>& done) {
  const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (!done()) {
    if (std::chrono::steady_clock::now() > until) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return true;
}

mpp::ProjectSettings small_project() {
  mpp::ProjectSettings s;
  s.width = 16;
  s.height = 16;
  s.rate = {15, 1, false};
  s.audio_rate = 22050;
  s.audio_bits = 16;
  return s;
}

// A 30 fps picture whose red counts up by 8 a frame, and mono sound at a
// steady 0.25: what came from where can be read back exactly.
const char* kCounting =
    "nullsrc=size=16x16:rate=30,format=gbrp,geq=r='mod(N*8\\,256)':g=0:b=0,format=rgba[out0];"
    "aevalsrc=0.25:s=22050[out1]";

}  // namespace

TEST_CASE("the device list is read from AVFoundation's log", "[capture]") {
  const std::string log =
      "[AVFoundation indev @ 0x76d410140] AVFoundation video devices:\n"
      "[AVFoundation indev @ 0x76d410140] [0] FaceTime HD Camera\n"
      "[AVFoundation indev @ 0x76d410140] [3] Capture screen 0\n"
      "[AVFoundation indev @ 0x76d410140] AVFoundation audio devices:\n"
      "[AVFoundation indev @ 0x76d410140] [0] MacBook Pro Microphone\n"
      "[in#0 @ 0x76d410000] Error opening input: Input/output error\n";
  const mpp::CaptureDevices d = mpp::parse_device_list(log);
  REQUIRE(d.cameras.size() == 2);
  CHECK(d.cameras[0].index == 0);
  CHECK(d.cameras[0].name == "FaceTime HD Camera");
  CHECK(d.cameras[1].index == 3);
  CHECK(d.cameras[1].name == "Capture screen 0");
  REQUIRE(d.microphones.size() == 1);
  CHECK(d.microphones[0].name == "MacBook Pro Microphone");

  // As the callback hands it over, without prefixes.
  const mpp::CaptureDevices bare = mpp::parse_device_list(
      "AVFoundation video devices:\n[1] Desk Camera\nAVFoundation audio devices:\n");
  REQUIRE(bare.cameras.size() == 1);
  CHECK(bare.cameras[0].index == 1);
  CHECK(bare.microphones.empty());
  CHECK(mpp::parse_device_list("").cameras.empty());
}

TEST_CASE("inputs name the devices or the test pattern", "[capture]") {
  const mpp::CaptureInput both = mpp::device_input(0, 2);
  CHECK(both.format == "avfoundation");
  CHECK(both.url == "0:2");
  REQUIRE(both.options.size() == 1);
  CHECK(both.options[0].first == "framerate");
  CHECK(both.options[0].second == "30");
  CHECK(mpp::device_input(1, -1).url == "1:none");
  CHECK(mpp::device_input(-1, 0).url == "none:0");
  CHECK(mpp::device_input(-1, 0).options.empty());

  mpp::ProjectSettings s;
  s.rate = {30000, 1001, true};
  const mpp::CaptureInput pattern = mpp::test_pattern_input(s, true);
  CHECK(pattern.format == "lavfi");
  CHECK(pattern.url ==
        "testsrc2=size=320x240:rate=30000/1001,format=rgba,realtime[out0];"
        "sine=frequency=440:sample_rate=22050,arealtime[out1]");
  CHECK(mpp::test_pattern_input(s, false).url.find("sine") == std::string::npos);
}

TEST_CASE("a recording takes the newest picture for each frame, with sound in step", "[capture]") {
  const mpp::ProjectSettings s = small_project();
  mpp::CaptureSession session(lavfi(kCounting), s);
  REQUIRE(session.wait_open().empty());
  CHECK(session.has_sound());

  const std::string path = out_path("capture.mov");
  REQUIRE(session.record(path, mpp::MovieFormat::Animation, 10).empty());
  CHECK(session.record(path, mpp::MovieFormat::Animation, 10) == "Already recording");
  REQUIRE(eventually([&] { return !session.recording(); }));
  CHECK(session.recorded_frames() == 10);
  CHECK(session.stop().empty());

  mpp::FfmpegSource movie(path);
  REQUIRE(movie.info().frames == 10);
  CHECK(movie.info().rate == mpp::Rate{15, 1, false});
  CHECK(movie.info().width == 16);
  // 30 fps into 15: every other picture, so red goes up by 16 a frame.
  const int first = movie.frame(0).pixel(5, 5).r;
  for (int f = 0; f < 10; ++f) {
    const mpp::Image frame = movie.frame(f);
    INFO("frame " << f);
    CHECK(frame.pixel(5, 5) == mpp::Color{static_cast<std::uint8_t>((first + 16 * f) % 256), 0, 0, 255});
  }

  // The sound lasts exactly as long as the pictures, and mono comes out on
  // both sides at full level, with nothing missing at either end.
  const mpp::AudioBuffer sound = movie.audio(22050);
  REQUIRE(static_cast<std::int64_t>(sound.frames()) == mpp::frame_to_sample(s, 10));
  std::size_t wrong = 0;
  for (float v : sound.samples) wrong += v != 0.25f;
  CHECK(wrong == 0);
}

TEST_CASE("stop() ends a recording and keeps what came in", "[capture]") {
  const mpp::ProjectSettings s = small_project();
  mpp::CaptureSession session(lavfi(kCounting), s);
  REQUIRE(session.wait_open().empty());
  const std::string path = out_path("capture_stopped.mov");
  REQUIRE(session.record(path, mpp::MovieFormat::Animation).empty());
  REQUIRE(eventually([&] { return session.recorded_frames() >= 3; }));
  CHECK(session.stop().empty());
  CHECK(!session.recording());
  const std::int64_t frames = session.recorded_frames();
  CHECK(frames >= 4);  // the newest picture goes in at the end

  mpp::FfmpegSource movie(path);
  CHECK(movie.info().frames == frames);
  CHECK(static_cast<std::int64_t>(movie.audio(22050).frames()) == mpp::frame_to_sample(s, frames));
  CHECK(session.stop().empty());  // nothing more to stop
}

TEST_CASE("pictures of another shape are fitted into the frame", "[capture]") {
  mpp::CaptureSession session(lavfi("color=c=red:size=32x16:rate=10,format=rgba[out0]"), small_project());
  REQUIRE(session.wait_open().empty());
  CHECK(!session.has_sound());
  REQUIRE(eventually([&] { return session.pictures() > 0; }));
  const mpp::Image picture = session.picture();
  REQUIRE(picture.width() == 16);
  REQUIRE(picture.height() == 16);
  CHECK(picture.pixel(8, 0) == mpp::kBlack);
  CHECK(picture.pixel(8, 8) == mpp::Color{255, 0, 0, 255});
  CHECK(picture.pixel(8, 15) == mpp::kBlack);

  // Without a microphone the movie has no sound track.
  const std::string path = out_path("capture_silent.mov");
  REQUIRE(session.record(path, mpp::MovieFormat::Animation, 2).empty());
  REQUIRE(eventually([&] { return !session.recording(); }));
  CHECK(session.stop().empty());
  mpp::FfmpegSource movie(path);
  CHECK(movie.info().frames == 2);
  CHECK(!movie.info().has_audio);
}

TEST_CASE("capture failures are reported, not thrown", "[capture]") {
  mpp::CaptureSession broken(lavfi("nosuchfilter[out0]"), small_project());
  CHECK(!broken.wait_open().empty());
  CHECK(!broken.open());
  CHECK(broken.record(out_path("never.mov"), mpp::MovieFormat::Animation) == "The camera isn't ready");
  CHECK(!fs::exists(out_path("never.mov")));

  mpp::ProjectSettings odd = small_project();
  odd.width = 15;
  mpp::CaptureSession session(lavfi(kCounting), odd);
  REQUIRE(session.wait_open().empty());
  CHECK(!session.record(out_path("odd.mp4"), mpp::MovieFormat::H264).empty());
  CHECK(!session.recording());
}
