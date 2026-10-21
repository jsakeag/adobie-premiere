#include "transition_windows.h"

#include <cmath>
#include <cstdlib>
#include <utility>
#include <vector>

#include <nanogui/button.h>
#include <nanogui/checkbox.h>
#include <nanogui/combobox.h>
#include <nanogui/label.h>
#include <nanogui/layout.h>
#include <nanogui/opengl.h>
#include <nanogui/screen.h>
#include <nanogui/slider.h>
#include <nanogui/vscrollpanel.h>

#include "frame_view.h"

using nanogui::Vector2i;

namespace {

constexpr int kListWidth = 220;
constexpr int kRowHeight = 40;
constexpr int kIconW = 44;
constexpr int kIconH = 33;
constexpr double kIconSeconds = 1.6;  // one play of a hovered icon

// A big letter made of blocks, so the samples read as "A" and "B" without
// a font.
void draw_letter(mpp::Image& img, char letter, mpp::Color ink) {
  static const char* kA[] = {".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"};
  static const char* kB[] = {"####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."};
  const char** rows = letter == 'A' ? kA : kB;
  const int cell = std::max(1, img.height() / 12);
  const int x0 = (img.width() - 5 * cell) / 2, y0 = (img.height() - 7 * cell) / 2;
  for (int r = 0; r < 7; ++r) {
    for (int c = 0; c < 5; ++c) {
      if (rows[r][c] == '#') img.fill({x0 + c * cell, y0 + r * cell, cell, cell}, ink);
    }
  }
}

}  // namespace

mpp::Image sample_a(int w, int h) {
  mpp::Image img(w, h);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      img.set_pixel(x, y, {30, static_cast<std::uint8_t>(50 + 60 * x / w),
                           static_cast<std::uint8_t>(150 + 80 * y / h), 255});
    }
  }
  draw_letter(img, 'A', {235, 235, 255, 255});
  return img;
}

mpp::Image sample_b(int w, int h) {
  mpp::Image img(w, h);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      img.set_pixel(x, y, {static_cast<std::uint8_t>(220 + 30 * y / h),
                           static_cast<std::uint8_t>(120 + 70 * x / w), 30, 255});
    }
  }
  draw_letter(img, 'B', {60, 20, 0, 255});
  return img;
}

// One row per transition: an icon and the name. The hovered row's icon
// plays the transition over and over.
class TransitionList : public nanogui::Widget {
 public:
  TransitionList(nanogui::Widget* parent,
                 std::function<void(const std::string&, Vector2i, bool)> drag)
      : nanogui::Widget(parent), drag_(std::move(drag)),
        a_(sample_a(kIconW, kIconH)), b_(sample_b(kIconW, kIconH)) {
    set_fixed_size({kListWidth, static_cast<int>(mpp::transitions().size()) * kRowHeight});
    icons_.resize(mpp::transitions().size());
  }

  Vector2i preferred_size_impl(NVGcontext*) const override { return fixed_size(); }

  Vector2i row_center(const std::string& id) const {
    const auto& all = mpp::transitions();
    for (std::size_t i = 0; i < all.size(); ++i) {
      if (all[i].id == id) {
        return absolute_position() + Vector2i(kListWidth / 2, static_cast<int>(i) * kRowHeight + kRowHeight / 2);
      }
    }
    return {-1, -1};
  }

  bool mouse_button_event(const Vector2i& p, int button, bool down, int) override {
    if (button != GLFW_MOUSE_BUTTON_LEFT) return true;
    const Vector2i local = p - m_pos;
    if (down) {
      pressed_ = row_at(local.y());
      press_at_ = local;
      dragging_ = false;
    } else {
      if (dragging_ && pressed_ >= 0 && drag_) {
        drag_(mpp::transitions()[pressed_].id, p + parent()->absolute_position(), true);
      }
      pressed_ = -1;
      dragging_ = false;
    }
    return true;
  }

  bool mouse_drag_event(const Vector2i& p, const Vector2i&, int, int) override {
    if (pressed_ < 0) return true;
    const Vector2i local = p - m_pos;
    if (std::abs(local.x() - press_at_.x()) > 4 || std::abs(local.y() - press_at_.y()) > 4) {
      dragging_ = true;
    }
    if (dragging_ && drag_) drag_(mpp::transitions()[pressed_].id, p + parent()->absolute_position(), false);
    return true;
  }

  bool mouse_motion_event(const Vector2i& p, const Vector2i&, int, int) override {
    hover_ = row_at((p - m_pos).y());
    return true;
  }

