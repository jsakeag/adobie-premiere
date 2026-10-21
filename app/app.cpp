#include "app.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <utility>

#include <nanogui/label.h>
#include <nanogui/messagedialog.h>
#include <nanogui/opengl.h>

#include "clip_window.h"
#include "frame_view.h"
#include "mpp/bin_commands.h"
#include "mpp/image_io.h"
#include "mpp/project_file.h"
#include "mpp/renderer.h"
#include "mpp/resample.h"
#include "mpp/time_scale.h"
#include "mpp/timeline_commands.h"
#include "project_window.h"
#include "settings_window.h"
#include "timeline_windows.h"

namespace {

constexpr int kStatusHeight = 24;
constexpr int kMargin = 10;

const std::vector<std::pair<std::string, std::string>> kImportTypes = {
    {"mov", "QuickTime movie"}, {"mp4", "MPEG-4 movie"}, {"m4v", "MPEG-4 movie"},
    {"avi", "AVI movie"},       {"mkv", "Matroska"},     {"png", "PNG"},
    {"jpg", "JPEG"},            {"jpeg", "JPEG"},        {"bmp", "BMP"},
    {"gif", "GIF"},             {"tga", "Targa"},        {"wav", "WAVE sound"},
    {"aif", "AIFF sound"},      {"aiff", "AIFF sound"},  {"mp3", "MP3 sound"},
    {"m4a", "AAC sound"}};

const std::vector<std::pair<std::string, std::string>> kProjectTypes = {
    {"mpp", "Mock Premiere project"}};

}  // namespace

App::App() : nanogui::Screen(nanogui::Vector2i(1440, 900), "Mock Premiere") {
  menu_ = new MenuBar(this, build_menus());

  ProjectWindowActions bin;
  bin.import = [this] { import_dialog(); };
  bin.select = [this](int id) {
    focus_ = Focus::Project;
    select_item(id);
  };
  bin.open = [this](int id) { open_clip(id); };
  bin.clear = [this] {
    focus_ = Focus::Project;
    clear_selected();
  };
  bin.drag = [this](int id, nanogui::Vector2i at, bool released) { drag_item(id, at, released); };
  bin.thumbnail = [this](int id) { return thumbnail(id); };
  project_window_ = new ProjectWindow(this, std::move(bin));

  ClipWindowActions clip;
  clip.seek = [this](std::int64_t f) { clip_seek(f); };
  clip.toggle_play = [this] { clip_toggle_play(); };
  clip.step = [this](int d) { clip_step(d); };
  clip.mark_in = [this] { clip_mark_in(); };
  clip.mark_out = [this] { clip_mark_out(); };
  clip.go_to_in = [this] { clip_go_to_in(); };
  clip.go_to_out = [this] { clip_go_to_out(); };
  clip.focus = [this] { focus_ = Focus::Clip; };
  clip_window_ = new ClipWindow(this, std::move(clip));

  preview_ = new PreviewWindow(this);

  ConstructionActions timeline;
  timeline.set_edit_line = [this](std::int64_t f) { set_edit_line(f); };
  timeline.select = [this](std::vector<int> c) { select_clips(std::move(c)); };
  timeline.move = [this](int c, mpp::Track t, std::int64_t s) { move_clip(c, t, s); };
  timeline.trim = [this](int c, std::int64_t s, std::int64_t i, std::int64_t o) {
    trim_clip(c, s, i, o);
  };
  timeline.razor = [this](int c, std::int64_t f) { razor(c, f); };
  timeline.set_work_area = [this](std::int64_t i, std::int64_t o) { set_work_area(i, o); };
  timeline.focus = [this] { focus_ = Focus::Timeline; };
  timeline.thumbnail = [this](int item, std::int64_t pos) { return item_thumbnail(item, pos); };
  ConstructionToolbarActions toolbar;
  toolbar.set_tool = [this](TimelineTool t) { set_tool(t); };
  toolbar.zoom = [this](int steps) { zoom(steps); };
  construction_ = new ConstructionWindow(this, std::move(timeline), std::move(toolbar));

  status_ = new nanogui::Label(this, "");
  const int top = MenuBar::kHeight + kMargin;
  project_window_->set_position({kMargin, top});
  clip_window_->set_position({440, top});
  preview_->set_position({790, top});
  layout_widgets();
  set_project(mpp::Project(), "Untitled", "");
}

