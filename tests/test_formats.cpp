#include <filesystem>
#include <memory>

#include <catch2/catch_test_macros.hpp>

#include "golden.h"
#include "mpp/audio_mixer.h"
#include "mpp/bin_commands.h"
#include "mpp/edl.h"
#include "mpp/editor.h"
#include "mpp/export_job.h"
#include "mpp/ffmpeg_source.h"
#include "mpp/filmstrip.h"
#include "mpp/image_io.h"
#include "mpp/image_sequence.h"
#include "mpp/project_file.h"
#include "mpp/renderer.h"
#include "mpp/timeline_commands.h"
#include "project_fixtures.h"

namespace fs = std::filesystem;
using mpp::Color;
using mpp::Image;
using mpp::Track;

namespace {

// Writes frames shot_0007.png ... into a fresh folder: frame N is grey 10N.
fs::path make_sequence_folder(const char* name, int first, int count) {
  const fs::path dir = fs::path(MPP_TEST_OUTPUT_DIR) / name;
  fs::remove_all(dir);
  fs::create_directories(dir);
  for (int i = 0; i < count; ++i) {
    char file[64];
    std::snprintf(file, sizeof(file), "shot_%04d.png", first + i);
    const auto v = static_cast<std::uint8_t>(10 * i);
    mpp::save_png(Image(40, 30, {v, v, v, 255}), (dir / file).string());
  }
  mpp::save_png(Image(4, 4), (dir / "shot_9999.png").string());  // not in the run
  mpp::save_png(Image(4, 4), (dir / "other_0008.png").string());
  return dir;
}

}  // namespace

TEST_CASE("a numbered image is found with its run", "[sequence-files]") {
  const fs::path dir = make_sequence_folder("seq", 7, 5);
  const auto seq = mpp::find_image_sequence((dir / "shot_0009.png").string());
  REQUIRE(seq);
  CHECK(seq->first == 7);
  CHECK(seq->count == 5);
  CHECK(seq->path(0) == (dir / "shot_0007.png").string());
  CHECK(seq->path(4) == (dir / "shot_0011.png").string());
  CHECK_FALSE(mpp::find_image_sequence((dir / "shot_9999.png").string()));
  CHECK_FALSE(mpp::find_image_sequence(fixture("still.png")));
}

TEST_CASE("an image sequence plays one image a frame", "[sequence-files]") {
  const fs::path dir = make_sequence_folder("seq_play", 1, 6);
  const auto seq = mpp::find_image_sequence((dir / "shot_0001.png").string());
  REQUIRE(seq);
  mpp::ProjectSettings s;
  s.width = 40;
  s.height = 30;
  mpp::Editor e;
  e.execute(std::make_unique<mpp::SetSettingsCommand>(s));
  mpp::MediaCache cache;
  const mpp::MediaItem item = mpp::make_sequence_item(*seq, cache.info(seq->path(0)), s);
  CHECK(item.out == 6);
  CHECK(item.info.kind == mpp::MediaKind::Sequence);
  e.execute(std::make_unique<mpp::AddItemCommand>(item));
  e.execute(std::make_unique<mpp::PlaceClipCommand>(e.project().bin[0].id, Track::VideoA, 0, 0, 6));
  for (int f = 0; f < 6; ++f) {
    CHECK(mpp::render_frame(e.project(), cache, f).pixel(5, 5).r == 10 * f);
  }
  // It survives a save and load, and a move of the folder with the project.
  const fs::path project = dir / "seq.mpp";
  mpp::save_project(e.project(), project.string());
  const fs::path moved = fs::path(MPP_TEST_OUTPUT_DIR) / "seq_moved";
  fs::remove_all(moved);
  fs::rename(dir, moved);
  const mpp::Project back = mpp::load_project((moved / "seq.mpp").string());
  REQUIRE(back.bin[0].sequence);
  CHECK(back.bin[0].sequence->count == 6);
  CHECK(fs::exists(back.bin[0].sequence->path(5)));
  CHECK(mpp::render_frame(back, cache, 3).pixel(5, 5).r == 30);
}

