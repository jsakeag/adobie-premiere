#include <memory>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "mpp/audio_mixer.h"
#include "mpp/editor.h"
#include "mpp/render_job.h"
#include "mpp/renderer.h"
#include "mpp/timeline_commands.h"
#include "project_fixtures.h"

using mpp::Track;

namespace {

mpp::Project small_program() {
  mpp::ProjectSettings s;
  s.width = 160;
  s.height = 120;
  s.rate = {15, 1, false};
  mpp::Editor e(project_with({"counter.mov", "still.png"}, s));
  e.execute(std::make_unique<mpp::PlaceClipCommand>(1, Track::VideoA, 0, 0, 30));
  e.execute(std::make_unique<mpp::PlaceClipCommand>(2, Track::VideoB, 20, 0, 30));
  e.execute(std::make_unique<mpp::AddTransitionCommand>("iris_round", 20, 10));
  return e.project();
}

}  // namespace

TEST_CASE("a render job gives the same frames and sound as rendering one by one", "[render][job]") {
  const mpp::Project p = small_program();
  mpp::RenderJob job(p, 5, 45, 3);
  const mpp::RenderedProgram out = job.take();
  CHECK(job.finished());
  CHECK(job.progress() == 1.0);
  REQUIRE(out.frames.size() == 40);
  CHECK(out.first == 5);
  mpp::MediaCache cache;
  for (std::int64_t f = 5; f < 45; ++f) {
    REQUIRE(out.frames[static_cast<std::size_t>(f - 5)] == mpp::render_frame(p, cache, f));
  }
  CHECK(out.audio.samples == mpp::mix_audio(p, cache, 5, 45).samples);
  CHECK_THROWS(job.take());
}

TEST_CASE("a cancelled render job stops and says so", "[render][job]") {
  mpp::ProjectSettings s;
  s.width = 640;
  s.height = 480;
  mpp::Editor e(project_with({"still.png"}, s));
  e.execute(std::make_unique<mpp::PlaceClipCommand>(1, Track::VideoA, 0, 0, 3000));
  mpp::FilterInstance blur = mpp::default_instance("gaussian_blur");
  blur.start = blur.end = {12};
  e.execute(std::make_unique<mpp::SetFiltersCommand>(e.project().clips[0].id,
                                                     std::vector<mpp::FilterInstance>{blur}));
  mpp::RenderJob job(e.project(), 0, 3000, 2);
  job.cancel();
  CHECK_THROWS_WITH(job.take(), "Rendering was cancelled");
  CHECK(job.finished());
  CHECK(job.progress() < 1.0);
}
