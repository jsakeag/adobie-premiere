#include "app.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <utility>

#include <nanogui/label.h>
#include <nanogui/opengl.h>

#include "clip_window.h"
#include "menu_bar.h"
#include "mpp/bin_commands.h"
#include "mpp/image_io.h"
#include "mpp/resample.h"
#include "project_window.h"
#include "settings_window.h"

namespace {

constexpr int kStatusHeight = 24;

const std::vector<std::pair<std::string, std::string>> kImportTypes = {
    {"mov", "QuickTime movie"}, {"mp4", "MPEG-4 movie"}, {"m4v", "MPEG-4 movie"},
    {"avi", "AVI movie"},       {"mkv", "Matroska"},     {"png", "PNG"},
    {"jpg", "JPEG"},            {"jpeg", "JPEG"},        {"bmp", "BMP"},
    {"gif", "GIF"},             {"tga", "Targa"},        {"wav", "WAVE sound"},
    {"aif", "AIFF sound"},      {"aiff", "AIFF sound"},  {"mp3", "MP3 sound"},
    {"m4a", "AAC sound"}};

}  // namespace

App::App() : nanogui::Screen(nanogui::Vector2i(1280, 800), "Mock Premiere") {
  menu_ = new MenuBar(this, build_menus());

  ProjectWindowActions bin;
  bin.import = [this] { import_dialog(); };
  bin.select = [this](int id) {
    focus_ = Focus::Project;
    select_item(id);
  };
  bin.open = [this](int id) { open_clip(id); };
  bin.clear = [this] { clear_selected(); };
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

  status_ = new nanogui::Label(this, "");
  perform_layout();
  project_window_->set_position({10, MenuBar::kHeight + 10});
  clip_window_->set_position({450, MenuBar::kHeight + 10});
  layout_widgets();
  set_project(mpp::Project());
}

std::vector<Menu> App::build_menus() {
  std::vector<Menu> menus;
  menus.push_back({"File", [this] {
    return std::vector<MenuItem>{
        {"New Project", "Cmd+N", [this] { new_project(); }},
        {"", "", nullptr},
        {"Import...", "Cmd+I", [this] { import_dialog(); }},
        {"", "", nullptr},
        {"Quit", "Cmd+Q", [this] { set_visible(false); }},
    };
  }});
  menus.push_back({"Edit", [this] {
    const std::string undo = editor_.can_redo() ? "Redo " + editor_.redo_name()
                             : editor_.can_undo() ? "Undo " + editor_.undo_name()
                                                  : "Undo";
    return std::vector<MenuItem>{
        {undo, "Cmd+Z", [this] { toggle_undo(); },
         editor_.can_undo() || editor_.can_redo()},
        {"", "", nullptr},
        {"Clear", "Delete", [this] { clear_selected(); }, selected_item_ != 0},
    };
  }});
  menus.push_back({"Project", [this] {
    return std::vector<MenuItem>{
        {"Settings...", "", [this] { open_settings(); }},
    };
  }});
  menus.push_back({"Clip", [this] {
    const bool open = clip_item_ != 0;
    return std::vector<MenuItem>{
        {"Open in Clip Window", "Return", [this] { open_clip(selected_item_); },
         selected_item_ != 0},
        {"", "", nullptr},
        {"Play / Stop", "Space", [this] { clip_toggle_play(); }, open},
        {"Mark In", "I", [this] { clip_mark_in(); }, open},
        {"Mark Out", "O", [this] { clip_mark_out(); }, open},
        {"Go to In", "Home", [this] { clip_go_to_in(); }, open},
        {"Go to Out", "End", [this] { clip_go_to_out(); }, open},
    };
  }});
  menus.push_back({"Windows", [this] {
    return std::vector<MenuItem>{
        {"Project", "", [this] {
           project_window_->set_visible(true);
           move_window_to_front(project_window_);
         }, true, project_window_->visible()},
        {"Clip", "", [this] {
           clip_window_->set_visible(true);
           move_window_to_front(clip_window_);
         }, true, clip_window_->visible()},
    };
  }});
  return menus;
}

void App::new_project() {
  set_project(mpp::Project());
  message_ = "New project";
}

