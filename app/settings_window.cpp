#include "settings_window.h"

#include <utility>
#include <vector>

#include <nanogui/button.h>
#include <nanogui/combobox.h>
#include <nanogui/label.h>
#include <nanogui/layout.h>
#include <nanogui/screen.h>
#include <nanogui/textbox.h>

namespace {

struct SizePreset {
  int width, height;
  const char* name;
};
const SizePreset kSizes[] = {
    {160, 120, "160 x 120 (Premiere 1.0, NTSC)"},
    {192, 144, "192 x 144 (Premiere 1.0, PAL)"},
    {240, 180, "240 x 180"},
    {320, 240, "320 x 240"},
    {640, 480, "640 x 480"},
};

struct RatePreset {
  mpp::Rate rate;
  const char* name;
};
const RatePreset kRates[] = {
    {{10, 1, false}, "10 fps"},          {{12, 1, false}, "12 fps"},
    {{25, 2, false}, "12.5 fps"},        {{15, 1, false}, "15 fps"},
    {{24, 1, false}, "24 fps"},          {{25, 1, false}, "25 fps"},
    {{30000, 1001, false}, "29.97 fps"}, {{30000, 1001, true}, "29.97 fps drop-frame"},
    {{30, 1, false}, "30 fps"},
};

struct AudioPreset {
  int bits, rate;
  const char* name;
};
const AudioPreset kAudio[] = {
    {8, 11025, "8-bit, 11.025 kHz"},
    {8, 22050, "8-bit, 22.05 kHz (Premiere 1.0)"},
    {16, 22050, "16-bit, 22.05 kHz"},
    {16, 44100, "16-bit, 44.1 kHz (Premiere 2.0)"},
};

template <typename T, std::size_t N>
std::vector<std::string> names(const T (&presets)[N]) {
  std::vector<std::string> out;
  for (const T& p : presets) out.push_back(p.name);
  return out;
}

nanogui::Widget* row(nanogui::Widget* parent, const char* label) {
  nanogui::Widget* w = new nanogui::Widget(parent);
  w->set_layout(new nanogui::BoxLayout(nanogui::Orientation::Horizontal,
                                       nanogui::Alignment::Middle, 0, 8));
  nanogui::Label* l = new nanogui::Label(w, label);
  l->set_fixed_width(90);
  return w;
}

}  // namespace

const std::vector<std::string>& SettingsWindow::size_names() {
  static const std::vector<std::string> n = names(kSizes);
  return n;
}
const std::vector<std::string>& SettingsWindow::rate_names() {
  static const std::vector<std::string> n = names(kRates);
  return n;
}
const std::vector<std::string>& SettingsWindow::audio_names() {
  static const std::vector<std::string> n = names(kAudio);
  return n;
}

SettingsWindow::SettingsWindow(nanogui::Widget* parent, const mpp::ProjectSettings& current,
                               std::function<void(const mpp::ProjectSettings&)> apply)
    : nanogui::Window(parent, "Project Settings"), current_(current), apply_(std::move(apply)) {
  set_layout(new nanogui::BoxLayout(nanogui::Orientation::Vertical,
                                    nanogui::Alignment::Fill, 12, 8));
  set_modal(true);

  size_ = new nanogui::ComboBox(row(this, "Frame size"), size_names());
  rate_ = new nanogui::ComboBox(row(this, "Frame rate"), rate_names());
  audio_ = new nanogui::ComboBox(row(this, "Sound"), audio_names());
  for (nanogui::ComboBox* box : {size_, rate_, audio_}) box->set_fixed_width(260);

  // Select the current settings. Listed values count as chosen, so OK
  // keeps them; picking from a menu always counts.
  for (std::size_t i = 0; i < std::size(kSizes); ++i) {
    if (kSizes[i].width == current.width && kSizes[i].height == current.height) {
      size_->set_selected_index(static_cast<int>(i));
      size_touched_ = true;
    }
  }
  for (std::size_t i = 0; i < std::size(kRates); ++i) {
    if (kRates[i].rate == current.rate) {
      rate_->set_selected_index(static_cast<int>(i));
      rate_touched_ = true;
    }
  }
  for (std::size_t i = 0; i < std::size(kAudio); ++i) {
    if (kAudio[i].bits == current.audio_bits && kAudio[i].rate == current.audio_rate) {
      audio_->set_selected_index(static_cast<int>(i));
      audio_touched_ = true;
    }
  }
  size_->set_callback([this](int) { size_touched_ = true; });
  rate_->set_callback([this](int) { rate_touched_ = true; });
  audio_->set_callback([this](int) { audio_touched_ = true; });

  nanogui::Widget* still = row(this, "Still length");
  still_ = new nanogui::IntBox<int>(still, static_cast<int>(current.still_frames));
  still_->set_editable(true);
  still_->set_units("frames");
  still_->set_min_value(1);
  still_->set_fixed_width(170);

  nanogui::Widget* buttons = new nanogui::Widget(this);
  buttons->set_layout(new nanogui::BoxLayout(nanogui::Orientation::Horizontal,
                                             nanogui::Alignment::Middle, 0, 8));
  nanogui::Button* cancel = new nanogui::Button(buttons, "Cancel");
  cancel->set_callback([this] { dispose(); });
  nanogui::Button* ok = new nanogui::Button(buttons, "OK");
  ok->set_callback([this] { this->ok(); });
}

mpp::ProjectSettings SettingsWindow::settings() const {
  // A menu still showing its first entry because the current value isn't
  // listed leaves that value alone.
  mpp::ProjectSettings s = current_;
  if (size_touched_) {
    s.width = kSizes[size_->selected_index()].width;
    s.height = kSizes[size_->selected_index()].height;
  }
  if (rate_touched_) s.rate = kRates[rate_->selected_index()].rate;
  if (audio_touched_) {
    s.audio_bits = kAudio[audio_->selected_index()].bits;
    s.audio_rate = kAudio[audio_->selected_index()].rate;
  }
  s.still_frames = still_->value();
  return s;
}

void SettingsWindow::choose(int size, int rate, int audio) {
  if (size >= 0) {
    size_->set_selected_index(size);
    size_touched_ = true;
  }
  if (rate >= 0) {
    rate_->set_selected_index(rate);
    rate_touched_ = true;
  }
  if (audio >= 0) {
    audio_->set_selected_index(audio);
    audio_touched_ = true;
  }
}

void SettingsWindow::ok() {
  const mpp::ProjectSettings s = settings();
  std::function<void(const mpp::ProjectSettings&)> apply = apply_;
  dispose();
  if (apply) apply(s);
}
