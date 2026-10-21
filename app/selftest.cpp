#include "selftest.h"

#include <cstdio>
#include <exception>
#include <string>

#include <nanogui/nanogui.h>
#include <nanogui/opengl.h>

#include "app.h"
#include "clip_window.h"
#include "frame_view.h"
#include "menu_bar.h"
#include "project_window.h"
#include "settings_window.h"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}

}  // namespace

int run_selftest(const std::string& out_dir, const std::string& fixtures) {
  nanogui::init();
  {
    nanogui::ref<App> app = new App();
    app->set_visible(true);
    for (int i = 0; i < 5; ++i) glfwPollEvents();
    const auto project = [&]() -> const mpp::Project& { return app->project(); };
    const auto shot = [&](const char* name) { app->capture(out_dir + "/" + name + ".png"); };
    const auto media = [&](const char* name) { return fixtures + "/" + name; };

    // Milestone 1: the Project window and the Clip window.
    shot("01_empty");
    check(app->import(media("counter.mov")), "import a movie");
    check(app->import(media("still.png")), "import a still");
    check(app->import(media("h264.mp4")), "import an H.264 movie");
    check(app->import(media("tone.wav")), "import a sound file");
    check(!app->import(media("missing.mov")) && !app->message().empty(),
          "a missing file shows an error instead of crashing");
    check(project().bin.size() == 4, "four items in the bin");
    const int counter = project().bin[0].id;
    const int still = project().bin[1].id;
    shot("02_bin");

    app->project_window()->perform_layout(app->nvg_context());
    app->simulate_click(app->project_window()->row_center(still));
    check(app->selected_item() == still, "clicking a row selects it");
    app->simulate_click(app->project_window()->row_center(counter));
    app->simulate_click(app->project_window()->row_center(counter));
    check(app->clip_item() == counter, "double-clicking a row opens it in the Clip window");
    shot("03_clip_open");

    app->clip_seek(20);  // project frame 20 at 30 fps = source frame 10
    shot("04_clip_frame_20");
    const mpp::Image shown = app->clip_window()->view()->frame();
    check(!shown.empty() && shown.pixel(5, 5).r == 80, "the Clip window shows source frame 10");
    app->clip_mark_in();
    app->clip_seek(39);
    app->clip_mark_out();
    const mpp::MediaItem* item = project().item(counter);
    check(item && item->in == 20 && item->out == 40, "marks in at 20 and out after 39");
    shot("05_marked");

    app->clip_go_to_in();
    app->clip_toggle_play();
    check(app->clip_playing(), "play starts");
    app->advance_playback(0.25);
    check(app->clip_position() == 27, "a quarter second later it is 7 frames on");
    app->advance_playback(5.0);
    check(!app->clip_playing() && app->clip_position() == 39, "play stops at the out mark");

    // Dragging the scrub bar seeks through real mouse events.
    app->simulate_drag(app->clip_window()->scrub_point(10), app->clip_window()->scrub_point(50));
    check(app->clip_position() >= 48 && app->clip_position() <= 52, "dragging the scrub bar seeks");

    app->toggle_undo();
    check(project().item(counter)->out == 60 && project().item(counter)->in == 20,
          "Cmd+Z undoes the out mark");
    app->toggle_undo();
    check(project().item(counter)->out == 40, "Cmd+Z again redoes it");

    // The menu bar.
    app->menu_bar()->open(0);
    shot("06_file_menu");
    shot("06_file_menu");  // a new popup draws on its second frame
    app->menu_bar()->open(1);
    shot("07_edit_menu");
    check(app->menu_bar()->choose("Redo Set Marks") == false &&
              app->menu_bar()->choose("Undo Set Marks"),
          "Edit > Undo names the command");
    check(project().item(counter)->out == 60, "the menu undo worked");
    check(app->menu_bar()->open_index() == -1, "choosing an item closes the menu");

    // Project settings.
    app->open_settings();
    check(app->settings_window() != nullptr, "Project > Settings opens");
    shot("08_settings");
    app->settings_window()->choose(0, 3, 1);  // 160 x 120, 15 fps, 8-bit 22 kHz
    app->settings_window()->ok();
    check(project().settings.width == 160 && project().settings.rate == mpp::Rate{15, 1, false},
          "settings applied: 160 x 120 at 15 fps");
    check(project().item(counter)->in == 10, "marks rescaled to the new rate");
    app->open_clip(counter);
    app->clip_seek(3);
    shot("09_clip_15fps");
    check(app->clip_window()->view()->frame().pixel(5, 5).r == 24, "15 fps frames map one to one");

    // Clearing.
    app->select_item(still);
    app->clear_selected();
    check(project().bin.size() == 3 && !project().item(still), "Clear removes the item");
    app->toggle_undo();
    check(project().bin.size() == 4, "and undo brings it back");
    shot("10_final");
  }
  nanogui::shutdown();
  std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
  return failures ? 1 : 0;
}
