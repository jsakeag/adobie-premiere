#include "title_window.h"

#include <algorithm>
#include <cmath>

#include <nanogui/button.h>
#include <nanogui/checkbox.h>
#include <nanogui/colorpicker.h>
#include <nanogui/label.h>
#include <nanogui/layout.h>
#include <nanogui/opengl.h>
#include <nanogui/screen.h>
#include <nanogui/slider.h>
#include <nanogui/textbox.h>

#include "frame_view.h"

using nanogui::Vector2i;
using Selection = TitleWindow::Selection;

namespace {

constexpr int kCanvasWidth = 400;

nanogui::Widget* hrow(nanogui::Widget* parent, int spacing = 6) {
  nanogui::Widget* w = new nanogui::Widget(parent);
  w->set_layout(new nanogui::BoxLayout(nanogui::Orientation::Horizontal,
                                       nanogui::Alignment::Middle, 0, spacing));
  return w;
}

nanogui::Widget* vbox(nanogui::Widget* parent, int spacing = 6) {
  nanogui::Widget* w = new nanogui::Widget(parent);
  w->set_layout(new nanogui::BoxLayout(nanogui::Orientation::Vertical,
                                       nanogui::Alignment::Minimum, 0, spacing));
  return w;
}

nanogui::Label* caption(nanogui::Widget* parent, const char* text, int width = 70) {
  nanogui::Label* l = new nanogui::Label(parent, text);
  l->set_fixed_width(width);
  return l;
}

nanogui::Color to_nanogui(mpp::Color c) { return nanogui::Color(c.r, c.g, c.b, 255); }

mpp::Color from_nanogui(const nanogui::Color& c, std::uint8_t alpha) {
  const auto byte = [](float v) {
    return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255));
  };
  return {byte(c.r()), byte(c.g()), byte(c.b()), alpha};
}

}  // namespace

// The title as it will look, with the selection outlined. A click selects
// whatever is on top there; dragging moves it.
class TitleCanvas : public nanogui::Widget {
 public:
  TitleCanvas(nanogui::Widget* parent, TitleWindow* window, int frame_width, int frame_height)
      : nanogui::Widget(parent), window_(window) {
    set_fixed_size({kCanvasWidth, std::max(1, kCanvasWidth * frame_height / frame_width)});
  }

  void refresh() { dirty_ = true; }

  Vector2i point(double fx, double fy) const {
    return absolute_position() + Vector2i(static_cast<int>(std::lround(fx * m_size.x())),
                                          static_cast<int>(std::lround(fy * m_size.y())));
  }

  bool mouse_button_event(const Vector2i& p, int button, bool down, int) override {
    if (button != GLFW_MOUSE_BUTTON_LEFT || !down) {
      dragging_ = false;
      return true;
    }
    const Vector2i local = p - m_pos;
    const mpp::TitleSpec& t = window_->title_;
    const int w = m_size.x(), h = m_size.y();
    Selection hit;
    for (std::size_t i = t.texts.size(); i-- > 0;) {
      if (inside(mpp::text_bounds(t.texts[i], w, h), local)) {
        hit = {Selection::Kind::Text, i};
        break;
      }
    }
    if (hit.kind == Selection::Kind::None) {
      for (std::size_t i = t.shapes.size(); i-- > 0;) {
        if (inside(mpp::shape_bounds(t.shapes[i], w, h), local)) {
          hit = {Selection::Kind::Shape, i};
          break;
        }
      }
    }
    window_->select(hit);
    if (hit.kind == Selection::Kind::None) return true;
    // Keep the grab point under the pointer while dragging.
    const auto [ax, ay] = anchor(hit);
    grab_x_ = ax - static_cast<double>(local.x()) / w;
    grab_y_ = ay - static_cast<double>(local.y()) / h;
    dragging_ = true;
    return true;
  }

  bool mouse_drag_event(const Vector2i& p, const Vector2i&, int, int) override {
    if (!dragging_) return true;
    const Vector2i local = p - m_pos;
    window_->move_selected_to(std::clamp(static_cast<double>(local.x()) / m_size.x() + grab_x_, 0.0, 1.0),
                              std::clamp(static_cast<double>(local.y()) / m_size.y() + grab_y_, 0.0, 1.0));
    return true;
  }