std::vector<Menu> App::build_menus() {
  std::vector<Menu> menus;
  menus.push_back({"File", [this] {
    return std::vector<MenuItem>{
        {"New Project", "Cmd+N", [this] { confirm_discard([this] { new_project(); }); }},
        {"Open...", "Cmd+O", [this] { open_dialog(); }},
        {"", "", nullptr},
        {"Save", "Cmd+S", [this] { save(); }},
        {"Save As...", "Cmd+Shift+S", [this] { save_as_dialog(); }},
        {"", "", nullptr},
        {"Import...", "Cmd+I", [this] { import_dialog(); }},
        {"", "", nullptr},
        {"Quit", "Cmd+Q", [this] { confirm_discard([this] { set_visible(false); }); }},
    };
  }});
  menus.push_back({"Edit", [this] {
    const std::string undo = editor_.can_redo()   ? "Redo " + editor_.redo_name()
                             : editor_.can_undo() ? "Undo " + editor_.undo_name()
                                                  : "Undo";
    const bool clips = focus_ == Focus::Timeline && !selected_clips_.empty();
    return std::vector<MenuItem>{
        {undo, "Cmd+Z", [this] { toggle_undo(); }, editor_.can_undo() || editor_.can_redo()},
        {"", "", nullptr},
        {"Clear", "Delete", [this] { clear_selected(); }, clips || selected_item_ != 0},
        {"Select All", "Cmd+A", [this] { select_all_clips(); }, !project().clips.empty()},
    };
  }});
  menus.push_back({"Project", [this] {
    const bool program = project().duration() > 0;
    return std::vector<MenuItem>{
        {"Settings...", "", [this] { open_settings(); }},
        {"", "", nullptr},
        {"Work Area to Program", "",
         [this] { set_work_area(0, project().duration()); }, program},
        {"", "", nullptr},
        {"Zoom In", "=", [this] { zoom(1); }},
        {"Zoom Out", "-", [this] { zoom(-1); }},
    };
  }});
  menus.push_back({"Clip", [this] {
    const bool open = clip_item_ != 0;
    return std::vector<MenuItem>{
        {"Open in Clip Window", "Return", [this] { open_clip(selected_item_); },
         selected_item_ != 0},
        {"Place at Edit Line", "P", [this] { place_at_edit_line(); }, open},
        {"", "", nullptr},
        {"Play / Stop", "Space", [this] { clip_toggle_play(); }, open},
        {"Mark In", "I", [this] { clip_mark_in(); }, open},
        {"Mark Out", "O", [this] { clip_mark_out(); }, open},
        {"Go to In", "Home", [this] { clip_go_to_in(); }, open},
        {"Go to Out", "End", [this] { clip_go_to_out(); }, open},
    };
  }});
  menus.push_back({"Windows", [this] {
    const auto show = [this](nanogui::Window* w) {
      return [this, w] {
        w->set_visible(true);
        move_window_to_front(w);
      };
    };
    return std::vector<MenuItem>{
        {"Project", "", show(project_window_), true, project_window_->visible()},
        {"Clip", "", show(clip_window_), true, clip_window_->visible()},
        {"Preview", "", show(preview_), true, preview_->visible()},
        {"Construction", "", show(construction_), true, construction_->visible()},
    };
  }});
  return menus;
}

// ---- File -----------------------------------------------------------------

void App::new_project() {
  set_project(mpp::Project(), "Untitled", "");
  message_ = "New project";
}

