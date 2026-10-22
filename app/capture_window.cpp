#include "capture_window.h"

#include <nanogui/button.h>
#include <nanogui/combobox.h>
#include <nanogui/icons.h>
#include <nanogui/label.h>
#include <nanogui/layout.h>
#include <nanogui/screen.h>

#include "frame_view.h"

namespace {

nanogui::Widget* row(nanogui::Widget* parent, const char* label) {
  nanogui::Widget* w = new nanogui::Widget(parent);
  w->set_layout(new nanogui::BoxLayout(nanogui::Orientation::Horizontal,
                                       nanogui::Alignment::Middle, 0, 8));
  if (label) {
    nanogui::Label* l = new nanogui::Label(w, label);
    l->set_fixed_width(52);
  }
  return w;
}

}  // namespace

CaptureWindow::CaptureWindow(nanogui::Widget* parent, std::vector<std::string> cameras,
                             std::vector<std::string> microphones, CaptureActions actions)
    : nanogui::Window(parent, "Capture"),
      actions_(std::move(actions)),
      cameras_(std::move(cameras)),
      microphones_(std::move(microphones)) {
  set_layout(new nanogui::BoxLayout(nanogui::Orientation::Vertical,
                                    nanogui::Alignment::Fill, 8, 6));
  view_ = new FrameView(this);
  view_->set_fixed_size({320, 240});

  std::vector<std::string> sources = cameras_;
  sources.push_back("Test pattern");
  source_box_ = new nanogui::ComboBox(row(this, "Video"), sources);
  source_box_->set_fixed_width(260);
  source_box_->set_callback([this](int i) { choose(i, 1); });
  sound_box_ = new nanogui::ComboBox(row(this, "Sound"), {"None"});
  sound_box_->set_fixed_width(260);
  sound_box_->set_callback([this](int i) { choose(source_, i); });
  format_box_ = new nanogui::ComboBox(row(this, "Format"),
                                      {"QuickTime Animation (.mov)", "H.264 (.mp4)"});
  format_box_->set_fixed_width(260);
  format_box_->set_callback([this](int i) {
    actions_.set_format(i == 0 ? mpp::MovieFormat::Animation : mpp::MovieFormat::H264);
  });
  nanogui::Widget* folder = row(this, "Folder");
  folder_ = new nanogui::Label(folder, "");
  // Fixed sizes: an empty label would be laid out with none.
  folder_->set_fixed_size({184, 20});
  folder_->set_font_size(14);
  folder_button_ = new nanogui::Button(folder, "Choose...");
  folder_button_->set_fixed_width(68);
  folder_button_->set_callback([this] { actions_.choose_folder(); });

  status_ = new nanogui::Label(this, "");
  status_->set_fixed_size({320, 20});
  status_->set_font_size(14);
  nanogui::Widget* buttons = row(this, nullptr);
  record_ = new nanogui::Button(buttons, "Record", FA_CIRCLE);
  record_->set_text_color(nanogui::Color(220, 60, 60, 255));
  record_->set_fixed_width(96);
  record_->set_callback([this] { record(); });
  stop_ = new nanogui::Button(buttons, "Stop", FA_STOP);
  stop_->set_fixed_width(96);
  stop_->set_callback([this] { stop(); });
  nanogui::Button* done = new nanogui::Button(buttons, "Close");
  done->set_fixed_width(96);
  done->set_callback([this] { close(); });
}

void CaptureWindow::fill_sounds(int source) {
  if (source == source_) return;
  source_ = source;
  std::vector<std::string> sounds = {"None"};
  if (source < static_cast<int>(cameras_.size())) {
    sounds.insert(sounds.end(), microphones_.begin(), microphones_.end());
  } else {
    sounds.push_back("Test tone");
  }
  sound_box_->set_items(sounds);
  if (nanogui::Screen* s = screen()) perform_layout(s->nvg_context());
}

void CaptureWindow::show_choice(int source, int sound, mpp::MovieFormat format) {
  fill_sounds(source);
  source_box_->set_selected_index(source);
  sound_box_->set_selected_index(sound);
  format_box_->set_selected_index(format == mpp::MovieFormat::Animation ? 0 : 1);
}

void CaptureWindow::choose(int source, int sound) {
  fill_sounds(source);
  const int count = static_cast<int>(sound_box_->items().size());
  actions_.choose(source, sound < count ? sound : 0);
}

void CaptureWindow::show(const CaptureWindowState& state) {
  status_->set_caption(state.status);
  status_->set_color(state.recording ? nanogui::Color(240, 90, 90, 255)
                                     : nanogui::Color(220, 220, 220, 255));
  folder_->set_caption(state.folder);
  folder_->set_tooltip(state.folder);
  record_->set_enabled(!state.recording);
  stop_->set_enabled(state.recording);
  source_box_->set_enabled(!state.recording);
  sound_box_->set_enabled(!state.recording);
  format_box_->set_enabled(!state.recording);
  folder_button_->set_enabled(!state.recording);
}

void CaptureWindow::dispose_window() { dispose(); }