  void draw(NVGcontext* ctx) override {
    const float x = m_pos.x(), y = m_pos.y(), w = m_size.x(), h = m_size.y();
    // A checkerboard shows where the title is clear.
    for (int cy = 0; cy < m_size.y(); cy += 10) {
      for (int cx = 0; cx < m_size.x(); cx += 10) {
        nvgBeginPath(ctx);
        nvgRect(ctx, x + cx, y + cy, std::min(10, m_size.x() - cx), std::min(10, m_size.y() - cy));
        nvgFillColor(ctx, ((cx + cy) / 10) % 2 ? nvgRGB(150, 150, 150) : nvgRGB(110, 110, 110));
        nvgFill(ctx);
      }
    }
    if (dirty_) {
      image_.set(ctx, mpp::render_title(window_->title_, m_size.x(), m_size.y()));
      dirty_ = false;
    }
    if (image_.valid()) {
      NVGpaint paint = nvgImagePattern(ctx, x, y, w, h, 0, image_.id(), 1.0f);
      nvgBeginPath(ctx);
      nvgRect(ctx, x, y, w, h);
      nvgFillPaint(ctx, paint);
      nvgFill(ctx);
    }
    const Selection s = window_->selection_;
    if (s.kind != Selection::Kind::None) {
      const mpp::TitleSpec& t = window_->title_;
      const mpp::Rect r = s.kind == Selection::Kind::Text
                              ? mpp::text_bounds(t.texts[s.index], m_size.x(), m_size.y())
                              : mpp::shape_bounds(t.shapes[s.index], m_size.x(), m_size.y());
      nvgBeginPath(ctx);
      nvgRect(ctx, x + r.x - 2.5f, y + r.y - 2.5f, r.width + 5.0f, r.height + 5.0f);
      nvgStrokeColor(ctx, nvgRGB(255, 255, 255));
      nvgStrokeWidth(ctx, 1.0f);
      nvgStroke(ctx);
      nvgStrokeColor(ctx, nvgRGB(0, 0, 0));
      nvgBeginPath(ctx);
      nvgRect(ctx, x + r.x - 3.5f, y + r.y - 3.5f, r.width + 7.0f, r.height + 7.0f);
      nvgStroke(ctx);
    }
  }

 private:
  static bool inside(const mpp::Rect& r, const Vector2i& p) {
    return p.x() >= r.x - 3 && p.x() < r.right() + 3 && p.y() >= r.y - 3 && p.y() < r.bottom() + 3;
  }

  std::pair<double, double> anchor(Selection s) const {
    const mpp::TitleSpec& t = window_->title_;
    if (s.kind == Selection::Kind::Text) return {t.texts[s.index].x, t.texts[s.index].y};
    return {t.shapes[s.index].x, t.shapes[s.index].y};
  }

  TitleWindow* window_;
  NvgImage image_;
  bool dirty_ = true;
  bool dragging_ = false;
  double grab_x_ = 0, grab_y_ = 0;
};

