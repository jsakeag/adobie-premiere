#include "selftest.h"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>

#include <nanogui/nanogui.h>
#include <nanogui/opengl.h>

#include "app.h"
#include "clip_window.h"
#include "frame_view.h"
#include "menu_bar.h"
#include "project_window.h"
#include "mpp/project_file.h"
#include "settings_window.h"
#include "timeline_windows.h"
#include "transition_windows.h"

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

    // Milestone 2: the Construction and Preview windows.
    app->new_project();
    check(project().bin.empty() && project().settings.width == 320, "new project");
    app->import(media("counter.mov"));
    app->import(media("still.png"));
    app->import(media("h264.mp4"));
    app->import(media("tone.wav"));
    const int movie = project().bin[0].id;
    const int picture = project().bin[1].id;
    const int grey = project().bin[2].id;
    ConstructionView* view = app->construction_window()->view();
    app->perform_layout();
    app->draw_all();  // lay out before reading positions

    // Drag the movie from the Project window to Video A at 0.
    app->project_window()->perform_layout(app->nvg_context());
    app->simulate_drag(app->project_window()->row_center(movie),
                       view->point_at(mpp::Track::VideoA, 0));
    check(project().clips.size() == 2, "dragging a movie with sound onto Video A places two clips");
    const mpp::Clip* placed = project().clip_at(mpp::Track::VideoA, 0);
    const mpp::Clip* sound = project().clip_at(mpp::Track::AudioA, 0);
    check(placed && sound && placed->link == sound->id && placed->length() == 60,
          "picture on Video A linked to sound on Audio A");
    app->draw_all();
    check(app->preview_window()->view()->frame().pixel(100, 100) == mpp::Color{0, 255, 64, 255},
          "Preview shows frame 0 of the movie");
    shot("11_placed");

    // Show the ghost of a drag in progress.
    app->drag_item(grey, view->point_at(mpp::Track::VideoB, 200), false);
    shot("12_drop_ghost");
    app->drag_item(grey, view->point_at(mpp::Track::VideoB, 200), true);
    check(project().clip_at(mpp::Track::VideoB, 250) != nullptr, "the H.264 movie lands on Video B at 200");
    check(project().clip_at(mpp::Track::AudioA, 250) == nullptr, "a movie without sound places no audio");

    app->set_edit_line(60);
    app->open_clip(picture);
    check(app->place_at_edit_line(), "Place at Edit Line");
    const mpp::Clip* still_clip = project().clip_at(mpp::Track::VideoA, 60);
    check(still_clip && still_clip->length() == 150 && app->edit_line() == 210,
          "the still goes on Video A after the movie, the edit line after it");

    // The ruler moves the edit line; Preview follows.
    app->simulate_click(view->ruler_point(250));
    check(app->edit_line() == 250, "clicking the ruler moves the edit line");
    app->draw_all();
    const int level = app->preview_window()->view()->frame().pixel(160, 120).r;
    check(level >= 96 && level <= 104, "Preview shows Video B where A is empty");

    // Move the still by dragging its body.
    app->simulate_drag(view->point_at(mpp::Track::VideoA, 120), view->point_at(mpp::Track::VideoA, 150));
    still_clip = project().clip(still_clip->id);
    check(still_clip && still_clip->start == 90, "dragging a clip moves it");
    shot("13_moved");

    // Trim the movie's tail; its sound follows.
    app->simulate_drag(view->point_at(mpp::Track::VideoA, 59), view->point_at(mpp::Track::VideoA, 50));
    placed = project().clip_at(mpp::Track::VideoA, 0);
    sound = placed ? project().clip(placed->link) : nullptr;
    check(placed && placed->out == 50 && sound && sound->out == 50, "dragging the end trims picture and sound");

    // Cut it with the razor.
    app->set_tool(TimelineTool::Razor);
    app->simulate_click(view->point_at(mpp::Track::VideoA, 25));
    check(project().clips_on(mpp::Track::VideoA).size() == 3 &&
              project().clips_on(mpp::Track::AudioA).size() == 2,
          "the razor cuts the picture and its sound");
    app->set_tool(TimelineTool::Select);
    shot("14_razor");

    // Select the right-hand piece and clear it.
    const mpp::Clip* piece = project().clip_at(mpp::Track::VideoA, 30);
    app->simulate_click(view->point_at(mpp::Track::VideoA, 30));
    check(piece && app->selection() == std::vector<int>{piece->id}, "clicking a clip selects it");
    app->clear_selected();
    check(!project().clip_at(mpp::Track::VideoA, 30) && !project().clip_at(mpp::Track::AudioA, 30),
          "Clear removes the clip and its sound");
    app->toggle_undo();
    check(project().clip_at(mpp::Track::VideoA, 30) != nullptr, "undo puts it back");

    // A move that would overlap is refused with a message.
    app->move_clip(project().clip_at(mpp::Track::VideoA, 90)->id, mpp::Track::VideoA, 10);
    check(project().clip_at(mpp::Track::VideoA, 95) != nullptr && !app->message().empty(),
          "an overlapping move is refused");

    // Draw a work area.
    app->simulate_drag(view->work_area_point(0), view->work_area_point(100));
    check(project().work_in == 0 && project().work_out == 100, "dragging the work area bar sets it");
    app->zoom(-1);
    shot("15_work_area");

    // Save, start over, open it again.
    const std::string saved = out_dir + "/selftest.mpp";
    check(app->save_to(saved), "save the project");
    check(!app->dirty(), "saving clears the unsaved mark");
    const std::size_t clip_count = project().clips.size();
    app->set_edit_line(0);
    app->menu_bar()->open(0);
    app->menu_bar()->choose("New Project");
    check(project().clips.empty(), "New Project after a save needs no prompt");
    check(app->open(saved), "open the saved project");
    check(project().clips.size() == clip_count && project().work_out == 100,
          "the clips and work area come back");
    app->razor(project().clip_at(mpp::Track::VideoA, 95)->id, 120);
    app->menu_bar()->open(0);
    app->menu_bar()->choose("New Project");
    check(app->discard_prompt_open(), "New Project with unsaved changes asks first");
    shot("16_prompt");
    app->answer_discard_prompt(false);
    check(project().clips.size() == clip_count + 1, "Cancel keeps the project");

    // Milestone 3: transitions. Back to 4 pixels a frame, which the
    // position helpers below assume.
    app->zoom(1);
    app->new_project();
    app->import(media("counter.mov"));
    app->import(media("still.png"));
    app->place(project().bin[0].id, mpp::Track::VideoA, 0);
    app->place(project().bin[1].id, mpp::Track::VideoB, 40);
    app->draw_all();
    app->simulate_drag(app->transitions_window()->row_center("cross_dissolve"),
                       view->transition_point(45));
    check(project().transitions.size() == 1, "dragging Cross Dissolve onto the T track adds it");
    const mpp::Transition* dissolve = project().transitions.empty() ? nullptr : &project().transitions[0];
    check(dissolve && dissolve->start == 40 && dissolve->length == 20 && !dissolve->settings.b_to_a,
          "it fits the A/B overlap, A to B");
    app->set_edit_line(50);
    app->draw_all();
    const mpp::Color mid = app->preview_window()->view()->frame().pixel(20, 120);
    // Frame 50 is 52.5% through: counter frame 25 (200, 55, 64) towards the
    // still's blue.
    check(std::abs(mid.r - 95) <= 2 && std::abs(mid.b - 164) <= 2, "Preview shows A and B mixed");
    shot("17_transition");

    // Double-click opens its settings.
    app->simulate_click(view->transition_point(50));
    app->simulate_click(view->transition_point(50), nanogui::MOD_DOUBLE_CLICK);
    check(app->transition_settings() != nullptr, "double-clicking a transition opens its settings");
    if (app->transition_settings()) {
      mpp::TransitionSettings s = app->transition_settings()->settings();
      s.start = 0.25;
      s.b_to_a = true;
      app->transition_settings()->set_settings(s);
      shot("18_transition_settings");
      app->transition_settings()->ok();
    }
    check(app->transition_settings() == nullptr, "OK closes the settings");
    check(project().transitions[0].settings.start == 0.25 && project().transitions[0].settings.b_to_a,
          "the settings are applied");
    app->toggle_undo();
    check(project().transitions[0].settings.start == 0.0, "and can be undone");
    app->edit_transition(project().transitions[0].id);
    if (app->transition_settings()) app->transition_settings()->cancel();
    check(app->transition_settings() == nullptr && project().transitions[0].settings.start == 0.0,
          "Cancel closes without changing anything");

    // Drag its end to shorten it, then its body to move it.
    app->simulate_drag(view->transition_point(59), view->transition_point(55));
    check(project().transitions[0].length == 15, "dragging a transition's end trims it");
    app->simulate_drag(view->transition_point(47), view->transition_point(57));
    check(project().transitions[0].start == 50, "dragging its body moves it");

    // Another one where there's no overlap lasts a second; Delete clears it.
    check(app->add_transition("iris_round", 150), "add Iris Round away from the overlap");
    check(project().transitions.size() == 2 && project().transition_at(150)->length == 30,
          "a lone transition lasts a second");
    app->clear_selected();
    check(project().transitions.size() == 1, "Clear removes the selected transition");
    app->open_settings();
    app->settings_window()->cancel();
    check(app->settings_window() == nullptr, "Cancel closes Project Settings");
    shot("19_transitions_window");
  }
  nanogui::shutdown();
  std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
  return failures ? 1 : 0;
}
