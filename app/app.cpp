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
#include "filters_window.h"
#include "frame_view.h"
#include "key_window.h"
#include "make_windows.h"
#include "motion_window.h"
#include "mpp/audio_mixer.h"
#include "mpp/compositing.h"
#include "mpp/edl.h"
#include "mpp/filmstrip.h"
#include "mpp/image_sequence.h"
#include "mpp/bin_commands.h"
#include "mpp/image_io.h"
#include "mpp/project_file.h"
#include "mpp/renderer.h"
#include "mpp/resample.h"
#include "mpp/time_scale.h"
#include "mpp/timeline_commands.h"
#include "project_window.h"
#include "sequence_window.h"
#include "settings_window.h"
#include "title_window.h"
#include "timeline_windows.h"
#include "transition_windows.h"

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
  bin.open = [this](int id) { open_item(id); };
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

  PreviewActions preview_actions;
  preview_actions.play = [this] { preview(); };
  preview_actions.stop = [this] { stop(); };
  preview_ = new PreviewWindow(this, std::move(preview_actions));

  ConstructionActions timeline;
  timeline.set_edit_line = [this](std::int64_t f) { set_edit_line(f); };
  timeline.select = [this](std::vector<int> ids) { select(std::move(ids)); };
  timeline.move = [this](int c, mpp::Track t, std::int64_t s) { move_clip(c, t, s); };
  timeline.trim = [this](int c, std::int64_t s, std::int64_t i, std::int64_t o) {
    trim_clip(c, s, i, o);
  };
  timeline.razor = [this](int c, std::int64_t f) { razor(c, f); };
  timeline.set_work_area = [this](std::int64_t i, std::int64_t o) { set_work_area(i, o); };
  timeline.set_transition_span = [this](int id, std::int64_t s, std::int64_t l) {
    set_transition_span(id, s, l);
  };
  timeline.edit_transition = [this](int id) { edit_transition(id); };
  timeline.open_clip = [this](int id) { open_timeline_clip(id); };
  timeline.set_fade = [this](int id, std::vector<mpp::FadePoint> fade) {
    set_fade(id, std::move(fade));
  };
  timeline.focus = [this] { focus_ = Focus::Timeline; };
  timeline.thumbnail = [this](int item, std::int64_t pos) { return item_thumbnail(item, pos); };
  ConstructionToolbarActions toolbar;
  toolbar.set_tool = [this](TimelineTool t) { set_tool(t); };
  toolbar.zoom = [this](int steps) { zoom(steps); };
  construction_ = new ConstructionWindow(this, std::move(timeline), std::move(toolbar));
  transitions_ = new TransitionsWindow(
      this, [this](const std::string& kind, nanogui::Vector2i at, bool released) {
        drag_transition(kind, at, released);
      });

  SequenceWindowActions sequence;
  sequence.set = [this](std::vector<int> items) { set_sequence(std::move(items)); };
  sequence.add_selected = [this] { add_to_sequence(selected_item_); };
  sequence.place = [this](const std::string& t, int overlap) { place_sequence(t, overlap); };
  sequence.thumbnail = [this](int id) { return thumbnail(id); };
  sequence_ = new SequenceWindow(this, std::move(sequence));
  sequence_->set_visible(false);

  status_ = new nanogui::Label(this, "");
  const int top = MenuBar::kHeight + kMargin;
  project_window_->set_position({kMargin, top});
  clip_window_->set_position({440, top});
  preview_->set_position({790, top});
  transitions_->set_position({1150, top});
  sequence_->set_position({1080, top + 90});
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
        {"Import Sequence...", "", [this] { import_sequence_dialog(); }},
        {"New Title", "Cmd+T", [this] { new_title(); }},
        {"", "", nullptr},
        {"Quit", "Cmd+Q", [this] { confirm_discard([this] { set_visible(false); }); }},
    };
  }});
  menus.push_back({"Edit", [this] {
    const std::string undo = editor_.can_redo()   ? "Redo " + editor_.redo_name()
                             : editor_.can_undo() ? "Undo " + editor_.undo_name()
                                                  : "Undo";
    const bool clips = focus_ == Focus::Timeline && !selection_.empty();
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
    const int transition =
        selection_.size() == 1 && project().transition(selection_[0]) ? selection_[0] : 0;
    const mpp::Clip* chosen = selection_.size() == 1 ? project().clip(selection_[0]) : nullptr;
    const int super = chosen && chosen->track == mpp::Track::Superimpose ? chosen->id : 0;
    const int chosen_id = chosen ? chosen->id : 0;
    return std::vector<MenuItem>{
        {"Open in Clip Window", "Return", [this] { open_clip(selected_item_); },
         selected_item_ != 0},
        {"Place at Edit Line", "P", [this] { place_at_edit_line(); }, open},
        {"Add to Sequence", "", [this] { add_to_sequence(selected_item_); }, selected_item_ != 0},
        {"Edit Title...", "", [this] { edit_title(selected_item_); },
         project().item(selected_item_) && project().item(selected_item_)->title},
        {"", "", nullptr},
        {"Play / Stop", "Space", [this] { clip_toggle_play(); }, open},
        {"Mark In", "I", [this] { clip_mark_in(); }, open},
        {"Mark Out", "O", [this] { clip_mark_out(); }, open},
        {"Go to In", "Home", [this] { clip_go_to_in(); }, open},
        {"Go to Out", "End", [this] { clip_go_to_out(); }, open},
        {"", "", nullptr},
        {"Transition Settings...", "", [this, transition] { edit_transition(transition); },
         transition != 0},
        {"Transparency...", "", [this, super] { edit_key(super); }, super != 0},
        {"Filters...", "Cmd+F", [this, chosen_id] { edit_filters(chosen_id); }, chosen_id != 0},
        {"Motion...", "", [this, chosen_id] { edit_motion(chosen_id); },
         chosen && mpp::is_video_track(chosen->track)},
        {"Speed...", "", [this, chosen_id] { edit_speed(chosen_id); }, chosen_id != 0},
    };
  }});
  menus.push_back({"Make", [this] {
    const bool program = project().duration() > 0;
    return std::vector<MenuItem>{
        {"Preview", "Return", [this] { preview(); }, program},
        {"Stop", "Esc", [this] { stop(); },
         rendering() || exporting() || playing_ != Playing::None},
        {"", "", nullptr},
        {"Movie...", "Cmd+M", [this] { open_export_dialog(); }, program && !exporting()},
        {"Frame...", "", [this] { export_frame_dialog(); }},
        {"Filmstrip...", "", [this] { export_filmstrip_dialog(); }, program},
        {"EDL...", "", [this] { export_edl_dialog(); }, program},
        {"", "", nullptr},
        {"Print to Video...", "", [this] { print_to_video(); }, program},
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
        {"Transitions", "", show(transitions_), true, transitions_->visible()},
        {"Sequence", "", show(sequence_), true, sequence_->visible()},
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
  stop();
  rendered_.reset();
  rendered_sound_.reset();
  editor_ = mpp::Editor(std::move(project));
  selected_item_ = 0;
  selection_.clear();
  clip_item_ = 0;
  clip_pos_ = 0;
  clip_shown_item_ = -1;
  edit_line_ = 0;
  construction_->view()->scroll_to_start();
  construction_->view()->forget_thumbnails();  // ids repeat between projects
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
    // A PNG may be a Filmstrip, which comes in as a clip.
    std::optional<mpp::FilmstripInfo> strip;
    if (mpp::is_image_path(path)) strip = mpp::read_filmstrip_info(cache_.frame(path, 0));
    item = strip ? mpp::make_filmstrip_item(path, *strip, project().settings)
                 : mpp::make_item(path, cache_.info(path), project().settings);
  } catch (const std::exception& e) {
    message_ = e.what();
    return false;
  }
  if (!run(std::make_unique<mpp::AddItemCommand>(item))) return false;
  selected_item_ = project().bin.back().id;
  message_ = "Imported " + item.name;
  return true;
}

bool App::import_sequence(const std::string& picked) {
  const auto sequence = mpp::find_image_sequence(picked);
  if (!sequence) {
    message_ = mpp::file_name(picked) + " isn't one of a run of numbered images";
    return false;
  }
  mpp::MediaItem item;
  try {
    item = mpp::make_sequence_item(*sequence, cache_.info(sequence->path(0)), project().settings);
  } catch (const std::exception& e) {
    message_ = e.what();
    return false;
  }
  if (!run(std::make_unique<mpp::AddItemCommand>(item))) return false;
  selected_item_ = project().bin.back().id;
  message_ = "Imported " + item.name;
  return true;
}

void App::import_sequence_dialog() {
  const std::vector<std::string> paths = nanogui::file_dialog(
      this, nanogui::FileDialogType::Open,
      {{"png", "PNG"}, {"jpg", "JPEG"}, {"bmp", "BMP"}, {"tga", "Targa"}, {"gif", "GIF"}});
  if (!paths.empty()) import_sequence(paths.front());
}

bool App::export_edl(const std::string& path) {
  std::string target = path;
  if (target.size() < 4 || target.compare(target.size() - 4, 4, ".edl") != 0) target += ".edl";
  try {
    const std::string stem = doc_name_.substr(0, doc_name_.rfind('.'));
    mpp::save_edl(project(), stem.empty() ? doc_name_ : stem, target);
  } catch (const std::exception& e) {
    message_ = e.what();
    return false;
  }
  message_ = "Saved the EDL as " + target;
  return true;
}

void App::export_edl_dialog() {
  const std::vector<std::string> paths =
      nanogui::file_dialog(this, nanogui::FileDialogType::Save, {{"edl", "Edit decision list"}});
  if (!paths.empty()) export_edl(paths.front());
}

bool App::export_filmstrip(const std::string& path) {
  const auto [first, last] = make_range(false);
  std::string target = path;
  if (target.size() < 4 || target.compare(target.size() - 4, 4, ".png") != 0) target += ".png";
  try {
    mpp::export_filmstrip(project(), cache_, target, first, last);
  } catch (const std::exception& e) {
    message_ = e.what();
    return false;
  }
  message_ = "Saved " + std::to_string(last - first) + " frames as the Filmstrip " + target;
  return true;
}

void App::export_filmstrip_dialog() {
  const std::vector<std::string> paths =
      nanogui::file_dialog(this, nanogui::FileDialogType::Save, {{"png", "Filmstrip (PNG)"}});
  if (!paths.empty()) export_filmstrip(paths.front());
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
  if (focus_ == Focus::Timeline && !selection_.empty()) {
    if (run(std::make_unique<mpp::ClearCommand>(selection_))) selection_.clear();
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
  selection_.clear();
  for (const mpp::Clip& c : project().clips) selection_.push_back(c.id);
  for (const mpp::Transition& t : project().transitions) selection_.push_back(t.id);
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
  // Over the Sequence window, it goes into the sequence there.
  if (sequence_->visible()) {
    const int row = sequence_->drop_row(at);
    if (row >= 0) {
      view->show_drop(std::nullopt, 0);
      if (released) add_to_sequence(id, row);
      return;
    }
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
  if (playing_ == Playing::Clip) {
    stop_playing();
    return;
  }
  stop_playing();
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
  playing_ = Playing::Clip;
  // The item's own sound, from here, timing the pictures.
  sound_clock_ = false;
  if (item->info.has_audio && !test_clock_) {
    try {
      const mpp::ProjectSettings& s = project().settings;
      sound_clock_ = audio_.play(cache_.audio(item->path, s.audio_rate),
                                 static_cast<std::size_t>(mpp::frame_to_sample(s, clip_pos_)));
    } catch (const std::exception& e) {
      message_ = e.what();
    }
  }
}

void App::clip_stop() {
  if (playing_ == Playing::Clip) stop_playing();
}

void App::stop_playing() {
  audio_.stop();
  sound_clock_ = false;
  playing_ = Playing::None;
  play_last_time_ = -1;
  program_index_ = -1;
  if (fullscreen_) end_print_ = true;
}

void App::set_test_clock() {
  test_clock_ = true;
  audio_.set_enabled(false);
}

void App::advance_playback(double seconds) {
  if (playing_ == Playing::None) return;
  play_clock_ += seconds;
  follow_clock();
}

void App::follow_clock() {
  const mpp::Rate rate = project().settings.rate;
  const std::int64_t elapsed = rate.frame_at(play_clock_);
  if (playing_ == Playing::Clip) {
    const std::int64_t position = play_from_ + elapsed;
    if (position >= play_stop_) {
      clip_pos_ = play_stop_ - 1;
      stop_playing();
    } else {
      clip_pos_ = position;
    }
  } else if (playing_ == Playing::Program && rendered_) {
    const auto count = static_cast<std::int64_t>(rendered_->frames.size());
    program_index_ = std::min(elapsed, count - 1);
    edit_line_ = rendered_->first + program_index_;
    if (elapsed >= count) stop_playing();
  }
}

void App::tick_playback(double now) {
  if (playing_ == Playing::None || test_clock_) return;
  if (sound_clock_ && audio_.playing()) {
    play_clock_ = audio_.position();
    follow_clock();
    return;
  }
  if (play_last_time_ >= 0) advance_playback(now - play_last_time_);
  play_last_time_ = now;
}

std::pair<std::int64_t, std::int64_t> App::make_range(bool whole_program) const {
  const mpp::Project& p = project();
  if (!whole_program && p.work_out > p.work_in) return {p.work_in, p.work_out};
  return {0, p.duration()};
}

void App::preview() {
  if (playing_ == Playing::Program) {
    stop_playing();
    return;
  }
  const auto [first, last] = make_range(false);
  if (last <= first) {
    message_ = "There's nothing to preview: place some clips first";
    return;
  }
  stop_playing();
  // Play the last render if nothing has changed since; otherwise render.
  if (rendered_ && rendered_changes_ == changes_ && rendered_->first == first &&
      rendered_->first + static_cast<std::int64_t>(rendered_->frames.size()) == last) {
    start_program(print_after_render_);
    print_after_render_ = false;
    return;
  }
  if (render_job_ && render_job_->first() == first && render_job_->last() == last &&
      render_changes_ == changes_) {
    return;  // already rendering this
  }
  // A preview lives in memory, frame by frame; keep it under about 2 GB.
  const mpp::ProjectSettings& s = project().settings;
  const double bytes = static_cast<double>(last - first) * s.width * s.height * 4;
  if (bytes > 2e9) {
    message_ = "The work area is too long to preview at this frame size; shorten it";
    print_after_render_ = false;
    return;
  }
  render_job_ = std::make_unique<mpp::RenderJob>(project(), first, last);
  render_changes_ = changes_;
  message_ = "Rendering " + std::to_string(last - first) + " frames (Esc cancels)";
}

void App::start_program(bool full_screen) {
  if (!rendered_ || rendered_->frames.empty()) return;
  playing_ = Playing::Program;
  play_clock_ = 0;
  play_last_time_ = -1;
  program_index_ = 0;
  edit_line_ = rendered_->first;
  sound_clock_ = !test_clock_ && audio_.play(rendered_sound_, 0);
  if (full_screen && !fullscreen_) {
    fullscreen_ = new FullScreenView(this, [this] { end_print_ = true; });
    fullscreen_->set_frame(rendered_->frames.front());
  }
  message_.clear();
}

void App::finish_render() {
  std::unique_ptr<mpp::RenderJob> job = std::move(render_job_);
  try {
    mpp::RenderedProgram program = job->take();
    rendered_sound_ = std::make_shared<const mpp::AudioBuffer>(std::move(program.audio));
    rendered_ = std::make_shared<const mpp::RenderedProgram>(std::move(program));
    rendered_changes_ = render_changes_;
  } catch (const std::exception& e) {
    message_ = e.what();
    print_after_render_ = false;
    return;
  }
  start_program(print_after_render_);
  print_after_render_ = false;
}

double App::render_progress() const { return render_job_ ? render_job_->progress() : 1.0; }

std::int64_t App::program_frame() const {
  return playing_ == Playing::Program ? program_index_ : -1;
}

void App::stop() {
  if (render_job_) {
    render_job_.reset();  // cancels and waits
    print_after_render_ = false;
    message_ = "Render cancelled";
  }
  if (export_job_) {
    export_job_->cancel();
    finish_export();
  }
  stop_playing();
}

bool App::export_movie(const std::string& path, mpp::MovieFormat format, bool whole_program) {
  if (export_job_) {
    message_ = "Already making a movie";
    return false;
  }
  const auto [first, last] = make_range(whole_program);
  if (last <= first) {
    message_ = "There's nothing to make a movie of";
    return false;
  }
  std::string target = path;
  const std::string ext = mpp::movie_extension(format);
  if (target.size() < ext.size() || target.compare(target.size() - ext.size(), ext.size(), ext) != 0) {
    target += ext;
  }
  export_job_ = std::make_unique<mpp::ExportJob>(project(), target, format, first, last);
  message_ = "Making " + target + " (Esc cancels)";
  return true;
}

double App::export_progress() const { return export_job_ ? export_job_->progress() : 1.0; }

void App::finish_export() {
  std::unique_ptr<mpp::ExportJob> job = std::move(export_job_);
  const std::string error = job->wait();
  message_ = error.empty() ? "Made " + job->path()
             : error == "cancelled" ? "Movie cancelled"
                                    : error;
}

void App::open_export_dialog() {
  if (export_window_) return;
  const mpp::Project& p = project();
  export_window_ = new ExportWindow(this, p.work_out > p.work_in,
                                    [this](mpp::MovieFormat format, bool whole) {
    const std::vector<std::string> paths = nanogui::file_dialog(
        this, nanogui::FileDialogType::Save,
        {{std::string(mpp::movie_extension(format) + 1), mpp::movie_format_name(format)}});
    if (!paths.empty()) export_movie(paths.front(), format, whole);
  });
  export_window_->set_closed_callback([this] { export_window_ = nullptr; });
  perform_layout();
  layout_widgets();
  export_window_->set_position((m_size - export_window_->size()) / 2);
  move_window_to_front(export_window_);
}

bool App::export_frame(const std::string& path) {
  std::string target = path;
  if (target.size() < 4 || target.compare(target.size() - 4, 4, ".png") != 0) target += ".png";
  try {
    mpp::save_png(mpp::render_frame(project(), cache_, edit_line_), target);
  } catch (const std::exception& e) {
    message_ = e.what();
    return false;
  }
  message_ = "Saved the frame at " + mpp::format_timecode(edit_line_, project().settings.rate) +
             " as " + target;
  return true;
}

void App::export_frame_dialog() {
  const std::vector<std::string> paths =
      nanogui::file_dialog(this, nanogui::FileDialogType::Save, {{"png", "PNG"}});
  if (!paths.empty()) export_frame(paths.front());
}

void App::print_to_video() {
  print_after_render_ = true;
  preview();
  if (playing_ == Playing::Program && !fullscreen_) {
    // preview() found a render and is playing it in the window; go full
    // screen from the start.
    stop_playing();
    start_program(true);
    print_after_render_ = false;
  }
}

void App::end_print() {
  end_print_ = false;
  if (!fullscreen_) return;
  if (playing_ == Playing::Program) stop_playing();
  remove_child(fullscreen_);
  fullscreen_ = nullptr;
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
  selection_.clear();
  for (const mpp::Clip& c : project().clips) {
    if (c.id >= first_new) selection_.push_back(c.id);
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

void App::select(std::vector<int> ids) {
  selection_ = std::move(ids);
  focus_ = Focus::Timeline;
}

void App::move_clip(int clip, mpp::Track track, std::int64_t start) {
  run(std::make_unique<mpp::MoveClipCommand>(clip, track, start));
}

void App::trim_clip(int clip, std::int64_t start, std::int64_t in, std::int64_t out) {
  run(std::make_unique<mpp::TrimClipCommand>(clip, start, in, out));
}

void App::razor(int clip, std::int64_t frame) {
  if (run(std::make_unique<mpp::RazorCommand>(clip, frame))) selection_.clear();
}

void App::set_work_area(std::int64_t in, std::int64_t out) {
  run(std::make_unique<mpp::SetWorkAreaCommand>(in, out));
}

void App::drag_transition(const std::string& kind, nanogui::Vector2i at, bool released) {
  ConstructionView* view = construction_->view();
  const auto frame = view->transition_drop_frame(at);
  if (!released) {
    if (frame) {
      const mpp::TransitionSpan span =
          mpp::suggest_transition_span(project(), *frame, project().settings.rate.nominal());
      view->show_transition_drop(span.start, span.length);
    } else {
      view->show_transition_drop(0, 0);
    }
    return;
  }
  view->show_transition_drop(0, 0);
  if (frame) add_transition(kind, *frame);
}

bool App::add_transition(const std::string& kind, std::int64_t frame) {
  // Fits the A/B overlap there, or lasts a second.
  const mpp::TransitionSpan span =
      mpp::suggest_transition_span(project(), frame, project().settings.rate.nominal());
  mpp::TransitionSettings settings;
  settings.b_to_a = span.b_to_a;
  const int id = project().next_id;
  if (!run(std::make_unique<mpp::AddTransitionCommand>(kind, span.start, span.length, settings))) {
    return false;
  }
  selection_ = {id};
  focus_ = Focus::Timeline;
  if (const mpp::TransitionDef* def = mpp::find_transition(kind)) {
    message_ = "Added " + def->name;
  }
  return true;
}

void App::set_transition_span(int id, std::int64_t start, std::int64_t length) {
  run(std::make_unique<mpp::SetTransitionSpanCommand>(id, start, length));
}

void App::edit_transition(int id) {
  const mpp::Transition* t = project().transition(id);
  const mpp::TransitionDef* def = t ? mpp::find_transition(t->kind) : nullptr;
  if (!def || transition_settings_) return;
  // Preview with the real pictures from the middle of the transition, or
  // the sample A and B when the tracks are empty there.
  const mpp::ProjectSettings& s = project().settings;
  const std::int64_t middle = t->start + t->length / 2;
  const int w = 160, h = std::max(1, 160 * s.height / s.width);
  mpp::Image a = project().clip_at(mpp::Track::VideoA, middle)
                     ? mpp::resized(mpp::track_picture(project(), cache_, mpp::Track::VideoA, middle), w, h)
                     : sample_a(w, h);
  mpp::Image b = project().clip_at(mpp::Track::VideoB, middle)
                     ? mpp::resized(mpp::track_picture(project(), cache_, mpp::Track::VideoB, middle), w, h)
                     : sample_b(w, h);
  transition_settings_ = new TransitionSettingsWindow(
      this, *def, t->settings, std::move(a), std::move(b),
      [this, id](const mpp::TransitionSettings& settings) {
        run(std::make_unique<mpp::SetTransitionSettingsCommand>(id, settings));
      });
  transition_settings_->set_closed_callback([this] { transition_settings_ = nullptr; });
  perform_layout();
  layout_widgets();
  transition_settings_->set_position((m_size - transition_settings_->size()) / 2);
  move_window_to_front(transition_settings_);
}

void App::set_fade(int clip, std::vector<mpp::FadePoint> fade) {
  run(std::make_unique<mpp::SetFadeCommand>(clip, std::move(fade)));
}

void App::open_timeline_clip(int id) {
  const mpp::Clip* clip = project().clip(id);
  if (!clip) return;
  if (clip->track == mpp::Track::Superimpose) {
    edit_key(id);
  } else {
    open_clip(clip->item);
  }
}

void App::edit_filters(int id) {
  const mpp::Clip* clip = project().clip(id);
  const mpp::MediaItem* item = clip ? project().item(clip->item) : nullptr;
  if (!item || filters_window_) return;
  // Pictures of the first and last frames, before filters, to preview on.
  mpp::Image first, last;
  const std::int64_t last_frame = clip->end() - 1;
  if (mpp::is_video_track(clip->track)) {
    try {
      first = mpp::clip_source(project(), cache_, *clip, clip->start);
      last = mpp::clip_source(project(), cache_, *clip, last_frame);
    } catch (const std::exception& e) {
      message_ = e.what();
      return;
    }
  }
  filters_window_ = new FiltersWindow(
      this, item->name, clip->filters, std::move(first), clip->start, std::move(last), last_frame,
      [this, id](const std::vector<mpp::FilterInstance>& filters) {
        run(std::make_unique<mpp::SetFiltersCommand>(id, filters));
      });
  filters_window_->set_closed_callback([this] { filters_window_ = nullptr; });
  perform_layout();
  layout_widgets();
  filters_window_->set_position((m_size - filters_window_->size()) / 2);
  move_window_to_front(filters_window_);
}

void App::edit_motion(int id) {
  const mpp::Clip* clip = project().clip(id);
  if (!clip || mpp::is_audio_track(clip->track) || motion_window_) return;
  mpp::Image picture;
  try {
    picture = mpp::clip_source(project(), cache_, *clip, clip->start);
  } catch (const std::exception& e) {
    message_ = e.what();
    return;
  }
  const mpp::ProjectSettings& s = project().settings;
  motion_window_ = new MotionWindow(this, clip->motion, std::move(picture), s.width, s.height,
                                    [this, id](const std::optional<mpp::Motion>& motion) {
    run(std::make_unique<mpp::SetMotionCommand>(id, motion));
  });
  motion_window_->set_closed_callback([this] { motion_window_ = nullptr; });
  perform_layout();
  layout_widgets();
  motion_window_->set_position((m_size - motion_window_->size()) / 2);
  move_window_to_front(motion_window_);
}

void App::edit_speed(int id) {
  const mpp::Clip* clip = project().clip(id);
  if (!clip || speed_window_) return;
  speed_window_ = new SpeedWindow(this, clip->speed, clip->out - clip->in, project().settings.rate,
                                  [this, id](int speed) {
    run(std::make_unique<mpp::SetSpeedCommand>(id, speed));
  });
  speed_window_->set_closed_callback([this] { speed_window_ = nullptr; });
  perform_layout();
  layout_widgets();
  speed_window_->set_position((m_size - speed_window_->size()) / 2);
  move_window_to_front(speed_window_);
}

void App::edit_key(int id) {
  const mpp::Clip* clip = project().clip(id);
  if (!clip || transparency_) return;
  // Preview at the edit line if it's over the clip, else at its start.
  const std::int64_t frame = clip->covers(edit_line_) ? edit_line_ : clip->start;
  mpp::Image picture, opaque;
  try {
    picture = mpp::clip_picture(project(), cache_, *clip, frame, true);
    opaque = mpp::clip_picture(project(), cache_, *clip, frame, false);
  } catch (const std::exception& e) {
    message_ = e.what();
    return;
  }
  transparency_ = new TransparencyWindow(
      this, clip->key, std::move(picture), std::move(opaque),
      mpp::render_beneath_super(project(), cache_, frame),
      [this, id](const mpp::TransparencyKey& key) {
        run(std::make_unique<mpp::SetKeyCommand>(id, key));
      });
  transparency_->set_closed_callback([this] { transparency_ = nullptr; });
  perform_layout();
  layout_widgets();
  transparency_->set_position((m_size - transparency_->size()) / 2);
  move_window_to_front(transparency_);
}

void App::open_item(int id) {
  const mpp::MediaItem* item = project().item(id);
  if (item && item->title) {
    selected_item_ = id;
    edit_title(id);
  } else {
    open_clip(id);
  }
}

void App::new_title() {
  if (title_window_) return;
  mpp::TitleSpec spec;
  mpp::TitleText words;
  words.text = "Title";
  spec.texts.push_back(words);
  const std::string name = "Title " + std::to_string(++titles_made_);
  const mpp::ProjectSettings& s = project().settings;
  title_window_ = new TitleWindow(this, spec, name, s.width, s.height,
                                  [this](const mpp::TitleSpec& title, const std::string& n) {
    if (run(std::make_unique<mpp::AddItemCommand>(mpp::make_title_item(title, n, project().settings)))) {
      selected_item_ = project().bin.back().id;
      message_ = "Made " + n;
    }
  });
  title_window_->set_closed_callback([this] { title_window_ = nullptr; });
  perform_layout();
  layout_widgets();
  title_window_->set_position((m_size - title_window_->size()) / 2);
  move_window_to_front(title_window_);
}

void App::edit_title(int id) {
  const mpp::MediaItem* item = project().item(id);
  if (!item || !item->title || title_window_) return;
  const mpp::ProjectSettings& s = project().settings;
  title_window_ = new TitleWindow(this, *item->title, item->name, s.width, s.height,
                                  [this, id](const mpp::TitleSpec& title, const std::string& n) {
    if (run(std::make_unique<mpp::SetTitleCommand>(id, title, n))) {
      construction_->view()->forget_thumbnails();
    }
  });
  title_window_->set_closed_callback([this] { title_window_ = nullptr; });
  perform_layout();
  layout_widgets();
  title_window_->set_position((m_size - title_window_->size()) / 2);
  move_window_to_front(title_window_);
}

void App::set_sequence(std::vector<int> items) {
  run(std::make_unique<mpp::SetSequenceCommand>(std::move(items)));
}

void App::add_to_sequence(int item, int row) {
  if (!project().item(item)) return;
  std::vector<int> items = project().sequence;
  const auto at = row < 0 || row > static_cast<int>(items.size()) ? items.end() : items.begin() + row;
  items.insert(at, item);
  if (run(std::make_unique<mpp::SetSequenceCommand>(std::move(items)))) {
    sequence_->set_visible(true);
    move_window_to_front(sequence_);
  }
}

bool App::place_sequence(const std::string& transition, int overlap) {
  const int first_new = project().next_id;
  if (!run(std::make_unique<mpp::BuildSequenceCommand>(project().sequence, edit_line_, transition,
                                                       overlap))) {
    return false;
  }
  selection_.clear();
  for (const mpp::Clip& c : project().clips) {
    if (c.id >= first_new) selection_.push_back(c.id);
  }
  for (const mpp::Transition& t : project().transitions) {
    if (t.id >= first_new) selection_.push_back(t.id);
  }
  message_ = "Placed the sequence";
  set_edit_line(project().duration());
  return true;
}

void App::set_tool(TimelineTool tool) {
  tool_ = tool;
  // Now rather than on the next draw, so a click right after uses it.
  construction_->view()->set_state(project(), edit_line_, selection_, tool_);
}

void App::zoom(int steps) { construction_->view()->zoom(steps); }

// ---- Settings -------------------------------------------------------------

void App::open_settings() {
  if (settings_) return;
  settings_ = new SettingsWindow(this, project().settings,
                                 [this](const mpp::ProjectSettings& s) { apply_settings(s); });
  settings_->set_closed_callback([this] { settings_ = nullptr; });
  perform_layout();
  layout_widgets();
  settings_->set_position((m_size - settings_->size()) / 2);
  move_window_to_front(settings_);
}

void App::apply_settings(const mpp::ProjectSettings& settings) {
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
    const mpp::Image frame =
        mpp::flattened(mpp::item_picture(project(), cache_, *item, static_cast<double>(position)));
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
  if (render_job_ && render_job_->finished()) finish_render();
  if (export_job_ && export_job_->finished()) finish_export();
  if (end_print_) end_print();
  if (clip_item_ && !project().item(clip_item_)) {
    clip_stop();
    clip_item_ = 0;
  }
  if (selected_item_ && !project().item(selected_item_)) selected_item_ = 0;
  selection_.erase(std::remove_if(selection_.begin(), selection_.end(),
                                  [this](int id) {
                                    return !project().clip(id) && !project().transition(id);
                                  }),
                   selection_.end());
  if (bin_stale_) {
    bin_stale_ = false;
    project_window_->refresh(project(), selected_item_);
    perform_layout();
    layout_widgets();
  }
  sequence_->refresh(project());
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
    state.playing = playing_ == Playing::Clip;
    state.still = item->info.is_still();
    if (clip_shown_item_ != item->id || clip_shown_pos_ != clip_pos_) {
      clip_shown_item_ = item->id;
      clip_shown_pos_ = clip_pos_;
      try {
        frame = mpp::item_picture(project(), cache_, *item, static_cast<double>(clip_pos_));
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
  construction_->view()->set_state(p, edit_line_, selection_, tool_);

  std::string info = "Program " + mpp::format_timecode(p.duration(), rate);
  if (p.work_out > p.work_in) {
    info += "    Work area " + mpp::format_timecode(p.work_in, rate) + " - " +
            mpp::format_timecode(p.work_out, rate);
  }
  if (!selection_.empty()) {
    info += "    " + std::to_string(selection_.size()) + " selected";
  }
  construction_->show(tool_, mpp::format_timecode(edit_line_, rate), info);

  // Playing shows the render; otherwise Preview renders the edit line.
  if (playing_ == Playing::Program && rendered_ && program_index_ >= 0) {
    const mpp::Image& frame = rendered_->frames[static_cast<std::size_t>(program_index_)];
    if (preview_shown_frame_ != edit_line_) {
      preview_->view()->set_frame(frame);
      if (fullscreen_) fullscreen_->set_frame(frame);
    }
    preview_shown_frame_ = edit_line_;
    preview_shown_changes_ = 0;  // redraw from the project when playback stops
  } else if (preview_shown_frame_ != edit_line_ || preview_shown_changes_ != changes_) {
    preview_shown_frame_ = edit_line_;
    preview_shown_changes_ = changes_;
    preview_->view()->set_frame(mpp::render_frame(p, cache_, edit_line_));
  }
  std::string caption = mpp::format_timecode(edit_line_, rate) + "   " +
                        std::to_string(p.settings.width) + " x " + std::to_string(p.settings.height);
  if (render_job_) {
    const auto done = static_cast<std::int64_t>(render_job_->progress() * (render_job_->last() - render_job_->first()));
    caption = "Rendering " + std::to_string(done) + " of " +
              std::to_string(render_job_->last() - render_job_->first()) + " frames";
  } else if (playing_ == Playing::Program) {
    caption += "   playing";
  }
  preview_->set_caption_text(caption);
  preview_->set_progress(render_job_ ? render_job_->progress() : export_job_ ? export_job_->progress() : -1.0);
  preview_->set_playing(playing_ == Playing::Program);
}

// ---- Events ---------------------------------------------------------------

bool App::keyboard_event(int key, int scancode, int action, int modifiers) {
  if (nanogui::Screen::keyboard_event(key, scancode, action, modifiers)) return true;
  if (action != GLFW_PRESS && action != GLFW_REPEAT) return false;
  if (key == GLFW_KEY_ESCAPE) {
    if (menu_->open_index() >= 0) menu_->close();
    else stop();
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
      case GLFW_KEY_M: open_export_dialog(); return true;
      case GLFW_KEY_T: new_title(); return true;
      case GLFW_KEY_Z: toggle_undo(); return true;
      case GLFW_KEY_A: select_all_clips(); return true;
      case GLFW_KEY_F:
        if (selection_.size() == 1) edit_filters(selection_[0]);
        return true;
      case GLFW_KEY_Q: confirm_discard([this] { set_visible(false); }); return true;
      default: return false;
    }
  }

  const bool timeline = focus_ == Focus::Timeline;
  switch (key) {
    case GLFW_KEY_DELETE:
    case GLFW_KEY_BACKSPACE: clear_selected(); return true;
    case GLFW_KEY_ENTER:
    case GLFW_KEY_KP_ENTER:
      if (focus_ == Focus::Project) open_clip(selected_item_);
      else preview();
      return true;
    case GLFW_KEY_SPACE:
      if (timeline) preview();
      else clip_toggle_play();
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
  if (fullscreen_) fullscreen_->fill_parent();
}