TitleWindow::TitleWindow(nanogui::Widget* parent, mpp::TitleSpec title, std::string name,
                         int frame_width, int frame_height,
                         std::function<void(const mpp::TitleSpec&, const std::string&)> apply)
    : nanogui::Window(parent, "Title"),
      title_(std::move(title)),
      name_(std::move(name)),
      frame_width_(std::max(1, frame_width)),
      frame_height_(std::max(1, frame_height)),
      apply_(std::move(apply)) {
  using nanogui::Button;
  set_modal(true);
  set_layout(new nanogui::BoxLayout(nanogui::Orientation::Vertical,
                                    nanogui::Alignment::Middle, 12, 8));
  nanogui::Widget* body = hrow(this, 14);
  nanogui::Widget* left = vbox(body);
  canvas_ = new TitleCanvas(left, this, frame_width_, frame_height_);
  hint_ = new nanogui::Label(left, "Click a text or shape to select it; drag to move it.");
  hint_->set_font_size(14);

  nanogui::Widget* right = vbox(body, 8);
  nanogui::Widget* name_row = hrow(right);
  caption(name_row, "Name");
  name_box_ = new nanogui::TextBox(name_row, name_);
  name_box_->set_editable(true);
  name_box_->set_fixed_width(220);
  name_box_->set_alignment(nanogui::TextBox::Alignment::Left);
  name_box_->set_callback([this](const std::string& v) {
    if (!v.empty()) name_ = v;
    return !v.empty();
  });

  nanogui::Widget* adds = hrow(right, 4);
  Button* add_text = new Button(adds, "Add Text");
  add_text->set_callback([this] { this->add_text("Text"); });
  Button* add_rect = new Button(adds, "Rectangle");
  add_rect->set_callback([this] { add_shape(mpp::ShapeKind::Rectangle); });
  Button* add_oval = new Button(adds, "Oval");
  add_oval->set_callback([this] { add_shape(mpp::ShapeKind::Oval); });
  Button* remove = new Button(adds, "Delete");
  remove->set_callback([this] { delete_selected(); });

  // Text settings.
  text_controls_ = vbox(right);
  nanogui::Widget* text_row = hrow(text_controls_);
  caption(text_row, "Text");
  text_box_ = new nanogui::TextBox(text_row, "");
  text_box_->set_editable(true);
  text_box_->set_fixed_width(220);
  text_box_->set_alignment(nanogui::TextBox::Alignment::Left);
  text_box_->set_callback([this](const std::string& v) {
    set_text(v);
    return true;
  });
  nanogui::Widget* font_row = hrow(text_controls_, 4);
  caption(font_row, "Font");
  regular_ = new Button(font_row, "Regular");
  bold_ = new Button(font_row, "Bold");
  for (Button* b : {regular_, bold_}) b->set_flags(Button::RadioButton);
  const auto on_text = [this](auto change) {
    return [this, change] {
      if (selection_.kind != Selection::Kind::Text) return;
      change(title_.texts[selection_.index]);
      update();
    };
  };
  regular_->set_callback(on_text([](mpp::TitleText& t) { t.font = mpp::TitleFont::Regular; }));
  bold_->set_callback(on_text([](mpp::TitleText& t) { t.font = mpp::TitleFont::Bold; }));
  nanogui::Widget* align_row = hrow(text_controls_, 4);
  caption(align_row, "Align");
  left_ = new Button(align_row, "Left");
  center_ = new Button(align_row, "Centre");
  right_ = new Button(align_row, "Right");
  for (Button* b : {left_, center_, right_}) b->set_flags(Button::RadioButton);
  left_->set_callback(on_text([](mpp::TitleText& t) { t.align = mpp::TextAlign::Left; }));
  center_->set_callback(on_text([](mpp::TitleText& t) { t.align = mpp::TextAlign::Center; }));
  right_->set_callback(on_text([](mpp::TitleText& t) { t.align = mpp::TextAlign::Right; }));
  nanogui::Widget* size_row = hrow(text_controls_);
  caption(size_row, "Size");
  size_ = new nanogui::Slider(size_row);
  size_->set_range({0.03f, 0.4f});
  size_->set_fixed_width(160);
  size_->set_callback([this](float v) {
    if (selection_.kind != Selection::Kind::Text) return;
    title_.texts[selection_.index].size = v;
    update();
  });
  shadow_ = new nanogui::CheckBox(text_controls_, "Shadow", [this](bool on) {
    if (selection_.kind != Selection::Kind::Text) return;
    title_.texts[selection_.index].shadow = on;
    update();
  });

  // Shape settings.
  shape_controls_ = vbox(right);
  filled_ = new nanogui::CheckBox(shape_controls_, "Filled", [this](bool on) {
    if (selection_.kind != Selection::Kind::Shape) return;
    title_.shapes[selection_.index].filled = on;
    update();
  });
  const auto shape_slider = [&](const char* label, nanogui::Slider*& slider, double mpp::TitleShape::*field) {
    nanogui::Widget* row = hrow(shape_controls_);
    caption(row, label);
    slider = new nanogui::Slider(row);
    slider->set_range({0.02f, 1.0f});
    slider->set_fixed_width(160);
    slider->set_callback([this, field](float v) {
      if (selection_.kind != Selection::Kind::Shape) return;
      title_.shapes[selection_.index].*field = v;
      update();
    });
  };
  shape_slider("Width", width_, &mpp::TitleShape::width);
  shape_slider("Height", height_, &mpp::TitleShape::height);

  nanogui::Widget* colour_row = hrow(right);
  caption(colour_row, "Colour");
  colour_ = new nanogui::ColorPicker(colour_row, nanogui::Color(255, 255, 255, 255));
  colour_->set_fixed_size({160, 24});
  colour_->set_final_callback([this](const nanogui::Color& c) {
    if (selection_.kind == Selection::Kind::Text) {
      title_.texts[selection_.index].color = from_nanogui(c, 255);
    } else if (selection_.kind == Selection::Kind::Shape) {
      mpp::Color& keep = title_.shapes[selection_.index].color;
      keep = from_nanogui(c, keep.a);
    }
    update();
  });

  nanogui::Widget* bg_row = hrow(right);
  caption(bg_row, "Background");
  clear_background_ = new nanogui::CheckBox(bg_row, "Clear", [this](bool on) {
    title_.background.a = on ? 0 : 255;
    update();
  });
  background_ = new nanogui::ColorPicker(bg_row, to_nanogui(title_.background));
  background_->set_fixed_size({100, 24});
  background_->set_final_callback([this](const nanogui::Color& c) {
    title_.background = from_nanogui(c, 255);
    update();
  });

  nanogui::Widget* buttons = hrow(this, 8);
  Button* cancel = new Button(buttons, "Cancel");
  cancel->set_callback([this] { this->cancel(); });
  Button* ok = new Button(buttons, "OK");
  ok->set_callback([this] { this->ok(); });

  if (!title_.texts.empty()) selection_ = {Selection::Kind::Text, 0};
  update();
}

