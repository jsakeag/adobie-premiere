#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <nanogui/window.h>

#include "mpp/movie_writer.h"

class FrameView;
namespace nanogui {
class Button;
class ComboBox;
class Label;
}  // namespace nanogui

struct CaptureActions {
  // Indexes into the Video and Sound menus.
  std::function<void(int source, int sound)> choose;
  std::function<void(mpp::MovieFormat)> set_format;
  std::function<void()> choose_folder;
  std::function<void()> record;
  std::function<void()> stop;
  std::function<void()> close;
};

struct CaptureWindowState {
  std::string status;  // "Live", "Recording 00:00:01:12", or what went wrong
  std::string folder;
  bool recording = false;
};

// File > Capture (2.0): the live picture from a camera (or the test
// pattern), the sound to record with it, the movie format and folder, and
// Record and Stop.
//
// The Video menu lists the cameras, then "Test pattern". The Sound menu
// starts with "None", then the microphones for a camera or "Test tone" for
// the test pattern.
class CaptureWindow : public nanogui::Window {
 public:
  CaptureWindow(nanogui::Widget* parent, std::vector<std::string> cameras,
                std::vector<std::string> microphones, CaptureActions actions);

  FrameView* view() const { return view_; }
  // Shows a choice made elsewhere, without calling back.
  void show_choice(int source, int sound, mpp::MovieFormat format);
  void show(const CaptureWindowState& state);
  // As the buttons and menus do (self-test).
  void choose(int source, int sound);
  void record() { actions_.record(); }
  void stop() { actions_.stop(); }
  void close() { actions_.close(); }
  // Called by the app once it has closed the window; disposes it.
  void dispose_window();

 private:
  void fill_sounds(int source);

  CaptureActions actions_;
  std::vector<std::string> cameras_;
  std::vector<std::string> microphones_;
  int source_ = -1;
  FrameView* view_ = nullptr;
  nanogui::ComboBox* source_box_ = nullptr;
  nanogui::ComboBox* sound_box_ = nullptr;
  nanogui::ComboBox* format_box_ = nullptr;
  nanogui::Label* folder_ = nullptr;
  nanogui::Button* folder_button_ = nullptr;
  nanogui::Label* status_ = nullptr;
  nanogui::Button* record_ = nullptr;
  nanogui::Button* stop_ = nullptr;
};