void App::set_project(mpp::Project project, const std::string& name, const std::string& path) {
  clip_stop();
  editor_ = mpp::Editor(std::move(project));
  selected_item_ = 0;
  selected_clips_.clear();
  clip_item_ = 0;
  clip_pos_ = 0;
  clip_shown_item_ = -1;
  edit_line_ = 0;
  message_.clear();
  doc_name_ = name;
  doc_path_ = path;
  ++changes_;
  saved_changes_ = changes_;
  bin_stale_ = true;
  update_caption();
}

bool App::open(const std::string& path) {
  try {
    mpp::Project p = mpp::load_project(path);
    set_project(std::move(p), mpp::file_name(path), path);
  } catch (const std::exception& e) {
    message_ = e.what();
    return false;
  }
  // Say which files couldn't be found; their clips show black.
  int missing = 0;
  for (const mpp::MediaItem& item : project().bin) {
    try {
      cache_.info(item.path);
    } catch (const std::exception&) {
      ++missing;
    }
  }
  message_ = missing ? std::to_string(missing) + " file(s) could not be found" : "Opened " + path;
  return true;
}

bool App::save_to(const std::string& path) {
  std::string target = path;
  if (!mpp::is_project_path(target)) target += mpp::kProjectExtension;
  try {
    mpp::save_project(project(), target);
  } catch (const std::exception& e) {
    message_ = e.what();
    return false;
  }
  doc_path_ = target;
  doc_name_ = mpp::file_name(target);
  saved_changes_ = changes_;
  message_ = "Saved " + target;
  update_caption();
  return true;
}

void App::save() {
  if (doc_path_.empty()) {
    save_as_dialog();
  } else {
    save_to(doc_path_);
  }
}

void App::save_as_dialog() {
  const std::vector<std::string> paths =
      nanogui::file_dialog(this, nanogui::FileDialogType::Save, kProjectTypes);
  if (!paths.empty()) save_to(paths.front());
}

void App::open_dialog() {
  confirm_discard([this] {
    const std::vector<std::string> paths =
        nanogui::file_dialog(this, nanogui::FileDialogType::Open, kProjectTypes);
    if (!paths.empty()) open(paths.front());
  });
}

bool App::import(const std::string& path) {
  mpp::MediaItem item;
  try {
    item = mpp::make_item(path, cache_.info(path), project().settings);
  } catch (const std::exception& e) {
    message_ = e.what();
    return false;
  }
  if (!run(std::make_unique<mpp::AddItemCommand>(item))) return false;
  selected_item_ = project().bin.back().id;
  message_ = "Imported " + item.name;
  return true;
}

void App::import_dialog() {
  const std::vector<std::string> paths =
      nanogui::file_dialog(this, nanogui::FileDialogType::OpenMultiple, kImportTypes);
  for (const std::string& path : paths) import(path);
}

void App::update_caption() {
  const std::string caption = doc_name_ + (dirty() ? " *" : "") + " - Mock Premiere";
  if (caption != caption_) {
    caption_ = caption;
    set_caption(caption);
  }
}

void App::confirm_discard(std::function<void()> then) {
  if (!dirty()) {
    then();
    return;
  }
  if (prompt_) return;  // already asking
  after_prompt_ = std::move(then);
  prompt_ = new nanogui::MessageDialog(this, nanogui::MessageDialog::Type::Warning,
                                       "Unsaved changes",
                                       "Discard the changes to " + doc_name_ + "?", "Discard",
                                       "Cancel", true);
  prompt_->set_callback([this](int choice) {
    prompt_ = nullptr;  // the dialog disposes of itself after this
    std::function<void()> then = std::move(after_prompt_);
    after_prompt_ = nullptr;
    if (choice == 0 && then) then();
  });
}

void App::answer_discard_prompt(bool discard) {
  if (!prompt_) return;
  nanogui::MessageDialog* dialog = prompt_;
  dialog->callback()(discard ? 0 : 1);
  dialog->dispose();
}

// ---- Edit -----------------------------------------------------------------