void TitleWindow::add_text(const std::string& text) {
  mpp::TitleText t;
  t.text = text;
  t.y = 0.3 + 0.1 * static_cast<double>(title_.texts.size() % 5);
  title_.texts.push_back(t);
  selection_ = {Selection::Kind::Text, title_.texts.size() - 1};
  update();
}

void TitleWindow::add_shape(mpp::ShapeKind kind) {
  mpp::TitleShape s;
  s.kind = kind;
  if (kind == mpp::ShapeKind::Oval) s.width = s.height = 0.3;
  title_.shapes.push_back(s);
  selection_ = {Selection::Kind::Shape, title_.shapes.size() - 1};
  update();
}

void TitleWindow::delete_selected() {
  if (selection_.kind == Selection::Kind::Text) {
    title_.texts.erase(title_.texts.begin() + static_cast<std::ptrdiff_t>(selection_.index));
  } else if (selection_.kind == Selection::Kind::Shape) {
    title_.shapes.erase(title_.shapes.begin() + static_cast<std::ptrdiff_t>(selection_.index));
  }
  selection_ = {};
  update();
}

void TitleWindow::select(Selection selection) {
  const std::size_t count = selection.kind == Selection::Kind::Text    ? title_.texts.size()
                            : selection.kind == Selection::Kind::Shape ? title_.shapes.size()
                                                                       : 0;
  selection_ = selection.index < count ? selection : Selection{};
  update();
}

void TitleWindow::set_text(const std::string& text) {
  if (selection_.kind != Selection::Kind::Text) return;
  title_.texts[selection_.index].text = text;
  update();
}

void TitleWindow::set_title(const mpp::TitleSpec& title) {
  title_ = title;
  select(selection_);
}

void TitleWindow::set_name(const std::string& name) {
  if (name.empty()) return;
  name_ = name;
  update();
}

void TitleWindow::move_selected_to(double x, double y) {
  if (selection_.kind == Selection::Kind::Text) {
    title_.texts[selection_.index].x = x;
    title_.texts[selection_.index].y = y;
  } else if (selection_.kind == Selection::Kind::Shape) {
    title_.shapes[selection_.index].x = x;
    title_.shapes[selection_.index].y = y;
  }
  update();
}

nanogui::Vector2i TitleWindow::canvas_point(double x, double y) const { return canvas_->point(x, y); }

void TitleWindow::update() {
  canvas_->refresh();
  name_box_->set_value(name_);
  const bool text = selection_.kind == Selection::Kind::Text;
  const bool shape = selection_.kind == Selection::Kind::Shape;
  const bool layout = text_controls_->visible() != text || shape_controls_->visible() != shape;
  text_controls_->set_visible(text);
  shape_controls_->set_visible(shape);
  colour_->set_enabled(text || shape);
  if (text) {
    const mpp::TitleText& t = title_.texts[selection_.index];
    if (!text_box_->focused()) text_box_->set_value(t.text);
    regular_->set_pushed(t.font == mpp::TitleFont::Regular);
    bold_->set_pushed(t.font == mpp::TitleFont::Bold);
    left_->set_pushed(t.align == mpp::TextAlign::Left);
    center_->set_pushed(t.align == mpp::TextAlign::Center);
    right_->set_pushed(t.align == mpp::TextAlign::Right);
    size_->set_value(static_cast<float>(t.size));
    shadow_->set_checked(t.shadow);
    colour_->set_color(to_nanogui(t.color));
  } else if (shape) {
    const mpp::TitleShape& s = title_.shapes[selection_.index];
    filled_->set_checked(s.filled);
    width_->set_value(static_cast<float>(s.width));
    height_->set_value(static_cast<float>(s.height));
    colour_->set_color(to_nanogui(s.color));
  }
  clear_background_->set_checked(title_.background.a == 0);
  if (layout) {
    if (nanogui::Screen* s = screen()) perform_layout(s->nvg_context());
  }
}

void TitleWindow::ok() {
  const mpp::TitleSpec title = title_;
  const std::string name = name_box_->value().empty() ? name_ : name_box_->value();
  std::function<void(const mpp::TitleSpec&, const std::string&)> apply = apply_;
  cancel();
  if (apply) apply(title, name);
}

void TitleWindow::cancel() {
  std::function<void()> closed = std::move(closed_);
  dispose();
  if (closed) closed();
}