TEST_CASE("a filmstrip round-trips its frames and settings", "[filmstrip]") {
  std::vector<Image> frames;
  for (int i = 0; i < 4; ++i) frames.push_back(make_test_pattern(48, 36).flipped_horizontal());
  frames[2].fill({0, 0, 48, 36}, {200, 30, 30, 255});
  const mpp::Rate ntsc{30000, 1001, true};
  const Image strip = mpp::make_filmstrip(frames, ntsc, 1798);
  CHECK(strip.width() == 48);
  CHECK(strip.height() == 4 * (36 + mpp::kFilmstripBand));
  const auto info = mpp::read_filmstrip_info(strip);
  REQUIRE(info);
  CHECK(info->count == 4);
  CHECK(info->height == 36);
  CHECK(info->rate == ntsc);
  CHECK(info->first == 1798);
  for (int i = 0; i < 4; ++i) CHECK(mpp::filmstrip_frame(strip, *info, i) == frames[static_cast<std::size_t>(i)]);
  CHECK(golden_mismatch(strip, "filmstrip") == "");
  // Through a PNG file, as an image editor would save it.
  const std::string path = (fs::path(MPP_TEST_OUTPUT_DIR) / "strip.png").string();
  mpp::save_png(strip, path);
  CHECK(mpp::read_filmstrip_info(mpp::load_image(path)));
  // A plain picture isn't one; nor is a strip with its first band painted.
  CHECK_FALSE(mpp::read_filmstrip_info(make_test_pattern(48, 52)));
  Image painted = strip;
  painted.fill({0, 36, 48, 2}, mpp::kWhite);
  CHECK_FALSE(mpp::read_filmstrip_info(painted));
}

TEST_CASE("a program goes out as a filmstrip and comes back as a clip", "[filmstrip]") {
  mpp::ProjectSettings s;
  s.width = 160;
  s.height = 120;
  s.rate = {15, 1, false};
  mpp::Editor e(project_with({"counter.mov"}, s));
  e.execute(std::make_unique<mpp::PlaceClipCommand>(1, Track::VideoA, 0, 0, 30));
  mpp::MediaCache cache;
  const std::string path = (fs::path(MPP_TEST_OUTPUT_DIR) / "program_strip.png").string();
  mpp::export_filmstrip(e.project(), cache, path, 5, 15);
  const auto info = mpp::read_filmstrip_info(mpp::load_image(path));
  REQUIRE(info);
  CHECK(info->count == 10);
  // Painted on (a frame's middle) and imported back.
  Image strip = mpp::load_image(path);
  strip.fill({60, 3 * (120 + mpp::kFilmstripBand) + 40, 40, 40}, {255, 0, 255, 255});
  mpp::save_png(strip, path);
  e.execute(std::make_unique<mpp::AddItemCommand>(mpp::make_filmstrip_item(path, *info, s)));
  const mpp::MediaItem& item = e.project().bin.back();
  CHECK(item.info.kind == mpp::MediaKind::Filmstrip);
  CHECK(item.out == 10);
  e.execute(std::make_unique<mpp::PlaceClipCommand>(item.id, Track::VideoB, 100, 0, 10));
  cache.forget(path);
  CHECK(mpp::render_frame(e.project(), cache, 102).pixel(5, 5) == mpp::render_frame(e.project(), cache, 7).pixel(5, 5));
  CHECK(mpp::render_frame(e.project(), cache, 103).pixel(80, 60) == Color{255, 0, 255, 255});
}