bool App::run(std::unique_ptr<mpp::Command> command) {
  try {
    editor_.execute(std::move(command));
  } catch (const std::exception& e) {
    message_ = e.what();
    return false;
  }
  message_.clear();
  changed();
  return true;
}

void App::changed() {
  ++changes_;
  bin_stale_ = true;
  clip_shown_item_ = -1;
  update_caption();
}

void App::toggle_undo() {
  if (editor_.toggle_undo()) {
    message_.clear();
    changed();
  }
}

void App::clear_selected() {
  if (focus_ == Focus::Timeline && !selected_clips_.empty()) {
    if (run(std::make_unique<mpp::DeleteClipsCommand>(selected_clips_))) selected_clips_.clear();
    return;
  }
  if (selected_item_ == 0) return;
  const int id = selected_item_;
  if (run(std::make_unique<mpp::RemoveItemCommand>(id))) {
    selected_item_ = 0;
    if (clip_item_ == id) {
      clip_stop();
      clip_item_ = 0;
    }
  }
}

void App::select_all_clips() {
  selected_clips_.clear();
  for (const mpp::Clip& c : project().clips) selected_clips_.push_back(c.id);
  focus_ = Focus::Timeline;
}

// ---- Project window -------------------------------------------------------

void App::select_item(int id) {
  selected_item_ = project().item(id) ? id : 0;
  bin_stale_ = true;
}

void App::drag_item(int id, nanogui::Vector2i at, bool released) {
  const mpp::MediaItem* item = project().item(id);
  ConstructionView* view = construction_->view();
  if (!item) {
    view->show_drop(std::nullopt, 0);
    return;
  }
  const std::int64_t length = item->out - item->in;
  const auto target =
      view->drop_target(at, length, item->info.has_video, item->info.has_audio);
  if (!released) {
    view->show_drop(target, length);
    return;
  }
  view->show_drop(std::nullopt, 0);
  if (target) place(id, target->track, target->start);
}

// ---- Clip window ----------------------------------------------------------

void App::open_clip(int id) {
  const mpp::MediaItem* item = project().item(id);
  if (!item) return;
  clip_stop();
  selected_item_ = id;
  clip_item_ = id;
  clip_pos_ = item->in;
  focus_ = Focus::Clip;
  clip_window_->set_visible(true);
  move_window_to_front(clip_window_);
  bin_stale_ = true;
}

std::int64_t App::clip_length() const {
  const mpp::MediaItem* item = project().item(clip_item_);
  if (!item) return 1;
  return mpp::source_length(item->info, project().settings.rate)
      .value_or(std::max(item->out, project().settings.still_frames));
}

void App::clip_seek(std::int64_t position) {
  if (!project().item(clip_item_)) return;
  clip_stop();
  clip_pos_ = std::clamp<std::int64_t>(position, 0, clip_length() - 1);
}

void App::clip_step(int frames) { clip_seek(clip_pos_ + frames); }

void App::clip_toggle_play() {
  const mpp::MediaItem* item = project().item(clip_item_);
  if (!item) return;
  if (clip_playing_) {
    clip_stop();
    return;
  }
  // Play to the out mark, or to the end if already past it; from the in
  // mark if sitting at the end.
  const std::int64_t length = clip_length();
  play_stop_ = clip_pos_ < item->out ? item->out : length;
  if (clip_pos_ >= play_stop_ - 1) {
    clip_pos_ = item->in;
    play_stop_ = item->out;
  }
  play_from_ = clip_pos_;
  play_clock_ = 0;
  play_last_time_ = -1;
  clip_playing_ = true;
}

void App::clip_stop() {
  clip_playing_ = false;
  play_last_time_ = -1;
}

void App::advance_playback(double seconds) {
  if (!clip_playing_) return;
  play_clock_ += seconds;
  const mpp::Rate rate = project().settings.rate;
  const std::int64_t position = play_from_ + rate.frame_at(play_clock_);
  if (position >= play_stop_) {
    clip_pos_ = play_stop_ - 1;
    clip_stop();
  } else {
    clip_pos_ = position;
  }
}

