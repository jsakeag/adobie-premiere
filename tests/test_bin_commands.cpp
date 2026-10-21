#include <memory>

#include <catch2/catch_test_macros.hpp>

#include "golden.h"
#include "mpp/bin_commands.h"
#include "mpp/editor.h"
#include "mpp/media_cache.h"

using mpp::Editor;
using mpp::MediaItem;

namespace {

MediaItem counter_item(const mpp::ProjectSettings& settings) {
  mpp::MediaCache cache;
  const std::string path = fixture("counter.mov");
  return mpp::make_item(path, cache.info(path), settings);
}

}  // namespace

TEST_CASE("a new item's marks cover its whole length", "[bin]") {
  mpp::ProjectSettings settings;  // 30 fps
  const MediaItem movie = counter_item(settings);
  CHECK(movie.name == "counter.mov");
  CHECK(movie.in == 0);
  CHECK(movie.out == 60);  // 2 seconds at 30 fps

  mpp::MediaCache cache;
  const std::string still = fixture("still.png");
  const MediaItem picture = mpp::make_item(still, cache.info(still), settings);
  CHECK(picture.out == settings.still_frames);
  CHECK_FALSE(mpp::source_length(picture.info, settings.rate));
}

TEST_CASE("project frames map to source frames by time", "[bin]") {
  mpp::MediaInfo info;
  info.rate = {15, 1, false};
  info.frames = 30;
  const mpp::Rate project{30, 1, false};
  CHECK(mpp::source_frame(info, project, 0) == 0);
  CHECK(mpp::source_frame(info, project, 1) == 0);
  CHECK(mpp::source_frame(info, project, 2) == 1);
  CHECK(mpp::source_frame(info, project, 59) == 29);
  CHECK(mpp::source_frame(info, project, 500) == 29);
}

TEST_CASE("bin edits are commands with single-level undo", "[bin][undo]") {
  Editor editor;
  const MediaItem item = counter_item(editor.project().settings);
  editor.execute(std::make_unique<mpp::AddItemCommand>(item));
  editor.execute(std::make_unique<mpp::AddItemCommand>(item));
  REQUIRE(editor.project().bin.size() == 2);
  const int first = editor.project().bin[0].id;
  const int second = editor.project().bin[1].id;
  CHECK(first != second);

  editor.execute(std::make_unique<mpp::SetItemMarksCommand>(second, 10, 20));
  CHECK(editor.project().item(second)->in == 10);
  CHECK(editor.undo_name() == "Set Marks");
  CHECK(editor.toggle_undo());
  CHECK(editor.project().item(second)->in == 0);
  CHECK(editor.toggle_undo());
  CHECK(editor.project().item(second)->out == 20);

  editor.execute(std::make_unique<mpp::RemoveItemCommand>(first));
  CHECK(editor.project().bin.size() == 1);
  editor.undo();
  CHECK(editor.project().bin.size() == 2);
  CHECK(editor.project().bin[0].id == first);
  CHECK_FALSE(editor.undo());  // only one level

  // A failing command changes nothing, including the undo step.
  CHECK_THROWS(editor.execute(std::make_unique<mpp::SetItemMarksCommand>(second, 30, 10)));
  CHECK_THROWS(editor.execute(std::make_unique<mpp::SetItemMarksCommand>(second, 0, 61)));
  CHECK_THROWS(editor.execute(std::make_unique<mpp::RemoveItemCommand>(999)));
  CHECK(editor.project().bin.size() == 2);
  CHECK(editor.can_redo());
}

TEST_CASE("changing the frame rate rescales marks", "[bin]") {
  Editor editor;
  editor.execute(std::make_unique<mpp::AddItemCommand>(counter_item(editor.project().settings)));
  const int id = editor.project().bin[0].id;
  editor.execute(std::make_unique<mpp::SetItemMarksCommand>(id, 10, 40));
  mpp::ProjectSettings settings = editor.project().settings;
  settings.rate = {15, 1, false};
  editor.execute(std::make_unique<mpp::SetSettingsCommand>(settings));
  CHECK(editor.project().item(id)->in == 5);
  CHECK(editor.project().item(id)->out == 20);

  settings.audio_bits = 12;
  CHECK_THROWS(editor.execute(std::make_unique<mpp::SetSettingsCommand>(settings)));
}
