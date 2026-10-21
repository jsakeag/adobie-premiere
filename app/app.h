#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <nanogui/screen.h>

#include "menu_bar.h"
#include "mpp/editor.h"
#include "mpp/media_cache.h"

class ClipWindow;
class ProjectWindow;
class SettingsWindow;
namespace nanogui {
class Label;
}

// The main window: menu bar, Project window, Clip window and status bar.
//
// Every edit is a command run through editor_. The public actions are what
// the menus, windows and shortcuts call, so the self-test can drive the
// same paths.
class App : public nanogui::Screen {
 public:
  App();

  const mpp::Editor& editor() const { return editor_; }
  const mpp::Project& project() const { return editor_.project(); }
  mpp::MediaCache& cache() { return cache_; }
  // The last error or note shown in the status bar; empty if none.
  const std::string& message() const { return message_; }

  // File.
  void new_project();
  // Adds a file to the Project window. Returns false and shows why if it
  // can't be read.
  bool import(const std::string& path);
  void import_dialog();

  // Edit.
  void toggle_undo();
  void clear_selected();

  // Project window.
  void select_item(int id);
  int selected_item() const { return selected_item_; }

  // Clip window. Positions are project frames into the item's source.
  void open_clip(int id);
  int clip_item() const { return clip_item_; }
  std::int64_t clip_position() const { return clip_pos_; }
  bool clip_playing() const { return clip_playing_; }
  void clip_seek(std::int64_t position);
  void clip_step(int frames);
  void clip_toggle_play();
  void clip_mark_in();
  void clip_mark_out();
  void clip_go_to_in();
  void clip_go_to_out();
  // Moves playback on as if `seconds` had passed (the self-test's clock;
  // the app uses real time).
  void advance_playback(double seconds);

  // Project > Settings.
  void open_settings();
  SettingsWindow* settings_window() const { return settings_; }
  void apply_settings(const mpp::ProjectSettings& settings);

  MenuBar* menu_bar() const { return menu_; }
  ClipWindow* clip_window() const { return clip_window_; }
  ProjectWindow* project_window() const { return project_window_; }

  // Draws one frame and saves exactly what is on screen as a PNG.
  void capture(const std::string& path);
  // Feeds real mouse events through NanoGUI (self-test).
  void simulate_click(nanogui::Vector2i at, int modifiers = 0);
  void simulate_drag(nanogui::Vector2i from, nanogui::Vector2i to);

  bool keyboard_event(int key, int scancode, int action, int modifiers) override;
  bool mouse_button_event(const nanogui::Vector2i& p, int button, bool down,
                          int modifiers) override;
  bool drop_event(const std::vector<std::string>& filenames) override;
  bool resize_event(const nanogui::Vector2i& size) override;
  void draw_all() override;
  void draw_teardown() override;

 private:
  enum class Focus { Project, Clip };

  // Runs a command; on failure shows the error and leaves things as they
  // were. Returns whether it succeeded.
  bool run(std::unique_ptr<mpp::Command> command);
  void set_project(mpp::Project project);
  std::vector<Menu> build_menus();
  // Length of the Clip window's scrub bar for the open item.
  std::int64_t clip_length() const;
  void clip_stop();
  void tick_playback(double now);
  mpp::Image thumbnail(int id);
  // Brings the windows up to date. Widgets are rebuilt here, outside their
  // own callbacks.
  void refresh();
  void show_clip();
  void layout_widgets();

  mpp::Editor editor_;
  mpp::MediaCache cache_;
  std::string message_;
  Focus focus_ = Focus::Project;
  int selected_item_ = 0;

  int clip_item_ = 0;
  std::int64_t clip_pos_ = 0;
  bool clip_playing_ = false;
  double play_clock_ = 0;            // seconds of playback so far
  double play_last_time_ = -1;       // wall clock at the last tick
  std::int64_t play_from_ = 0;       // position when play started
  std::int64_t play_stop_ = 0;       // stops before this position
  int clip_shown_item_ = -1;         // what the Clip window's picture is
  std::int64_t clip_shown_pos_ = -1;

  bool bin_stale_ = true;
  std::string capture_path_;
  MenuBar* menu_ = nullptr;
  ProjectWindow* project_window_ = nullptr;
  ClipWindow* clip_window_ = nullptr;
  SettingsWindow* settings_ = nullptr;
  nanogui::Label* status_ = nullptr;
};
