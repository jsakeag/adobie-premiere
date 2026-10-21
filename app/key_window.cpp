#include "key_window.h"

#include <cmath>
#include <string>
#include <vector>

#include <nanogui/button.h>
#include <nanogui/checkbox.h>
#include <nanogui/colorpicker.h>
#include <nanogui/combobox.h>
#include <nanogui/label.h>
#include <nanogui/layout.h>
#include <nanogui/slider.h>

#include "frame_view.h"

namespace {

nanogui::Widget* hrow(nanogui::Widget* parent) {
  nanogui::Widget* w = new nanogui::Widget(parent);
  w->set_layout(new nanogui::BoxLayout(nanogui::Orientation::Horizontal,
                                       nanogui::Alignment::Middle, 0, 8));
  return w;
}

nanogui::Label* name_label(nanogui::Widget* parent, const char* text) {
  nanogui::Label* l = new nanogui::Label(parent, text);
  l->set_fixed_width(80);
  return l;
}

std::string percent(double v) { return std::to_string(static_cast<int>(std::lround(v * 100))) + "%"; }

nanogui::Color to_nanogui(mpp::Color c) { return nanogui::Color(c.r, c.g, c.b, 255); }

mpp::Color from_nanogui(const nanogui::Color& c) {
  const auto byte = [](float v) {
    return static_cast<std::uint8_t>(std::lround(std::fmin(std::fmax(v, 0.0f), 1.0f) * 255));
  };
  return {byte(c.r()), byte(c.g()), byte(c.b()), 255};
}

}  // namespace

TransparencyWindow::TransparencyWindow(nanogui::Widget* parent, const mpp::TransparencyKey& key,
                                       mpp::Image picture, mpp::Image opaque,
                                       mpp::Image beneath,
                                       std::function<void(const mpp::TransparencyKey&)> apply)
    : nanogui::Window(parent, "Transparency Settings"),
      key_(key),
      picture_(std::move(picture)),
      opaque_picture_(std::move(opaque)),
      beneath_(std::move(beneath)),
      apply_(std::move(apply)) {
  set_modal(true);
  set_layout(new nanogui::BoxLayout(nanogui::Orientation::Vertical,
                                    nanogui::Alignment::Middle, 12, 8));
  preview_ = new FrameView(this);
  preview_->set_fixed_size({320, 240});

  nanogui::Widget* type = hrow(this);
  name_label(type, "Key type");
  std::vector<std::string> names;
  for (const auto& [t, name] : mpp::key_types()) names.push_back(name);
  type_ = new nanogui::ComboBox(type, names);
  type_->set_fixed_width(200);
  type_->set_callback([this](int i) {
    key_.type = mpp::key_types()[static_cast<std::size_t>(i)].first;
    update();
  });

  nanogui::Widget* color = hrow(this);
  name_label(color, "Colour");
  color_ = new nanogui::ColorPicker(color, to_nanogui(key_.color));
  color_->set_fixed_size({200, 26});
  color_->set_final_callback([this](const nanogui::Color& c) {
    key_.color = from_nanogui(c);
    update();
  });

  const auto slider_row = [&](const char* text, nanogui::Slider*& slider, nanogui::Label*& label,
                              double mpp::TransparencyKey::*field) {
    nanogui::Widget* row = hrow(this);
    name_label(row, text);
    slider = new nanogui::Slider(row);
    slider->set_fixed_width(160);
    slider->set_callback([this, field](float v) {
      key_.*field = v;
      update();
    });
    label = new nanogui::Label(row, "");
    label->set_fixed_width(50);
  };
  slider_row("Similarity", similarity_, similarity_label_, &mpp::TransparencyKey::similarity);
  slider_row("Blend", blend_, blend_label_, &mpp::TransparencyKey::blend);

  reverse_ = new nanogui::CheckBox(this, "Reverse key", [this](bool on) {
    key_.reverse = on;
    update();
  });

  nanogui::Widget* buttons = hrow(this);
  nanogui::Button* cancel = new nanogui::Button(buttons, "Cancel");
  cancel->set_callback([this] { this->cancel(); });
  nanogui::Button* ok = new nanogui::Button(buttons, "OK");
  ok->set_callback([this] { this->ok(); });
  update();
}

void TransparencyWindow::set_key(const mpp::TransparencyKey& key) {
  key_ = key;
  update();
}

void TransparencyWindow::update() {
  const auto& types = mpp::key_types();
  for (std::size_t i = 0; i < types.size(); ++i) {
    if (types[i].first == key_.type) type_->set_selected_index(static_cast<int>(i));
  }
  color_->set_enabled(mpp::key_uses_color(key_.type));
  const bool tolerances = mpp::key_uses_similarity(key_.type);
  similarity_->set_enabled(tolerances);
  blend_->set_enabled(tolerances);
  similarity_->set_value(static_cast<float>(key_.similarity));
  blend_->set_value(static_cast<float>(key_.blend));
  similarity_label_->set_caption(percent(key_.similarity));
  blend_label_->set_caption(percent(key_.blend));
  reverse_->set_checked(key_.reverse);

  // As the renderer does it: None sees the picture as opaque.
  mpp::Image shown = beneath_;
  mpp::composite_superimpose(shown, key_.type == mpp::KeyType::None ? opaque_picture_ : picture_,
                             key_, 1.0);
  preview_->set_frame(shown);
}

void TransparencyWindow::ok() {
  const mpp::TransparencyKey key = key_;
  std::function<void(const mpp::TransparencyKey&)> apply = apply_;
  cancel();
  if (apply) apply(key);
}

void TransparencyWindow::cancel() {
  std::function<void()> closed = std::move(closed_);
  dispose();
  if (closed) closed();
}