void App::set_project(mpp::Project project) {
  clip_stop();
  editor_ = mpp::Editor(std::move(project));
  selected_item_ = 0;
  clip_item_ = 0;
  clip_pos_ = 0;
  clip_shown_item_ = -1;
  message_.clear();
  bin_stale_ = true;
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

void App::toggle_undo() {
  if (editor_.toggle_undo()) {
    message_.clear();
    bin_stale_ = true;
    clip_shown_item_ = -1;
  }
}

void App::clear_selected() {
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

void App::select_item(int id) {
  selected_item_ = project().item(id) ? id : 0;
  bin_stale_ = true;
}

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

void App::open_settings() {
  if (settings_) return;
  settings_ = new SettingsWindow(this, project().settings,
                                 [this](const mpp::ProjectSettings& s) { apply_settings(s); });
  perform_layout();
  settings_->set_position((m_size - settings_->size()) / 2);
  move_window_to_front(settings_);
}

void App::apply_settings(const mpp::ProjectSettings& settings) {
  settings_ = nullptr;  // the window closed itself
  if (settings == project().settings) return;
  const double old_fps = project().settings.rate.fps();
  if (run(std::make_unique<mpp::SetSettingsCommand>(settings))) {
    clip_pos_ = static_cast<std::int64_t>(clip_pos_ * settings.rate.fps() / old_fps);
    clip_pos_ = std::clamp<std::int64_t>(clip_pos_, 0, clip_length() - 1);
    message_ = "Project is " + std::to_string(settings.width) + " x " +
               std::to_string(settings.height) + ", " + mpp::rate_name(settings.rate);
  }
}

bool App::run(std::unique_ptr<mpp::Command> command) {
  try {
    editor_.execute(std::move(command));
  } catch (const std::exception& e) {
    message_ = e.what();
    return false;
  }
  message_.clear();
  bin_stale_ = true;
  clip_shown_item_ = -1;
  return true;
}

mpp::Image App::thumbnail(int id) {
  const mpp::MediaItem* item = project().item(id);
  if (!item || !item->info.has_video) return {};
  try {
    const mpp::Image frame = cache_.frame(
        item->path, mpp::source_frame(item->info, project().settings.rate, item->in));
    const mpp::Point size = mpp::fitted_size(frame.width(), frame.height(), 48, 36);
    return mpp::resized(frame, size.x, size.y);
  } catch (const std::exception&) {
    return {};
  }
}

void App::refresh() {
  if (clip_item_ && !project().item(clip_item_)) {
    clip_stop();
    clip_item_ = 0;
  }
  if (settings_ && !settings_->parent()) settings_ = nullptr;  // cancelled
  if (bin_stale_) {
    bin_stale_ = false;
    project_window_->refresh(project(), selected_item_);
    perform_layout();
  }
  show_clip();
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
        frame = cache_.frame(item->path,
                             mpp::source_frame(item->info, state.rate, static_cast<double>(clip_pos_)));
      } catch (const std::exception& e) {
        message_ = e.what();
      }
    }
  }
  clip_window_->show(state, frame);
}

bool App::keyboard_event(int key, int scancode, int action, int modifiers) {
  if (nanogui::Screen::keyboard_event(key, scancode, action, modifiers)) return true;
  if (action != GLFW_PRESS && action != GLFW_REPEAT) return false;
  if (key == GLFW_KEY_ESCAPE && menu_->open_index() >= 0) {
    menu_->close();
    return true;
  }

  if (modifiers & (GLFW_MOD_SUPER | GLFW_MOD_CONTROL)) {
    switch (key) {
      case GLFW_KEY_N: new_project(); return true;
      case GLFW_KEY_I: import_dialog(); return true;
      case GLFW_KEY_Z: toggle_undo(); return true;
      case GLFW_KEY_Q: set_visible(false); return true;
      default: return false;
    }
  }

  switch (key) {
    case GLFW_KEY_DELETE:
    case GLFW_KEY_BACKSPACE:
      if (focus_ == Focus::Project) clear_selected();
      return true;
    case GLFW_KEY_ENTER:
      if (focus_ == Focus::Project) open_clip(selected_item_);
      return true;
    case GLFW_KEY_SPACE: clip_toggle_play(); return true;
    case GLFW_KEY_I: clip_mark_in(); return true;
    case GLFW_KEY_O: clip_mark_out(); return true;
    case GLFW_KEY_LEFT: clip_step(-1); return true;
    case GLFW_KEY_RIGHT: clip_step(1); return true;
    case GLFW_KEY_HOME: clip_go_to_in(); return true;
    case GLFW_KEY_END: clip_go_to_out(); return true;
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

void App::simulate_click(nanogui::Vector2i at, int modifiers) {
  cursor_pos_callback_event(at.x(), at.y());
  mouse_button_callback_event(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, modifiers);
  mouse_button_callback_event(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, modifiers);
}

void App::simulate_drag(nanogui::Vector2i from, nanogui::Vector2i to) {
  cursor_pos_callback_event(from.x(), from.y());
  mouse_button_callback_event(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
  for (int i = 1; i <= 8; ++i) {
    cursor_pos_callback_event(from.x() + (to.x() - from.x()) * i / 8.0,
                              from.y() + (to.y() - from.y()) * i / 8.0);
  }
  mouse_button_callback_event(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
}

void App::layout_widgets() {
  // Fixed sizes, so perform_layout() keeps them.
  menu_->set_position({0, 0});
  menu_->set_fixed_size({m_size.x(), MenuBar::kHeight});
  menu_->set_size(menu_->fixed_size());
  status_->set_position({8, m_size.y() - kStatusHeight + 4});
  status_->set_fixed_size({m_size.x() - 16, kStatusHeight - 4});
  status_->set_size(status_->fixed_size());
}
