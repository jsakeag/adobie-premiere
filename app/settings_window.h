#pragma once

#include <functional>
#include <string>
#include <vector>

#include <nanogui/window.h>

#include "mpp/project.h"

namespace nanogui {
class ComboBox;
template <typename T> class IntBox;
}  // namespace nanogui

// Project > Settings: frame size, frame rate, sound format and how long a
// still lasts. OK calls `apply` with the new settings; either button closes
// the window.
class SettingsWindow : public nanogui::Window {
 public:
  SettingsWindow(nanogui::Widget* parent, const mpp::ProjectSettings& current,
                 std::function<void(const mpp::ProjectSettings&)> apply);

  // Chooses entries as the user would (self-test); -1 leaves one alone.
  void choose(int size, int rate, int audio);
  void ok();

  static const std::vector<std::string>& size_names();
  static const std::vector<std::string>& rate_names();
  static const std::vector<std::string>& audio_names();

 private:
  mpp::ProjectSettings settings() const;

  mpp::ProjectSettings current_;
  std::function<void(const mpp::ProjectSettings&)> apply_;
  nanogui::ComboBox* size_ = nullptr;
  nanogui::ComboBox* rate_ = nullptr;
  nanogui::ComboBox* audio_ = nullptr;
  nanogui::IntBox<int>* still_ = nullptr;
  bool size_touched_ = false;
  bool rate_touched_ = false;
  bool audio_touched_ = false;
};