TEST_CASE("the EDL lists cuts, transitions, sound and speed", "[edl]") {
  mpp::Editor e(project_with({"counter.mov", "still.png"}));
  e.execute(std::make_unique<mpp::PlaceClipCommand>(1, Track::VideoA, 0, 0, 60));
  e.execute(std::make_unique<mpp::PlaceClipCommand>(2, Track::VideoB, 40, 0, 150));
  e.execute(std::make_unique<mpp::AddTransitionCommand>("cross_dissolve", 40, 20));
  e.execute(std::make_unique<mpp::PlaceClipCommand>(2, Track::VideoA, 200, 0, 30));
  e.execute(std::make_unique<mpp::AddTransitionCommand>("iris_round", 200, 10, mpp::TransitionSettings{0, 1, true}));
  e.execute(std::make_unique<mpp::SetSpeedCommand>(e.project().clip_at(Track::VideoA, 200)->id, 50));
  e.execute(std::make_unique<mpp::PlaceClipCommand>(2, Track::Superimpose, 10, 0, 20));
  const std::string edl = mpp::make_edl(e.project(), "Test");
  mpp::save_edl(e.project(), "Test", (fs::path(MPP_TEST_OUTPUT_DIR) / "test.edl").string());
  // The still on B carries on after the dissolve to frame 190; the iris at
  // 200 comes in from black, since B is empty there; out points are
  // exclusive.
  const std::string want =
      "TITLE: Test\n"
      "FCM: NON-DROP FRAME\n"
      "\n"
      "001  AX       V     C        00:00:00:00 00:00:01:10 01:00:00:00 01:00:01:10\n"
      "* FROM CLIP NAME: counter.mov\n"
      "002  AX       V     C        00:00:01:10 00:00:01:10 01:00:01:10 01:00:01:10\n"
      "002  AX       V     D    020 00:00:00:00 00:00:05:00 01:00:01:10 01:00:06:10\n"
      "* FROM CLIP NAME: counter.mov\n"
      "* TO CLIP NAME: still.png\n"
      "003  BL       V     C        00:00:00:00 00:00:00:00 01:00:06:10 01:00:06:20\n"
      "004  BL       V     C        00:00:00:00 00:00:00:00 01:00:06:20 01:00:06:20\n"
      "004  AX       V     W001 010 00:00:00:00 00:00:01:00 01:00:06:20 01:00:08:20\n"
      "M2   AX       015.0                00:00:00:00\n"
      "* EFFECT NAME: Iris Round\n"
      "* FROM CLIP NAME: BLACK\n"
      "* TO CLIP NAME: still.png\n"
      "005  AX       A     C        00:00:00:00 00:00:02:00 01:00:00:00 01:00:02:00\n"
      "* FROM CLIP NAME: counter.mov\n"
      "* SUPER: still.png 01:00:00:10 01:00:01:00\n";
  CHECK(edl == want);

  mpp::Editor drop(project_with({"counter.mov"}, mpp::ProjectSettings{320, 240, {30000, 1001, true}}));
  drop.execute(std::make_unique<mpp::PlaceClipCommand>(1, Track::VideoA, 0, 0, 30));
  const std::string df = mpp::make_edl(drop.project(), "DF");
  mpp::save_edl(drop.project(), "DF", (fs::path(MPP_TEST_OUTPUT_DIR) / "df.edl").string());
  CHECK(df.find("FCM: DROP FRAME") != std::string::npos);
  CHECK(df.find("01:00:00;00 01:00:01;00") != std::string::npos);
}

TEST_CASE("16-bit 44.1 kHz sound goes out and back exactly", "[export][audio]") {
  mpp::ProjectSettings s;
  s.width = 160;
  s.height = 120;
  s.rate = {15, 1, false};
  s.audio_rate = 44100;
  s.audio_bits = 16;
  mpp::Editor e(project_with({"counter.mov"}, s));
  e.execute(std::make_unique<mpp::PlaceClipCommand>(1, Track::VideoA, 0, 0, 30));
  mpp::MediaCache cache;
  const std::string path = (fs::path(MPP_TEST_OUTPUT_DIR) / "cd.mov").string();
  mpp::export_movie(e.project(), cache, path, mpp::MovieFormat::Animation, 0, 30);
  mpp::FfmpegSource movie(path);
  CHECK(movie.info().sample_rate == 44100);
  const mpp::AudioBuffer mix = mpp::mix_audio(e.project(), cache, 0, 30);
  CHECK(mix.frames() == 88200);
  CHECK(movie.audio(44100).samples == mix.samples);
}
