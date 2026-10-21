#pragma once

#include <functional>
#include <utility>

#include <nanogui/window.h>

#include "mpp/image.h"
#include "mpp/keys.h"

class FrameView;
namespace nanogui {
class CheckBox;
class ColorPicker;
class ComboBox;
class Label;
class Slider;
}  // namespace nanogui

// Clip > Transparency: the Super clip's key type, colour, similarity, blend
// and reverse, with a preview of the clip keyed over the picture beneath it
// at the edit line. OK calls `apply`.
class TransparencyWindow : public nanogui::Window {
 public:
  // `picture` is the Super clip's frame fitted to the project frame with
  // its alpha, `opaque` the same made opaque first (what None shows), and
  // `beneath` what A and B show there.
  TransparencyWindow(nanogui::Widget* parent, const mpp::TransparencyKey& key, mpp::Image picture,
                     mpp::Image opaque, mpp::Image beneath,
                     std::function<void(const mpp::TransparencyKey&)> apply);

  const mpp::TransparencyKey& key() const { return key_; }
  void set_key(const mpp::TransparencyKey& key);
  void ok();
  void cancel();
  // Called once the window has closed, either way.
  void set_closed_callback(std::function<void()> closed) { closed_ = std::move(closed); }

 private:
  void update();

  std::function<void()> closed_;
  mpp::TransparencyKey key_;
  mpp::Image picture_, opaque_picture_, beneath_;
  std::function<void(const mpp::TransparencyKey&)> apply_;
  FrameView* preview_ = nullptr;
  nanogui::ComboBox* type_ = nullptr;
  nanogui::ColorPicker* color_ = nullptr;
  nanogui::Slider* similarity_ = nullptr;
  nanogui::Slider* blend_ = nullptr;
  nanogui::Label* similarity_label_ = nullptr;
  nanogui::Label* blend_label_ = nullptr;
  nanogui::CheckBox* reverse_ = nullptr;
};
