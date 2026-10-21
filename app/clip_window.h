#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include <nanogui/window.h>

#include "mpp/image.h"
#include "mpp/rate.h"

class FrameView;
class Scrubber;
namespace nanogui {
class Button;
class Label;
class TextBox;
}  // namespace nanogui

struct ClipWindowActions {
  std::function<void(std::int64_t)> seek;  // project frames into the source
  std::function<void()> toggle_play;
  std::function<void(int)> step;           // frames, + or -
  std::function<void()> mark_in;
  std::function<void()> mark_out;
  std::function<void()> go_to_in;
  std::function<void()> go_to_out;
  std::function<void()> focus;             // the window was clicked
};

// What the Clip window shows. Positions are project frames into the source.
struct ClipWindowState {
  bool has_item = false;
  std::string name;
  std::int64_t position = 0;
  std::int64_t in = 0;
  std::int64_t out = 0;
  std::int64_t length = 1;  // the scrub bar's range
  mpp::Rate rate;
  bool playing = false;
  bool still = false;
};

// The Clip window: one bin item's picture, a scrub bar with its in and out
// marks, transport buttons and timecode.
class ClipWindow : public nanogui::Window {
 public:
  ClipWindow(nanogui::Widget* parent, ClipWindowActions actions);

  // Updates the controls; a non-empty frame replaces the picture.
  void show(const ClipWindowState& state, const mpp::Image& frame);

  FrameView* view() const { return view_; }
  // The scrub bar's screen position for a source position (self-test).
  nanogui::Vector2i scrub_point(std::int64_t position) const;

  bool mouse_button_event(const nanogui::Vector2i& p, int button, bool down,
                          int modifiers) override;

 private:
  ClipWindowActions actions_;
  ClipWindowState state_;
  FrameView* view_ = nullptr;
  Scrubber* scrubber_ = nullptr;
  nanogui::Button* play_ = nullptr;
  nanogui::TextBox* position_ = nullptr;
  nanogui::Label* marks_ = nullptr;
};
