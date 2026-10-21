#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nanogui/screen.h>

#include "audio_player.h"
#include "construction_view.h"
#include "menu_bar.h"
#include "mpp/editor.h"
#include "mpp/export_job.h"
#include "mpp/media_cache.h"
#include "mpp/render_job.h"

class ClipWindow;
class ConstructionWindow;
class ExportWindow;
class FullScreenView;
class PreviewWindow;
class SequenceWindow;
class TitleWindow;
class ProjectWindow;
class SettingsWindow;
class TransitionSettingsWindow;
class TransparencyWindow;
class FiltersWindow;
class TransitionsWindow;
namespace nanogui {
class Label;
class MessageDialog;
}  // namespace nanogui

// The main window: menu bar, Project, Clip, Preview and Construction
// windows, and a status bar.
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

  // File. Return false and show the error in the status bar on failure.
  void new_project();
  bool open(const std::string& path);  // a .mpp project
  bool save_to(const std::string& path);
  // Whether there are changes since the project was opened or saved.
  bool dirty() const { return changes_ != saved_changes_; }
  // Adds a file to the Project window.
  bool import(const std::string& path);

  // Edit.
  void toggle_undo();
  // Clears what has focus: the selected clips, or the selected bin item.
  void clear_selected();
  void select_all_clips();

  // Project window.
  void select_item(int id);
  int selected_item() const { return selected_item_; }
  // A bin row dragged to a screen position; `released` drops it.
  void drag_item(int id, nanogui::Vector2i at, bool released);

  // Clip window. Positions are project frames into the item's source.
  void open_clip(int id);
  int clip_item() const { return clip_item_; }
  std::int64_t clip_position() const { return clip_pos_; }
  bool clip_playing() const { return playing_ == Playing::Clip; }
  void clip_seek(std::int64_t position);
  void clip_step(int frames);
  void clip_toggle_play();
  void clip_mark_in();
  void clip_mark_out();
  void clip_go_to_in();
  void clip_go_to_out();
  // Moves playback on as if `seconds` had passed. The app's clock is the
  // sound device while sound plays, real time otherwise; with
  // set_test_clock() only this moves it.
  void advance_playback(double seconds);
  // For the self-test: playback moves only by advance_playback(), and no
  // sound plays.
  void set_test_clock();

  // Construction window.
  bool place(int item, mpp::Track track, std::int64_t start);
  // Places the Clip window's item at the edit line, on Video A or else B.
  bool place_at_edit_line();
  void set_edit_line(std::int64_t frame);
  std::int64_t edit_line() const { return edit_line_; }
  // Moves the edit line to the previous or next clip edge.
  void jump_to_edit(int direction);
  // The selection holds clip and transition ids.
  void select(std::vector<int> ids);
  const std::vector<int>& selection() const { return selection_; }
  void move_clip(int clip, mpp::Track track, std::int64_t start);
  void trim_clip(int clip, std::int64_t start, std::int64_t in, std::int64_t out);
  void razor(int clip, std::int64_t frame);
  void set_work_area(std::int64_t in, std::int64_t out);

  // Transitions.
  // A transition dragged from the Transitions window to a screen position;
  // `released` drops it on the T track.
  void drag_transition(const std::string& kind, nanogui::Vector2i at, bool released);
  bool add_transition(const std::string& kind, std::int64_t frame);
  void set_transition_span(int id, std::int64_t start, std::int64_t length);
  // Opens the settings dialog for a transition; OK applies them.
  void edit_transition(int id);
  TransitionSettingsWindow* transition_settings() const { return transition_settings_; }

  // Super and sound clips.
  void set_fade(int clip, std::vector<mpp::FadePoint> fade);
  // Opens the Transparency dialog for a Super clip; OK applies it.
  void edit_key(int clip);
  TransparencyWindow* transparency() const { return transparency_; }
  // Opens the Filters dialog for a clip; OK applies the list.
  void edit_filters(int clip);
  FiltersWindow* filters_window() const { return filters_window_; }
  // A double-click on a clip: Transparency for Super, the Clip window
  // otherwise.
  void open_timeline_clip(int clip);
  void set_tool(TimelineTool tool);
  TimelineTool tool() const { return tool_; }
  void zoom(int steps);

  // Make. preview() renders the work area (or the whole program if there is
  // none) in the background unless an up-to-date render exists, then plays
  // it with sound from its start.
  void preview();
  void stop();  // playback, a render, an export or Print to Video
  bool rendering() const { return render_job_ != nullptr; }
  double render_progress() const;
  bool program_playing() const { return playing_ == Playing::Program; }
  // The rendered frame Preview shows while playing, as an index into the
  // render; -1 when not playing.
  std::int64_t program_frame() const;
  // The range Make uses: the work area, or the whole program.
  std::pair<std::int64_t, std::int64_t> make_range(bool whole_program) const;
  // Make > Movie: starts writing the movie in the background.
  bool export_movie(const std::string& path, mpp::MovieFormat format, bool whole_program);
  bool exporting() const { return export_job_ != nullptr; }
  double export_progress() const;
  void open_export_dialog();
  ExportWindow* export_window() const { return export_window_; }
  // Make > Frame: the program at the edit line as a PNG.
  bool export_frame(const std::string& path);
  // Make > Print to Video: plays the work area full screen.
  void print_to_video();
  bool printing() const { return fullscreen_ != nullptr; }
  FullScreenView* fullscreen() const { return fullscreen_; }

  // Titles (2.0). new_title() opens the Title window on a fresh title;
  // OK adds it to the Project window. edit_title() opens an existing one.
  void new_title();
  void edit_title(int item);
  TitleWindow* title_window() const { return title_window_; }

  // Sequence window (2.0).
  void set_sequence(std::vector<int> items);
  void add_to_sequence(int item, int row = -1);  // -1 appends
  bool place_sequence(const std::string& transition, int overlap);
  SequenceWindow* sequence_window() const { return sequence_; }

  // Project > Settings.
  void open_settings();
  SettingsWindow* settings_window() const { return settings_; }
  void apply_settings(const mpp::ProjectSettings& settings);

  MenuBar* menu_bar() const { return menu_; }
  ClipWindow* clip_window() const { return clip_window_; }
  ProjectWindow* project_window() const { return project_window_; }
  ConstructionWindow* construction_window() const { return construction_; }
  PreviewWindow* preview_window() const { return preview_; }
  TransitionsWindow* transitions_window() const { return transitions_; }
  bool discard_prompt_open() const { return prompt_ != nullptr; }
  void answer_discard_prompt(bool discard);

  // Draws one frame and saves exactly what is on screen as a PNG.
  void capture(const std::string& path);
  // Feeds real mouse events through NanoGUI (self-test).
  void simulate_click(nanogui::Vector2i at, int modifiers = 0);
  void simulate_drag(nanogui::Vector2i from, nanogui::Vector2i to, int modifiers = 0);

  bool keyboard_event(int key, int scancode, int action, int modifiers) override;
  bool mouse_button_event(const nanogui::Vector2i& p, int button, bool down,
                          int modifiers) override;
  bool drop_event(const std::vector<std::string>& filenames) override;
  bool resize_event(const nanogui::Vector2i& size) override;
  void draw_all() override;
  void draw_teardown() override;

 private:
  enum class Focus { Project, Clip, Timeline };
  enum class Playing { None, Clip, Program };

  // Runs a command; on failure shows the error and leaves things as they
  // were. Returns whether it succeeded.
  bool run(std::unique_ptr<mpp::Command> command);
  void changed();  // after a command, undo or redo
  void set_project(mpp::Project project, const std::string& name, const std::string& path);
  std::vector<Menu> build_menus();
  // Runs `then` now, or after the user agrees to drop unsaved changes.
  void confirm_discard(std::function<void()> then);
  void open_dialog();
  void save();
  void save_as_dialog();
  void import_dialog();
  void update_caption();
  // Length of the Clip window's scrub bar for the open item.
  std::int64_t clip_length() const;
  void clip_stop();
  void stop_playing();
  void tick_playback(double now);
  // Moves the clip or program playhead to the clock.
  void follow_clock();
  void start_program(bool full_screen);
  void finish_render();
  void finish_export();
  void end_print();
  void export_frame_dialog();
  // A double-click in the Project window: the Title window for a title,
  // the Clip window otherwise.
  void open_item(int id);
  mpp::Image thumbnail(int id);
  mpp::Image item_thumbnail(int item, std::int64_t position);
  // Brings the windows up to date. Widgets are rebuilt here, outside their
  // own callbacks.
  void refresh();
  void show_clip();
  void show_timeline();
  void layout_widgets();

  mpp::Editor editor_;
  mpp::MediaCache cache_;
  std::string message_;
  Focus focus_ = Focus::Project;
  int selected_item_ = 0;
  std::string doc_name_ = "Untitled";
  std::string doc_path_;             // the .mpp file to save to; empty until there is one
  std::uint64_t changes_ = 0;        // counts commands, undos and redos
  std::uint64_t saved_changes_ = 0;  // changes_ when last opened or saved
  std::string caption_;
  nanogui::MessageDialog* prompt_ = nullptr;
  std::function<void()> after_prompt_;

  int clip_item_ = 0;
  std::int64_t clip_pos_ = 0;
  Playing playing_ = Playing::None;
  double play_clock_ = 0;       // seconds of playback so far
  double play_last_time_ = -1;  // wall clock at the last tick
  bool sound_clock_ = false;    // the sound device is timing playback
  bool test_clock_ = false;
  std::int64_t play_from_ = 0;  // clip position when play started
  std::int64_t play_stop_ = 0;  // stops before this position
  AudioPlayer audio_;

  // The work area as last rendered, and what the project looked like then.
  std::shared_ptr<const mpp::RenderedProgram> rendered_;
  std::shared_ptr<const mpp::AudioBuffer> rendered_sound_;
  std::uint64_t rendered_changes_ = 0;
  std::uint64_t render_changes_ = 0;  // changes_ when the running render began
  std::unique_ptr<mpp::RenderJob> render_job_;
  bool print_after_render_ = false;
  std::int64_t program_index_ = -1;  // frame of rendered_ on show
  std::unique_ptr<mpp::ExportJob> export_job_;
  FullScreenView* fullscreen_ = nullptr;
  bool end_print_ = false;  // a click asked Print to Video to stop
  int clip_shown_item_ = -1;    // what the Clip window's picture is
  std::int64_t clip_shown_pos_ = -1;

  std::int64_t edit_line_ = 0;
  std::vector<int> selection_;  // clip and transition ids
  TimelineTool tool_ = TimelineTool::Select;
  std::int64_t preview_shown_frame_ = -1;  // what the Preview window shows
  std::uint64_t preview_shown_changes_ = 0;

  bool bin_stale_ = true;
  std::string capture_path_;
  MenuBar* menu_ = nullptr;
  ProjectWindow* project_window_ = nullptr;
  ClipWindow* clip_window_ = nullptr;
  PreviewWindow* preview_ = nullptr;
  ConstructionWindow* construction_ = nullptr;
  SettingsWindow* settings_ = nullptr;
  TransitionsWindow* transitions_ = nullptr;
  TransitionSettingsWindow* transition_settings_ = nullptr;
  TransparencyWindow* transparency_ = nullptr;
  FiltersWindow* filters_window_ = nullptr;
  ExportWindow* export_window_ = nullptr;
  TitleWindow* title_window_ = nullptr;
  SequenceWindow* sequence_ = nullptr;
  int titles_made_ = 0;  // for naming new titles
  nanogui::Label* status_ = nullptr;
};