void App::tick_playback(double now) {
  if (!clip_playing_) return;
  if (play_last_time_ >= 0) advance_playback(now - play_last_time_);
  play_last_time_ = now;
}

void App::clip_mark_in() {
  const mpp::MediaItem* item = project().item(clip_item_);
  if (!item) return;
  // A new in at or past the out mark moves the out to the end.
  std::int64_t out = item->out;
  if (clip_pos_ >= out) out = clip_length();
  run(std::make_unique<mpp::SetItemMarksCommand>(item->id, clip_pos_, out));
}

void App::clip_mark_out() {
  const mpp::MediaItem* item = project().item(clip_item_);
  if (!item) return;
  // The out mark is exclusive: marking out here keeps this frame.
  std::int64_t in = item->in;
  if (clip_pos_ < in) in = 0;
  run(std::make_unique<mpp::SetItemMarksCommand>(item->id, in, clip_pos_ + 1));
}

void App::clip_go_to_in() {
  if (const mpp::MediaItem* item = project().item(clip_item_)) clip_seek(item->in);
}

void App::clip_go_to_out() {
  if (const mpp::MediaItem* item = project().item(clip_item_)) clip_seek(item->out - 1);
}

// ---- Construction window --------------------------------------------------

bool App::place(int item_id, mpp::Track track, std::int64_t start) {
  const mpp::MediaItem* item = project().item(item_id);
  if (!item) return false;
  const std::string name = item->name;
  const int first_new = project().next_id;
  if (!run(std::make_unique<mpp::PlaceClipCommand>(item_id, track, start, item->in, item->out))) {
    return false;
  }
  selected_clips_.clear();
  for (const mpp::Clip& c : project().clips) {
    if (c.id >= first_new) selected_clips_.push_back(c.id);
  }
  focus_ = Focus::Timeline;
  message_ = "Placed " + name + " on " + mpp::track_name(track);
  return true;
}

bool App::place_at_edit_line() {
  const int id = clip_item_ ? clip_item_ : selected_item_;
  const mpp::MediaItem* item = project().item(id);
  if (!item) return false;
  // The first track the clip fits on: Video A, then B; sound only goes on
  // the audio tracks.
  std::vector<mpp::Track> tracks;
  if (item->info.has_video) {
    tracks = {mpp::Track::VideoA, mpp::Track::VideoB};
  } else {
    tracks = {mpp::Track::AudioA, mpp::Track::AudioB, mpp::Track::AudioC};
  }
  const std::int64_t length = item->out - item->in;
  for (mpp::Track t : tracks) {
    mpp::Project trial = project();
    try {
      mpp::PlaceClipCommand(id, t, edit_line_, item->in, item->out).execute(trial);
    } catch (const std::exception&) {
      continue;
    }
    if (!place(id, t, edit_line_)) return false;
    set_edit_line(edit_line_ + length);
    return true;
  }
  message_ = "There's no room for " + item->name + " at the edit line";
  return false;
}

void App::set_edit_line(std::int64_t frame) {
  edit_line_ = std::max<std::int64_t>(0, frame);
  construction_->view()->reveal(edit_line_);
}

void App::jump_to_edit(int direction) {
  std::vector<std::int64_t> points = mpp::snap_points(project(), 0, {});
  std::sort(points.begin(), points.end());
  if (direction > 0) {
    for (std::int64_t p : points) {
      if (p > edit_line_) return set_edit_line(p);
    }
  } else {
    for (auto it = points.rbegin(); it != points.rend(); ++it) {
      if (*it < edit_line_) return set_edit_line(*it);
    }
  }
}

void App::select_clips(std::vector<int> clips) {
  selected_clips_ = std::move(clips);
  focus_ = Focus::Timeline;
}

void App::move_clip(int clip, mpp::Track track, std::int64_t start) {
  run(std::make_unique<mpp::MoveClipCommand>(clip, track, start));
}