  bool mouse_enter_event(const Vector2i& p, bool enter) override {
    if (!enter) hover_ = -1;
    return nanogui::Widget::mouse_enter_event(p, enter);
  }

  void draw(NVGcontext* ctx) override {
    const auto& all = mpp::transitions();
    const float x = m_pos.x(), y = m_pos.y();
    nvgFontFace(ctx, "sans");
    nvgFontSize(ctx, 15);
    nvgTextAlign(ctx, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    const double phase = std::fmod(glfwGetTime(), kIconSeconds) / kIconSeconds;
    for (std::size_t i = 0; i < all.size(); ++i) {
      const float top = y + static_cast<float>(i) * kRowHeight;
      const bool hovered = static_cast<int>(i) == hover_;
      if (static_cast<int>(i) == pressed_) {
        nvgBeginPath(ctx);
        nvgRect(ctx, x, top, kListWidth, kRowHeight);
        nvgFillColor(ctx, nvgRGBA(70, 110, 180, 160));
        nvgFill(ctx);
      }
      // Still icons show 40%; the hovered one plays.
      const double progress = hovered ? phase : 0.4;
      if (!icons_[i].valid() || hovered || shown_[i] != progress) {
        icons_[i].set(ctx, mpp::apply_transition(all[i], a_, b_, progress, {}));
        shown_[i] = progress;
      }
      NVGpaint paint = nvgImagePattern(ctx, x + 4, top + (kRowHeight - kIconH) / 2.0f, kIconW,
                                       kIconH, 0, icons_[i].id(), 1.0f);
      nvgBeginPath(ctx);
      nvgRect(ctx, x + 4, top + (kRowHeight - kIconH) / 2.0f, kIconW, kIconH);
      nvgFillPaint(ctx, paint);
      nvgFill(ctx);
      nvgFillColor(ctx, nvgRGB(230, 230, 230));
      nvgText(ctx, x + kIconW + 12, top + kRowHeight / 2.0f, all[i].name.c_str(), nullptr);
    }
    if (hover_ >= 0) screen()->redraw();  // keep the icon playing
  }

 private:
  int row_at(int local_y) const {
    const int i = local_y / kRowHeight;
    return i >= 0 && i < static_cast<int>(mpp::transitions().size()) ? i : -1;
  }

  std::function<void(const std::string&, Vector2i, bool)> drag_;
  mpp::Image a_, b_;
  std::vector<NvgImage> icons_;
  std::vector<double> shown_ = std::vector<double>(mpp::transitions().size(), -1);
  int hover_ = -1;
  int pressed_ = -1;
  Vector2i press_at_{0, 0};
  bool dragging_ = false;
};

TransitionsWindow::TransitionsWindow(
    nanogui::Widget* parent,
    std::function<void(const std::string&, nanogui::Vector2i, bool)> drag)
    : nanogui::Window(parent, "Transitions") {
  set_layout(new nanogui::BoxLayout(nanogui::Orientation::Vertical,
                                    nanogui::Alignment::Fill, 6, 4));
  nanogui::VScrollPanel* scroll = new nanogui::VScrollPanel(this);
  scroll->set_fixed_size({kListWidth + 12, 9 * kRowHeight});
  list_ = new TransitionList(scroll, std::move(drag));
}

nanogui::Vector2i TransitionsWindow::row_center(const std::string& id) const {
  return list_->row_center(id);
}

// ---- Settings dialog ------------------------------------------------------

namespace {

nanogui::Widget* hrow(nanogui::Widget* parent, int spacing = 6) {
  nanogui::Widget* w = new nanogui::Widget(parent);
  w->set_layout(new nanogui::BoxLayout(nanogui::Orientation::Horizontal,
                                       nanogui::Alignment::Middle, 0, spacing));
  return w;
}

std::string percent(double v) { return std::to_string(static_cast<int>(std::lround(v * 100))) + "%"; }

}  // namespace

TransitionSettingsWindow::TransitionSettingsWindow(
    nanogui::Widget* parent, const mpp::TransitionDef& def, const mpp::TransitionSettings& settings,
    mpp::Image a, mpp::Image b, std::function<void(const mpp::TransitionSettings&)> apply)
    : nanogui::Window(parent, def.name + " Settings"),
      def_(def),
      settings_(settings),
      a_(std::move(a)),
      b_(std::move(b)),
      apply_(std::move(apply)) {
  using nanogui::Button;
  using nanogui::Label;
  using nanogui::Slider;
  set_modal(true);
  set_layout(new nanogui::BoxLayout(nanogui::Orientation::Vertical,
                                    nanogui::Alignment::Middle, 12, 8));

  // Start and end previews, each with its slider.
  nanogui::Widget* previews = hrow(this, 16);
  const auto column = [&](const char* title, FrameView*& view, Slider*& slider, Label*& label,
                          bool is_start) {
    nanogui::Widget* col = new nanogui::Widget(previews);
    col->set_layout(new nanogui::BoxLayout(nanogui::Orientation::Vertical,
                                           nanogui::Alignment::Middle, 0, 4));
    new Label(col, title, "sans-bold");
    view = new FrameView(col);
    view->set_fixed_size({160, 120});
    slider = new Slider(col);
    slider->set_fixed_width(160);
    label = new Label(col, "");
    label->set_fixed_width(60);
    slider->set_callback([this, is_start](float v) {
      (is_start ? settings_.start : settings_.end) = v;
      update();
    });
  };
  column("Start", start_view_, start_, start_label_, true);
  column("End", end_view_, end_, end_label_, false);

  // The track arrow.
  nanogui::Widget* arrow = hrow(this);
  new Label(arrow, "Play");
  a_to_b_ = new Button(arrow, "A to B");
  b_to_a_ = new Button(arrow, "B to A");
  for (Button* button : {a_to_b_, b_to_a_}) button->set_flags(Button::RadioButton);
  a_to_b_->set_callback([this] { settings_.b_to_a = false; update(); });
  b_to_a_->set_callback([this] { settings_.b_to_a = true; update(); });
  reverse_ = new nanogui::CheckBox(arrow, "Reverse", [this](bool on) {
    settings_.reverse = on;
    update();
  });

  if (!def.directions.empty()) {
    nanogui::Widget* dir = hrow(this);
    new Label(dir, "Direction");
    direction_ = new nanogui::ComboBox(dir, def.directions);
    direction_->set_fixed_width(200);
    direction_->set_callback([this](int i) {
      settings_.direction = i;
      update();
    });
  }
  if (def.border) {
    nanogui::Widget* border = hrow(this);
    new Label(border, "Border");
    border_ = new Slider(border);
    border_->set_fixed_width(140);
    border_->set_range({0.0f, 20.0f});
    border_->set_callback([this](float v) {
      settings_.border = static_cast<int>(std::lround(v));
      update();
    });
    border_label_ = new Label(border, "");
    border_label_->set_fixed_width(40);
    white_ = new Button(border, "White");
    black_ = new Button(border, "Black");
    for (Button* button : {white_, black_}) button->set_flags(Button::RadioButton);
    white_->set_callback([this] { settings_.border_color = mpp::kWhite; update(); });
    black_->set_callback([this] { settings_.border_color = mpp::kBlack; update(); });
  }

  nanogui::Widget* buttons = hrow(this, 8);
  Button* cancel = new Button(buttons, "Cancel");
  cancel->set_callback([this] { this->cancel(); });
  Button* ok = new Button(buttons, "OK");
  ok->set_callback([this] { this->ok(); });
  update();
}

void TransitionSettingsWindow::set_settings(const mpp::TransitionSettings& settings) {
  settings_ = settings;
  update();
}

void TransitionSettingsWindow::update() {
  start_->set_value(static_cast<float>(settings_.start));
  end_->set_value(static_cast<float>(settings_.end));
  start_label_->set_caption(percent(settings_.start));
  end_label_->set_caption(percent(settings_.end));
  a_to_b_->set_pushed(!settings_.b_to_a);
  b_to_a_->set_pushed(settings_.b_to_a);
  reverse_->set_checked(settings_.reverse);
  if (direction_) direction_->set_selected_index(settings_.direction);
  if (border_) {
    border_->set_value(static_cast<float>(settings_.border));
    border_label_->set_caption(std::to_string(settings_.border) + " px");
    white_->set_pushed(settings_.border_color == mpp::kWhite);
    black_->set_pushed(settings_.border_color == mpp::kBlack);
  }
  const mpp::Image& from = settings_.b_to_a ? b_ : a_;
  const mpp::Image& to = settings_.b_to_a ? a_ : b_;
  start_view_->set_frame(mpp::apply_transition(def_, from, to, settings_.start, settings_));
  end_view_->set_frame(mpp::apply_transition(def_, from, to, settings_.end, settings_));
}

void TransitionSettingsWindow::ok() {
  const mpp::TransitionSettings s = settings_;
  std::function<void(const mpp::TransitionSettings&)> apply = apply_;
  cancel();
  if (apply) apply(s);
}

void TransitionSettingsWindow::cancel() {
  std::function<void()> closed = std::move(closed_);
  dispose();
  if (closed) closed();
}
