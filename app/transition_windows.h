#pragma once

#include <functional>
#include <utility>
#include <string>

#include <nanogui/window.h>

#include "mpp/image.h"
#include "mpp/transitions.h"

class FrameView;
class TransitionList;
namespace nanogui {
class Button;
class CheckBox;
class ComboBox;
class Label;
class Slider;
}  // namespace nanogui

// Sample pictures for transition icons and previews: a blue "A" side and
// an orange "B" side.
mpp::Image sample_a(int width, int height);
mpp::Image sample_b(int width, int height);

// The Transitions window: every transition with an icon that plays while
// the mouse is over it. Drag one onto the T track.
class TransitionsWindow : public nanogui::Window {
 public:
  // `drag` gets the transition id and the screen position; `released`
  // drops it.
  TransitionsWindow(nanogui::Widget* parent,
                    std::function<void(const std::string& id, nanogui::Vector2i at, bool released)> drag);

  // Where a transition's row is on screen (self-test).
  nanogui::Vector2i row_center(const std::string& id) const;

 private:
  TransitionList* list_ = nullptr;
};

// Double-clicking a transition opens this: start and end percentages with
// a preview of each, the track arrow (A to B or B to A), reverse, and the
// direction and border where the transition has them. OK calls `apply`.
class TransitionSettingsWindow : public nanogui::Window {
 public:
  TransitionSettingsWindow(nanogui::Widget* parent, const mpp::TransitionDef& def,
                           const mpp::TransitionSettings& settings, mpp::Image a, mpp::Image b,
                           std::function<void(const mpp::TransitionSettings&)> apply);

  const mpp::TransitionSettings& settings() const { return settings_; }
  // Changes settings as the controls would (self-test).
  void set_settings(const mpp::TransitionSettings& settings);
  void ok();
  void cancel();
  // Called once the window has closed, either way.
  void set_closed_callback(std::function<void()> closed) { closed_ = std::move(closed); }

 private:
  void update();  // controls and previews from settings_

  std::function<void()> closed_;
  const mpp::TransitionDef& def_;
  mpp::TransitionSettings settings_;
  mpp::Image a_, b_;
  std::function<void(const mpp::TransitionSettings&)> apply_;
  FrameView* start_view_ = nullptr;
  FrameView* end_view_ = nullptr;
  nanogui::Slider* start_ = nullptr;
  nanogui::Slider* end_ = nullptr;
  nanogui::Label* start_label_ = nullptr;
  nanogui::Label* end_label_ = nullptr;
  nanogui::Button* a_to_b_ = nullptr;
  nanogui::Button* b_to_a_ = nullptr;
  nanogui::CheckBox* reverse_ = nullptr;
  nanogui::ComboBox* direction_ = nullptr;
  nanogui::Slider* border_ = nullptr;
  nanogui::Label* border_label_ = nullptr;
  nanogui::Button* white_ = nullptr;
  nanogui::Button* black_ = nullptr;
};