void App::trim_clip(int clip, std::int64_t start, std::int64_t in, std::int64_t out) {
  run(std::make_unique<mpp::TrimClipCommand>(clip, start, in, out));
}

void App::razor(int clip, std::int64_t frame) {
  if (run(std::make_unique<mpp::RazorCommand>(clip, frame))) selected_clips_.clear();
}

void App::set_work_area(std::int64_t in, std::int64_t out) {
  run(std::make_unique<mpp::SetWorkAreaCommand>(in, out));
}

void App::set_tool(TimelineTool tool) {
  tool_ = tool;
  // Now rather than on the next draw, so a click right after uses it.
  construction_->view()->set_state(project(), edit_line_, selected_clips_, tool_);
}

void App::zoom(int steps) { construction_->view()->zoom(steps); }

// ---- Settings -------------------------------------------------------------

void App::open_settings() {
  if (settings_) return;
  settings_ = new SettingsWindow(this, project().settings,
                                 [this](const mpp::ProjectSettings& s) { apply_settings(s); });
  perform_layout();
  layout_widgets();
  settings_->set_position((m_size - settings_->size()) / 2);
  move_window_to_front(settings_);
}

void App::apply_settings(const mpp::ProjectSettings& settings) {
  settings_ = nullptr;  // the window closed itself
  if (settings == project().settings) return;
  const double scale = settings.rate.fps() / project().settings.rate.fps();
  if (run(std::make_unique<mpp::SetSettingsCommand>(settings))) {
    clip_pos_ = std::clamp<std::int64_t>(static_cast<std::int64_t>(clip_pos_ * scale), 0,
                                         clip_length() - 1);
    edit_line_ = static_cast<std::int64_t>(edit_line_ * scale);
    message_ = "Project is " + std::to_string(settings.width) + " x " +
               std::to_string(settings.height) + ", " + mpp::rate_name(settings.rate);
  }
}

// ---- Drawing --------------------------------------------------------------

mpp::Image App::item_thumbnail(int id, std::int64_t position) {
  const mpp::MediaItem* item = project().item(id);
  if (!item || !item->info.has_video) return {};
  try {
    const mpp::Image frame = cache_.frame(
        item->path,
        mpp::source_frame(item->info, project().settings.rate, static_cast<double>(position)));
    const mpp::Point size = mpp::fitted_size(frame.width(), frame.height(), 48, 36);
    return mpp::resized(frame, size.x, size.y);
  } catch (const std::exception&) {
    return {};
  }
}

mpp::Image App::thumbnail(int id) {
  const mpp::MediaItem* item = project().item(id);
  return item ? item_thumbnail(id, item->in) : mpp::Image();
}

void App::refresh() {
  if (clip_item_ && !project().item(clip_item_)) {
    clip_stop();
    clip_item_ = 0;
  }
  if (selected_item_ && !project().item(selected_item_)) selected_item_ = 0;
  selected_clips_.erase(std::remove_if(selected_clips_.begin(), selected_clips_.end(),
                                       [this](int id) { return !project().clip(id); }),
                        selected_clips_.end());
  if (settings_ && !settings_->parent()) settings_ = nullptr;  // cancelled
  if (bin_stale_) {
    bin_stale_ = false;
    project_window_->refresh(project(), selected_item_);
    perform_layout();
    layout_widgets();
  }
  show_clip();
  show_timeline();
}

void App::show_clip() {
  ClipWindowState state;
  const mpp::MediaItem* item = project().item(clip_item_);
  state.rate = project().settings.rate;
  mpp::Image frame;
  if (item) {
    state.has_item = true;
    state.name = item->name;
    state.position = clip_pos_;
    state.in = item->in;
    state.out = item->out;
    state.length = clip_length();
    state.playing = clip_playing_;
    state.still = item->info.is_still();
    if (clip_shown_item_ != item->id || clip_shown_pos_ != clip_pos_) {
      clip_shown_item_ = item->id;
      clip_shown_pos_ = clip_pos_;
      try {
        frame = cache_.frame(
            item->path, mpp::source_frame(item->info, state.rate, static_cast<double>(clip_pos_)));
      } catch (const std::exception& e) {
        message_ = e.what();
      }
    }
  }
  clip_window_->show(state, frame);
}

