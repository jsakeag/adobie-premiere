#pragma once

#include <functional>
#include <string>
#include <utility>

#include <nanogui/window.h>

#include "mpp/title.h"

class TitleCanvas;
namespace nanogui {
class Button;
class CheckBox;
class ColorPicker;
class Label;
class Slider;
class TextBox;
}  // namespace nanogui

// The Title window (Premiere 2.0): the title drawn at the project's frame
// shape, where a click selects a text or shape and a drag moves it, with
// controls for the selection and the background. OK calls `apply` with the
// title and its name.
class TitleWindow : public nanogui::Window {
 public:
  // What's selected: a text, a shape, or nothing.
  struct Selection {
    enum class Kind { None, Text, Shape } kind = Kind::None;
    std::size_t index = 0;
  };

  TitleWindow(nanogui::Widget* parent, mpp::TitleSpec title, std::string name, int frame_width,
              int frame_height, std::function<void(const mpp::TitleSpec&, const std::string&)> apply);

  const mpp::TitleSpec& title() const { return title_; }
  const std::string& title_name() const { return name_; }
  Selection selection() const { return selection_; }

  // What the controls do, for the self-test as well.
  void add_text(const std::string& text);
  void add_shape(mpp::ShapeKind kind);
  void delete_selected();
  void select(Selection selection);
  void set_text(const std::string& text);
  void set_title(const mpp::TitleSpec& title);
  void set_name(const std::string& name);
  // Moves the selection so its centre (or anchor) sits at a frame position
  // (fractions of the frame).
  void move_selected_to(double x, double y);
  // The canvas's screen position for a point in the frame (fractions).
  nanogui::Vector2i canvas_point(double x, double y) const;

  void ok();
  void cancel();
  void set_closed_callback(std::function<void()> closed) { closed_ = std::move(closed); }

 private:
  friend class TitleCanvas;
  void update();  // controls and canvas from title_ and selection_

  std::function<void()> closed_;
  mpp::TitleSpec title_;
  std::string name_;
  int frame_width_, frame_height_;
  std::function<void(const mpp::TitleSpec&, const std::string&)> apply_;
  Selection selection_;

  TitleCanvas* canvas_ = nullptr;
  nanogui::TextBox* name_box_ = nullptr;
  nanogui::Widget* text_controls_ = nullptr;
  nanogui::Widget* shape_controls_ = nullptr;
  nanogui::TextBox* text_box_ = nullptr;
  nanogui::Button* regular_ = nullptr;
  nanogui::Button* bold_ = nullptr;
  nanogui::Button* left_ = nullptr;
  nanogui::Button* center_ = nullptr;
  nanogui::Button* right_ = nullptr;
  nanogui::Slider* size_ = nullptr;
  nanogui::CheckBox* shadow_ = nullptr;
  nanogui::CheckBox* filled_ = nullptr;
  nanogui::Slider* width_ = nullptr;
  nanogui::Slider* height_ = nullptr;
  nanogui::ColorPicker* colour_ = nullptr;
  nanogui::CheckBox* clear_background_ = nullptr;
  nanogui::ColorPicker* background_ = nullptr;
  nanogui::Label* hint_ = nullptr;
};
