#include "selftest.h"

#include <chrono>
#include <cstdio>
#include <functional>
#include <thread>
#include <cstdlib>
#include <exception>
#include <string>

#include <nanogui/nanogui.h>
#include <nanogui/opengl.h>

#include "app.h"
#include "clip_window.h"
#include "filters_window.h"
#include "frame_view.h"
#include "menu_bar.h"
#include "project_window.h"
#include "mpp/project_file.h"
#include "sequence_window.h"
#include "settings_window.h"
#include "title_window.h"
#include "timeline_windows.h"
#include "key_window.h"
#include "make_windows.h"
#include "motion_window.h"
#include "mpp/ffmpeg_source.h"
#include "mpp/image_io.h"
#include "mpp/renderer.h"
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
    app->set_test_clock();  // silent, and playback moves only when told
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

    // Milestone 4: the Super track, transparency and rubber bands.
    app->new_project();
    app->import(media("counter.mov"));
    app->import(media("still.png"));
    app->place(project().bin[0].id, mpp::Track::VideoA, 0);
    app->place(project().bin[1].id, mpp::Track::Superimpose, 0);
    const int super = project().clip_at(mpp::Track::Superimpose, 0)->id;
    app->set_edit_line(10);
    app->draw_all();
    check(app->preview_window()->view()->frame().pixel(20, 120) == mpp::Color{0, 0, 255, 255},
          "with no key, the Super clip covers Video A");
    check(app->preview_window()->view()->frame().pixel(20, 10) == mpp::Color{40, 215, 64, 255},
          "but not where it's letterboxed");

    app->simulate_click(view->point_at(mpp::Track::Superimpose, 30));
    app->simulate_click(view->point_at(mpp::Track::Superimpose, 30), nanogui::MOD_DOUBLE_CLICK);
    check(app->transparency() != nullptr, "double-clicking a Super clip opens Transparency");
    if (app->transparency()) {
      mpp::TransparencyKey key;
      key.type = mpp::KeyType::BlueScreen;
      app->transparency()->set_key(key);
      shot("20_transparency");
      app->transparency()->ok();
    }
    check(project().clip(super)->key.type == mpp::KeyType::BlueScreen, "Blue Screen is applied");
    app->draw_all();
    check(app->preview_window()->view()->frame().pixel(20, 120) == mpp::Color{40, 215, 64, 255},
          "Video A shows through where the still is blue");
    check(app->preview_window()->view()->frame().pixel(300, 120) == mpp::Color{255, 255, 0, 255},
          "and the yellow stays");

    // Rubber band: pull the line down at frame 20, then up again at 60.
    app->simulate_drag(view->fade_point(mpp::Track::Superimpose, 20, 1.0),
                       view->fade_point(mpp::Track::Superimpose, 20, 0.0));
    check(project().clip(super)->fade.size() == 1 && project().clip(super)->fade[0].level == 0.0,
          "dragging the Super rubber band adds a handle at 0%");
    app->simulate_drag(view->fade_point(mpp::Track::Superimpose, 60, 0.0),
                       view->fade_point(mpp::Track::Superimpose, 60, 1.0));
    const auto& fade = project().clip(super)->fade;
    check(fade.size() == 2 && fade[1].level == 1.0 && std::abs(fade[1].position - 60.25) < 0.01,
          "a second handle at 100%");
    app->set_edit_line(40);
    app->draw_all();
    const mpp::Color faded = app->preview_window()->view()->frame().pixel(300, 120);
    // Level 0.49 at frame 40: yellow over counter frame 20 (160, 95, 64).
    check(std::abs(faded.r - 206) <= 3 && std::abs(faded.b - 33) <= 3,
          "half way between the handles the still is half faded");
    shot("21_fade");
    app->simulate_drag(view->fade_point(mpp::Track::Superimpose, 20, 0.0),
                       view->fade_point(mpp::Track::Superimpose, 20, 0.0) + nanogui::Vector2i(0, 120));
    check(project().clip(super)->fade.size() == 1, "dragging a handle off the track removes it");

    // Sound has a volume rubber band too.
    app->simulate_drag(view->fade_point(mpp::Track::AudioA, 30, 1.0),
                       view->fade_point(mpp::Track::AudioA, 30, 0.5));
    const mpp::Clip* sound_clip = project().clip_at(mpp::Track::AudioA, 30);
    check(sound_clip && sound_clip->fade.size() == 1 &&
              std::abs(sound_clip->fade[0].level - 0.5) < 0.06,
          "dragging the audio rubber band sets a volume handle");
    shot("22_volume");
    app->toggle_undo();
    check(project().clip_at(mpp::Track::AudioA, 30)->fade.empty(), "undo takes the handle away");

    // Milestone 5: filters.
    app->new_project();
    app->import(media("still.png"));
    app->import(media("counter.mov"));
    app->place(project().bin[0].id, mpp::Track::VideoA, 0);
    app->place(project().bin[1].id, mpp::Track::VideoB, 200);
    const mpp::Clip* still_on_a = project().clip_at(mpp::Track::VideoA, 0);
    const int filtered = still_on_a ? still_on_a->id : 0;
    app->draw_all();
    app->simulate_click(view->point_at(mpp::Track::VideoA, 40));
    app->menu_bar()->open(3);
    check(app->menu_bar()->choose("Filters..."), "Clip > Filters... is there for a selected clip");
    FiltersWindow* filters = app->filters_window();
    check(filters != nullptr, "and opens the Filters dialog");
    if (filters) {
      filters->add("brightness_contrast");
      filters->set_value(0, false, -100);
      filters->set_value(0, true, 100);
      filters->add("invert");
      filters->move(-1);
      check(filters->filters().size() == 2 && filters->filters()[0].id == "invert" &&
                filters->selected() == 0,
            "Up moves Invert first");
      filters->remove();
      filters->add("mosaic");
      filters->set_value(0, false, 30.4);
      check(filters->filters()[1].start[0] == 30, "whole-number settings round");
      app->draw_all();
      shot("23_filters");
      filters->ok();
    }
    check(app->filters_window() == nullptr, "OK closes the dialog");
    const mpp::Clip* with_filters = project().clip(filtered);
    check(with_filters && with_filters->filters.size() == 2 &&
              with_filters->filters[0].id == "brightness_contrast" &&
              with_filters->filters[0].end[0] == 100,
          "the clip has Brightness & Contrast then Mosaic");
    app->set_edit_line(0);
    app->draw_all();
    const mpp::Color dark = app->preview_window()->view()->frame().pixel(300, 120);
    app->set_edit_line(149);
    app->draw_all();
    const mpp::Color bright = app->preview_window()->view()->frame().pixel(300, 120);
    check(dark.r < 200 && bright.r == 255 && dark.r < bright.r,
          "Preview shows the brightness ramp from the first frame to the last");
    shot("24_filtered");

    // A sound clip gets the audio filters.
    const mpp::Clip* sound_b = project().clip_at(mpp::Track::AudioA, 210);
    app->select(sound_b ? std::vector<int>{sound_b->id} : std::vector<int>{});
    app->edit_filters(sound_b ? sound_b->id : 0);
    if (app->filters_window()) {
      app->filters_window()->add("backwards");
      app->filters_window()->add("pan");
      app->filters_window()->set_value(0, true, 100);
      shot("25_audio_filters");
      app->filters_window()->ok();
    }
    sound_b = project().clip_at(mpp::Track::AudioA, 210);
    check(sound_b && sound_b->filters.size() == 2 && sound_b->filters[1].end[0] == 100,
          "the sound clip has Backwards and a Pan sweep");
    app->toggle_undo();
    check(project().clip_at(mpp::Track::AudioA, 210)->filters.empty(), "undo removes them");

    // Milestone 6: preview, playback and making movies.
    const auto wait_for = [&](const std::function<bool()>& busy) {
      for (int i = 0; i < 3000 && busy(); ++i) {
        app->draw_all();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      app->draw_all();
    };
    app->new_project();
    {
      mpp::ProjectSettings s = project().settings;
      s.width = 160;
      s.height = 120;
      s.rate = {15, 1, false};
      s.audio_bits = 8;
      app->apply_settings(s);  // Premiere 1.0's size, rate and sound
    }
    app->import(media("counter.mov"));
    app->import(media("still.png"));
    app->place(project().bin[0].id, mpp::Track::VideoA, 0);
    app->place(project().bin[1].id, mpp::Track::VideoB, 20);
    app->add_transition("cross_dissolve", 25);
    app->set_work_area(0, 30);
    app->preview();
    check(app->rendering(), "Make > Preview starts rendering the work area");
    shot("26_rendering");
    wait_for([&] { return app->rendering(); });
    check(!app->rendering() && app->program_playing() && app->program_frame() == 0,
          "when the render is done it plays from the start");
    mpp::MediaCache check_cache;
    app->advance_playback(1.0);
    app->draw_all();
    check(app->program_frame() == 15 && app->edit_line() == 15, "a second later it shows frame 15");
    check(app->preview_window()->view()->frame() == mpp::render_frame(project(), check_cache, 15),
          "Preview shows the rendered frame");
    shot("27_playing");
    app->advance_playback(5.0);
    app->draw_all();
    check(!app->program_playing() && app->edit_line() == 29, "it stops on the last frame");
    app->preview();
    check(!app->rendering() && app->program_playing(), "with nothing changed it plays without rendering");
    app->stop();
    app->set_work_area(5, 30);
    app->preview();
    check(app->rendering(), "after a change it renders again");
    app->stop();
    check(!app->rendering() && app->message() == "Render cancelled", "Esc cancels a render");

    // Make > Movie, as a lossless QuickTime of the whole program.
    app->menu_bar()->open(4);
    shot("28_make_menu");
    app->menu_bar()->close();
    app->open_export_dialog();
    check(app->export_window() != nullptr, "Make > Movie... opens its dialog");
    shot("29_make_movie");
    if (app->export_window()) app->export_window()->cancel();
    const std::string movie_path = out_dir + "/selftest_movie";
    check(app->export_movie(movie_path, mpp::MovieFormat::Animation, true), "export starts");
    wait_for([&] { return app->exporting(); });
    check(app->message() == "Made " + movie_path + ".mov", "and finishes");
    try {
      mpp::FfmpegSource made(movie_path + ".mov");
      check(made.info().frames == project().duration() && made.info().has_audio,
            "the movie has every frame and sound");
      check(made.frame(25) == mpp::render_frame(project(), check_cache, 25),
            "and its frames are exactly the program's");
    } catch (const std::exception& e) {
      check(false, e.what());
    }

    app->set_edit_line(27);
    check(app->export_frame(out_dir + "/selftest_frame"), "Make > Frame saves a PNG");
    check(mpp::load_image(out_dir + "/selftest_frame.png") == mpp::render_frame(project(), check_cache, 27),
          "of the frame at the edit line");

    // Print to Video plays the work area full screen.
    app->print_to_video();
    wait_for([&] { return app->rendering(); });
    check(app->printing() && app->program_playing(), "Print to Video plays full screen");
    app->advance_playback(0.5);
    app->draw_all();
    shot("30_print_to_video");
    app->stop();
    app->draw_all();
    check(!app->printing(), "Esc ends it");

    // Milestone 7: titles and the Sequence window.
    app->new_project();
    app->import(media("counter.mov"));
    app->import(media("still.png"));
    app->import(media("h264.mp4"));
    const int counter_item = project().bin[0].id;
    const int still_item = project().bin[1].id;
    const int grey_item = project().bin[2].id;
    app->menu_bar()->open(0);
    check(app->menu_bar()->choose("New Title"), "File > New Title");
    TitleWindow* title = app->title_window();
    check(title != nullptr && title->title().texts.size() == 1 &&
              title->selection().kind == TitleWindow::Selection::Kind::Text,
          "opens the Title window with a text selected");
    if (title) {
      title->set_text("Hello");
      title->add_text("World");
      title->move_selected_to(0.5, 0.75);
      title->add_shape(mpp::ShapeKind::Oval);
      title->move_selected_to(0.15, 0.2);
      app->draw_all();
      // Drag "World" up and left on the canvas.
      app->simulate_drag(title->canvas_point(0.5, 0.75), title->canvas_point(0.4, 0.65));
      const mpp::TitleText& world = title->title().texts[1];
      check(title->selection().kind == TitleWindow::Selection::Kind::Text &&
                title->selection().index == 1 && std::abs(world.x - 0.4) < 0.01 &&
                std::abs(world.y - 0.65) < 0.01,
            "dragging a text on the canvas moves it");
      shot("31_title_window");
      title->ok();
    }
    const mpp::MediaItem* made = project().bin.empty() ? nullptr : &project().bin.back();
    check(made && made->title && made->name == "Title 1" && made->title->texts.size() == 2 &&
              made->title->shapes.size() == 1,
          "OK puts the title in the Project window");
    const int title_item = made ? made->id : 0;

    // Double-click it to edit it.
    app->draw_all();
    app->project_window()->perform_layout(app->nvg_context());
    app->simulate_click(app->project_window()->row_center(title_item));
    app->simulate_click(app->project_window()->row_center(title_item), nanogui::MOD_DOUBLE_CLICK);
    check(app->title_window() != nullptr, "double-clicking a title opens it in the Title window");
    if (app->title_window()) {
      app->title_window()->set_name("Opening");
      app->title_window()->ok();
    }
    check(project().item(title_item)->name == "Opening", "the edit renames it");
    app->toggle_undo();
    check(project().item(title_item)->name == "Title 1", "and undo puts the name back");
    app->toggle_undo();

    // Superimposed with its own transparency.
    app->place(counter_item, mpp::Track::VideoA, 0);
    app->place(title_item, mpp::Track::Superimpose, 0);
    mpp::TransparencyKey alpha;
    alpha.type = mpp::KeyType::Alpha;
    {
      const mpp::Clip* super_title = project().clip_at(mpp::Track::Superimpose, 0);
      if (super_title) {
        app->select({super_title->id});
        app->edit_key(super_title->id);
        if (app->transparency()) {
          app->transparency()->set_key(alpha);
          app->transparency()->ok();
        }
      }
    }
    app->set_edit_line(10);
    app->draw_all();
    {
      const mpp::Image& shown = app->preview_window()->view()->frame();
      const mpp::Rect words = mpp::text_bounds(project().item(title_item)->title->texts[0], 320, 240);
      int white = 0, counter_green = 0;
      for (int y = words.y; y < words.bottom(); ++y) {
        for (int x = words.x; x < words.right(); ++x) {
          if (shown.pixel(x, y) == mpp::kWhite) ++white;
        }
      }
      if (shown.pixel(300, 10) == mpp::Color{40, 215, 64, 255}) ++counter_green;
      check(white > 20 && counter_green == 1, "the title's words show over Video A, which shows around them");
    }
    shot("32_title_superimposed");

    // The Sequence window.
    app->set_edit_line(200);
    app->menu_bar()->open(5);
    check(app->menu_bar()->choose("Sequence") && app->sequence_window()->visible(),
          "Windows > Sequence shows it");
    app->add_to_sequence(counter_item);
    app->draw_all();
    app->project_window()->perform_layout(app->nvg_context());
    app->simulate_drag(app->project_window()->row_center(grey_item),
                       app->sequence_window()->absolute_position() + nanogui::Vector2i(150, 60));
    app->select_item(still_item);
    app->menu_bar()->open(3);
    app->menu_bar()->choose("Add to Sequence");
    check(project().sequence == std::vector<int>{counter_item, grey_item, still_item},
          "items arrive by Add, by dragging from the Project window, and from the Clip menu");
    app->draw_all();
    app->sequence_window()->select(2);
    app->sequence_window()->move(-1);
    check(project().sequence == std::vector<int>{counter_item, still_item, grey_item}, "Up reorders");
    app->sequence_window()->choose("cross_dissolve", 10);
    app->draw_all();
    shot("33_sequence");
    app->sequence_window()->place();
    check(project().clip_at(mpp::Track::VideoA, 200) && project().clip_at(mpp::Track::VideoB, 260) &&
              project().clip_at(mpp::Track::VideoA, 420) && project().transitions.size() == 2,
          "Place lays the sequence out from the edit line, alternating A and B with dissolves");
    shot("34_sequence_placed");
    app->toggle_undo();
    check(project().clips_on(mpp::Track::VideoB).empty() && project().transitions.empty(),
          "one undo takes it all back");

    // Milestone 8: motion and speed.
    app->new_project();
    app->import(media("counter.mov"));
    app->import(media("still.png"));
    app->place(project().bin[0].id, mpp::Track::VideoA, 0);
    const int moving = project().clip_at(mpp::Track::VideoA, 0)->id;
    app->draw_all();
    app->simulate_click(view->point_at(mpp::Track::VideoA, 20));
    app->menu_bar()->open(3);
    check(app->menu_bar()->choose("Motion..."), "Clip > Motion... is there for a picture clip");
    MotionWindow* motion = app->motion_window();
    check(motion && motion->motion().points.size() == 2 && motion->motion().points[0].x == -0.5,
          "it opens on Premiere's left-to-right path");
    if (motion) {
      motion->add_point();
      check(motion->motion().points.size() == 3 && motion->selected() == 1 &&
                std::abs(motion->motion().points[1].x - 0.5) < 1e-9,
            "Add Point puts one half way along");
      app->draw_all();
      app->simulate_drag(motion->stage_point(0.5, 0.5), motion->stage_point(0.5, 0.25));
      check(std::abs(motion->motion().points[1].y - 0.25) < 0.01,
            "dragging a point on the stage moves it");
      mpp::MotionPoint p = motion->motion().points[1];
      p.rotation = 90;
      p.zoom = 50;
      motion->set_point(p);
      motion->set_preview(0.5);
      app->draw_all();
      shot("35_motion");
      motion->ok();
    }
    check(project().clip(moving) && project().clip(moving)->motion &&
              project().clip(moving)->motion->points.size() == 3,
          "OK gives the clip its path");
    app->set_edit_line(30);
    app->draw_all();
    {
      const mpp::Image& shown = app->preview_window()->view()->frame();
      check(shown.pixel(160, 60) != mpp::kBlack && shown.pixel(160, 200) == mpp::kBlack &&
                shown.pixel(20, 120) == mpp::kBlack,
            "half way along, the clip is small and high in the frame");
    }

    app->menu_bar()->open(3);
    check(app->menu_bar()->choose("Speed..."), "Clip > Speed...");
    if (app->speed_window()) {
      app->speed_window()->set_speed(50);
      shot("36_speed");
      app->speed_window()->ok();
    }
    const mpp::Clip* slowed = project().clip(moving);
    check(slowed && slowed->speed == 50 && slowed->length() == 120 && project().clip(slowed->link) &&
              project().clip(slowed->link)->length() == 120,
          "half speed doubles the clip and its sound");
    app->draw_all();
    shot("37_slowed");
    app->toggle_undo();
    check(project().clip(moving) && project().clip(moving)->length() == 60, "undo restores the speed");
    app->toggle_undo();
    app->place(project().bin[1].id, mpp::Track::VideoA, 120);
    app->edit_speed(moving);
    if (app->speed_window()) {
      app->speed_window()->set_speed(25);
      app->speed_window()->ok();
    }
    check(project().clip(moving) && project().clip(moving)->speed == 50 && !app->message().empty(),
          "a speed that would run into the next clip is refused");
  }
  nanogui::shutdown();
  std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
  return failures ? 1 : 0;
}