void App::show_timeline() {
  const mpp::Project& p = project();
  const mpp::Rate rate = p.settings.rate;
  construction_->view()->set_state(p, edit_line_, selected_clips_, tool_);

  std::string info = "Program " + mpp::format_timecode(p.duration(), rate);
  if (p.work_out > p.work_in) {
    info += "    Work area " + mpp::format_timecode(p.work_in, rate) + " - " +
            mpp::format_timecode(p.work_out, rate);
  }
  if (!selected_clips_.empty()) {
    info += "    " + std::to_string(selected_clips_.size()) + " selected";
  }
  construction_->show(tool_, mpp::format_timecode(edit_line_, rate), info);

  if (preview_shown_frame_ != edit_line_ || preview_shown_changes_ != changes_) {
    preview_shown_frame_ = edit_line_;
    preview_shown_changes_ = changes_;
    preview_->view()->set_frame(mpp::render_frame(p, cache_, edit_line_));
    preview_->set_caption_text(mpp::format_timecode(edit_line_, rate) + "   " +
                               std::to_string(p.settings.width) + " x " +
                               std::to_string(p.settings.height));
  }
}

// ---- Events ---------------------------------------------------------------

bool App::keyboard_event(int key, int scancode, int action, int modifiers) {
  if (nanogui::Screen::keyboard_event(key, scancode, action, modifiers)) return true;
  if (action != GLFW_PRESS && action != GLFW_REPEAT) return false;
  if (key == GLFW_KEY_ESCAPE && menu_->open_index() >= 0) {
    menu_->close();
    return true;
  }

  if (modifiers & (GLFW_MOD_SUPER | GLFW_MOD_CONTROL)) {
    switch (key) {
      case GLFW_KEY_N: confirm_discard([this] { new_project(); }); return true;
      case GLFW_KEY_O: open_dialog(); return true;
      case GLFW_KEY_S:
        if (modifiers & GLFW_MOD_SHIFT) save_as_dialog();
        else save();
        return true;
      case GLFW_KEY_I: import_dialog(); return true;
      case GLFW_KEY_Z: toggle_undo(); return true;
      case GLFW_KEY_A: select_all_clips(); return true;
      case GLFW_KEY_Q: confirm_discard([this] { set_visible(false); }); return true;
      default: return false;
    }
  }

  const bool timeline = focus_ == Focus::Timeline;
  switch (key) {
    case GLFW_KEY_DELETE:
    case GLFW_KEY_BACKSPACE: clear_selected(); return true;
    case GLFW_KEY_ENTER:
      if (focus_ == Focus::Project) open_clip(selected_item_);
      return true;
    case GLFW_KEY_SPACE:
      if (!timeline) clip_toggle_play();
      return true;
    case GLFW_KEY_I: clip_mark_in(); return true;
    case GLFW_KEY_O: clip_mark_out(); return true;
    case GLFW_KEY_P: place_at_edit_line(); return true;
    case GLFW_KEY_V: set_tool(TimelineTool::Select); return true;
    case GLFW_KEY_C: set_tool(TimelineTool::Razor); return true;
    case GLFW_KEY_EQUAL: zoom(1); return true;
    case GLFW_KEY_MINUS: zoom(-1); return true;
    case GLFW_KEY_LEFT:
      if (timeline) set_edit_line(edit_line_ - 1);
      else clip_step(-1);
      return true;
    case GLFW_KEY_RIGHT:
      if (timeline) set_edit_line(edit_line_ + 1);
      else clip_step(1);
      return true;
    case GLFW_KEY_UP: jump_to_edit(-1); return true;
    case GLFW_KEY_DOWN: jump_to_edit(1); return true;
    case GLFW_KEY_HOME:
      if (timeline) set_edit_line(0);
      else clip_go_to_in();
      return true;
    case GLFW_KEY_END:
      if (timeline) set_edit_line(project().duration());
      else clip_go_to_out();
      return true;
    default: return false;
  }
}

