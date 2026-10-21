#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

#include <nanogui/window.h>

#include "mpp/image.h"
#include "mpp/motion.h"
#include "mpp/rate.h"

class MotionStage;
namespace nanogui {
class Button;
class Label;
class Slider;
template <typename T> class IntBox;
}  // namespace nanogui

// Clip > Motion (Premiere 2.0): the clip's path drawn over the frame, with
// room around it for points off screen. Drag a point to move it; the
// selected point's time, rotation and zoom have sliders. The preview shows
// the clip on its path at the Preview slider's moment. OK calls `apply`
// with the path, or with none after Remove Motion.
class MotionWindow : public nanogui::Window {
 public:
  // `picture` is the clip's picture at its fitted size, before motion.
  MotionWindow(nanogui::Widget* parent, std::optional<mpp::Motion> motion, mpp::Image picture,
               int frame_width, int frame_height,
               std::function<void(const std::optional<mpp::Motion>&)> apply);

  const mpp::Motion& motion() const { return motion_; }
  int selected() const { return selected_; }

  // What the controls do, for the self-test as well.
  void select(int point);
  void add_point();     // half way between the selected point and the next
  void delete_point();  // keeps at least one
  void set_point(const mpp::MotionPoint& point);  // the selected one
  void set_preview(double t);
  void remove_motion();  // OK will take the motion away
  // The stage's screen position for a frame position (fractions).
  nanogui::Vector2i stage_point(double x, double y) const;

  void ok();
  void cancel();
  void set_closed_callback(std::function<void()> closed) { closed_ = std::move(closed); }

 private:
  friend class MotionStage;
  void update();

  std::function<void()> closed_;
  mpp::Motion motion_;
  bool removed_ = false;
  int selected_ = 0;
  double preview_t_ = 0.5;
  mpp::Image picture_;
  int frame_width_, frame_height_;
  std::function<void(const std::optional<mpp::Motion>&)> apply_;

  MotionStage* stage_ = nullptr;
  nanogui::Label* point_label_ = nullptr;
  nanogui::Slider* time_ = nullptr;
  nanogui::Slider* rotation_ = nullptr;
  nanogui::Slider* zoom_ = nullptr;
  nanogui::Slider* preview_ = nullptr;
  nanogui::Label* time_value_ = nullptr;
  nanogui::Label* rotation_value_ = nullptr;
  nanogui::Label* zoom_value_ = nullptr;
};

// Clip > Speed: the clip's speed in percent, with the length it gives.
class SpeedWindow : public nanogui::Window {
 public:
  SpeedWindow(nanogui::Widget* parent, int speed, std::int64_t source_frames, mpp::Rate rate,
              std::function<void(int)> apply);

  void set_speed(int speed);
  void ok();
  void cancel();
  void set_closed_callback(std::function<void()> closed) { closed_ = std::move(closed); }

 private:
  void update();

  std::function<void()> closed_;
  std::int64_t source_frames_;
  mpp::Rate rate_;
  std::function<void(int)> apply_;
  nanogui::IntBox<int>* speed_ = nullptr;
  nanogui::Label* length_ = nullptr;
};
