#include <cmath>
#include <filesystem>
#include <memory>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "mpp/audio_mixer.h"
#include "mpp/editor.h"
#include "mpp/export_job.h"
#include "mpp/ffmpeg_source.h"
#include "mpp/renderer.h"
#include "mpp/timeline_commands.h"
#include "project_fixtures.h"

namespace fs = std::filesystem;
using mpp::Track;

namespace {

mpp::Project program(int width, int height, mpp::Rate rate, int bits) {
  mpp::ProjectSettings s;
  s.width = width;
  s.height = height;
  s.rate = rate;
  s.audio_bits = bits;
  mpp::Editor e(project_with({"counter.mov", "still.png"}, s));
  e.execute(std::make_unique<mpp::PlaceClipCommand>(1, Track::VideoA, 0, 0, rate.nominal() * 2));
  e.execute(std::make_unique<mpp::PlaceClipCommand>(2, Track::Superimpose, 5, 0, 10));
  return e.project();
}

std::string out_path(const char* name) { return (fs::path(MPP_TEST_OUTPUT_DIR) / name).string(); }

}  // namespace

TEST_CASE("an Animation movie holds exactly the rendered frames and sound", "[export]") {
  const mpp::Project p = program(160, 120, {15, 1, false}, 16);
  const std::string path = out_path("export.mov");
  mpp::MediaCache cache;
  mpp::export_movie(p, cache, path, mpp::MovieFormat::Animation, 2, 22);

  mpp::FfmpegSource movie(path);
  CHECK(movie.info().frames == 20);
  CHECK(movie.info().rate == mpp::Rate{15, 1, false});
  CHECK(movie.info().width == 160);
  CHECK(movie.info().has_audio);
  for (int f = 0; f < 20; ++f) {
    REQUIRE(movie.frame(f) == mpp::render_frame(p, cache, f + 2));
  }
  const mpp::AudioBuffer mix = mpp::mix_audio(p, cache, 2, 22);
  const mpp::AudioBuffer back = movie.audio(22050);
  REQUIRE(back.frames() == mix.frames());
  CHECK(back.samples == mix.samples);
}

TEST_CASE("8-bit sound stays 8-bit in an Animation movie", "[export]") {
  const mpp::Project p = program(160, 120, {15, 1, false}, 8);
  const std::string path = out_path("export8.mov");
  mpp::MediaCache cache;
  mpp::export_movie(p, cache, path, mpp::MovieFormat::Animation, 0, 15);
  const mpp::AudioBuffer back = mpp::FfmpegSource(path).audio(22050);
  CHECK(back.samples == mpp::mix_audio(p, cache, 0, 15).samples);
}

TEST_CASE("an H.264 movie plays back close to the render", "[export]") {
  const mpp::Project p = program(320, 240, {30, 1, false}, 16);
  const std::string path = out_path("export.mp4");
  mpp::MediaCache cache;
  mpp::export_movie(p, cache, path, mpp::MovieFormat::H264, 0, 45);
  mpp::FfmpegSource movie(path);
  CHECK(movie.info().frames == 45);
  CHECK(movie.info().has_audio);
  for (int f : {0, 7, 30, 44}) {
    const mpp::Color want = mpp::render_frame(p, cache, f).pixel(40, 120);
    const mpp::Color got = movie.frame(f).pixel(40, 120);
    INFO("frame " << f);
    CHECK(std::abs(want.r - got.r) <= 8);
    CHECK(std::abs(want.g - got.g) <= 8);
    CHECK(std::abs(want.b - got.b) <= 8);
  }
  CHECK(std::abs(static_cast<double>(movie.audio(22050).frames()) - 33075) < 2048);
}

TEST_CASE("exports refuse bad sizes and clean up when stopped", "[export]") {
  mpp::MediaCache cache;
  const mpp::Project odd = program(161, 120, {15, 1, false}, 16);
  CHECK_THROWS_WITH(mpp::export_movie(odd, cache, out_path("odd.mp4"), mpp::MovieFormat::H264, 0, 5),
                    Catch::Matchers::ContainsSubstring("even"));
  const mpp::Project p = program(160, 120, {15, 1, false}, 16);
  const std::string path = out_path("stopped.mov");
  int calls = 0;
  CHECK_THROWS_WITH(mpp::export_movie(p, cache, path, mpp::MovieFormat::Animation, 0, 30,
                                      [&](double) { return ++calls < 5; }),
                    "cancelled");
  CHECK_FALSE(fs::exists(path));
  CHECK_THROWS(mpp::export_movie(p, cache, path, mpp::MovieFormat::Animation, 5, 5));
}

TEST_CASE("an export job runs in the background", "[export][job]") {
  const std::string path = out_path("job.mov");
  mpp::ExportJob job(program(160, 120, {15, 1, false}, 16), path, mpp::MovieFormat::Animation, 0, 10);
  CHECK(job.wait() == "");
  CHECK(job.finished());
  CHECK(job.progress() == 1.0);
  CHECK(mpp::FfmpegSource(path).info().frames == 10);
}