bool App::mouse_button_event(const nanogui::Vector2i& p, int button, bool down,
                             int modifiers) {
  // A click outside an open menu closes it and goes no further.
  if (down && menu_->open_index() >= 0 && !menu_->covers(p)) {
    menu_->close();
    return true;
  }
  return nanogui::Screen::mouse_button_event(p, button, down, modifiers);
}

bool App::drop_event(const std::vector<std::string>& filenames) {
  if (filenames.size() == 1 && mpp::is_project_path(filenames.front())) {
    const std::string path = filenames.front();
    confirm_discard([this, path] { open(path); });
    return true;
  }
  for (const std::string& path : filenames) import(path);
  return !filenames.empty();
}

bool App::resize_event(const nanogui::Vector2i& size) {
  nanogui::Screen::resize_event(size);
  layout_widgets();
  return true;
}

void App::draw_all() {
  tick_playback(glfwGetTime());
  refresh();
  status_->set_caption(message_.empty()
                           ? std::to_string(project().settings.width) + " x " +
                                 std::to_string(project().settings.height) + "   " +
                                 mpp::rate_name(project().settings.rate) + "   " +
                                 std::to_string(project().settings.audio_bits) + "-bit " +
                                 std::to_string(project().settings.audio_rate) + " Hz"
                           : message_);
  nanogui::Screen::draw_all();
}

void App::capture(const std::string& path) {
  capture_path_ = path;
  redraw();
  draw_all();
  capture_path_.clear();
}

void App::draw_teardown() {
  if (!capture_path_.empty()) {
    int width = 0, height = 0;
    glfwGetFramebufferSize(glfw_window(), &width, &height);
    mpp::Image frame(width, height);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, frame.data());
    // OpenGL rows run bottom to top; make it opaque like the window.
    mpp::Image upright = frame.flipped_vertical();
    for (std::size_t i = 3; i < upright.size_bytes(); i += 4) upright.data()[i] = 255;
    mpp::save_png(upright, capture_path_);
  }
  nanogui::Screen::draw_teardown();
}

// NanoGUI moves every cursor position by (-1, -2) before using it; these
// add it back so a simulated event lands on the exact pixel asked for.
void App::simulate_click(nanogui::Vector2i at, int modifiers) {
  cursor_pos_callback_event(at.x() + 1, at.y() + 2);
  mouse_button_callback_event(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, modifiers);
  mouse_button_callback_event(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, modifiers);
}

void App::simulate_drag(nanogui::Vector2i from, nanogui::Vector2i to, int modifiers) {
  cursor_pos_callback_event(from.x() + 1, from.y() + 2);
  mouse_button_callback_event(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, modifiers);
  for (int i = 1; i <= 8; ++i) {
    cursor_pos_callback_event(from.x() + 1 + (to.x() - from.x()) * i / 8.0,
                              from.y() + 2 + (to.y() - from.y()) * i / 8.0);
  }
  mouse_button_callback_event(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, modifiers);
}

void App::layout_widgets() {
  // Fixed sizes, so perform_layout() keeps them.
  menu_->set_position({0, 0});
  menu_->set_fixed_size({m_size.x(), MenuBar::kHeight});
  menu_->set_size(menu_->fixed_size());
  status_->set_position({8, m_size.y() - kStatusHeight + 4});
  status_->set_fixed_size({m_size.x() - 16, kStatusHeight - 4});
  status_->set_size(status_->fixed_size());
  // The Construction window spans the bottom of the screen.
  const int width = m_size.x() - 2 * kMargin;
  if (construction_->fixed_width() != width) {
    construction_->set_fixed_width(width);
    construction_->fit_width(width);
    perform_layout();
  }
  construction_->set_position({kMargin, m_size.y() - kStatusHeight - construction_->height() - 4});
}
