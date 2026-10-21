#include "motion_window.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include <nanogui/button.h>
#include <nanogui/label.h>
#include <nanogui/layout.h>
#include <nanogui/opengl.h>
#include <nanogui/slider.h>
#include <nanogui/textbox.h>

#include "frame_view.h"

using nanogui::Vector2i;

namespace {

constexpr int kFrameShown = 240;  // the frame's width on the stage

nanogui::Widget* hrow(nanogui::Widget* parent, int spacing = 6) {
  nanogui::Widget* w = new nanogui::Widget(parent);
  w->set_layout(new nanogui::BoxLayout(nanogui::Orientation::Horizontal,
                                       nanogui::Alignment::Middle, 0, spacing));
  return w;
}

std::string number(double v, const char* unit) {
  char text[32];
  std::snprintf(text, sizeof(text), "%.0f%s", v, unit);
  return text;
}

}  // namespace

// The frame with a margin of half a frame all round, the path through the
// points, and the clip where the preview moment puts it.
class MotionStage : public nanogui::Widget {
 public:
  MotionStage(nanogui::Widget* parent, MotionWindow* window)
      : nanogui::Widget(parent), window_(window) {
    fw_ = kFrameShown;
    fh_ = std::max(1, kFrameShown * window->frame_height_ / window->frame_width_);
    set_fixed_size({fw_ * 2, fh_ * 2});
  }

  void refresh() { dirty_ = true; }

  // Frame fractions to stage pixels and back.
  float sx(double x) const { return static_cast<float>((x + 0.5) * fw_); }
  float sy(double y) const { return static_cast<float>((y + 0.5) * fh_); }
  double fx(int px) const { return static_cast<double>(px) / fw_ - 0.5; }
  double fy(int py) const { return static_cast<double>(py) / fh_ - 0.5; }

  Vector2i point(double x, double y) const {
    return absolute_position() + Vector2i(static_cast<int>(std::lround(sx(x))),
                                          static_cast<int>(std::lround(sy(y))));
  }

  bool mouse_button_event(const Vector2i& p, int button, bool down, int) override {
    if (button != GLFW_MOUSE_BUTTON_LEFT) return true;
    dragging_ = false;
    if (!down) return true;
    const Vector2i local = p - m_pos;
    const auto& points = window_->motion_.points;
    for (std::size_t i = points.size(); i-- > 0;) {
      if (std::abs(local.x() - sx(points[i].x)) <= 6 && std::abs(local.y() - sy(points[i].y)) <= 6) {
        window_->select(static_cast<int>(i));
        dragging_ = true;
        return true;
      }
    }
    return true;
  }

  bool mouse_drag_event(const Vector2i& p, const Vector2i&, int, int) override {
    if (!dragging_) return true;
    const Vector2i local = p - m_pos;
    mpp::MotionPoint m = window_->motion_.points[static_cast<std::size_t>(window_->selected_)];
    m.x = std::clamp(fx(local.x()), -0.5, 1.5);
    m.y = std::clamp(fy(local.y()), -0.5, 1.5);
    window_->set_point(m);
    return true;
  }

  void draw(NVGcontext* ctx) override {
    const float x = m_pos.x(), y = m_pos.y();
    nvgBeginPath(ctx);
    nvgRect(ctx, x, y, m_size.x(), m_size.y());
    nvgFillColor(ctx, nvgRGB(55, 55, 60));
    nvgFill(ctx);
    if (dirty_) {
      const mpp::Image frame = mpp::place_picture(window_->picture_, &window_->motion_, window_->preview_t_,
                                                  window_->frame_width_, window_->frame_height_);
      mpp::Image shown(frame.width(), frame.height());
      // Over black, as on Video A.
      for (std::size_t i = 0; i < shown.size_bytes(); i += 4) {
        const float a = frame.data()[i + 3] / 255.0f;
        for (int k = 0; k < 3; ++k) shown.data()[i + k] = static_cast<std::uint8_t>(frame.data()[i + k] * a);
        shown.data()[i + 3] = 255;
      }
      image_.set(ctx, shown);
      dirty_ = false;
    }
    const float fx0 = x + fw_ / 2.0f, fy0 = y + fh_ / 2.0f;
    if (image_.valid()) {
      NVGpaint paint = nvgImagePattern(ctx, fx0, fy0, fw_, fh_, 0, image_.id(), 1.0f);
      nvgBeginPath(ctx);
      nvgRect(ctx, fx0, fy0, fw_, fh_);
      nvgFillPaint(ctx, paint);
      nvgFill(ctx);
    }
    nvgBeginPath(ctx);
    nvgRect(ctx, fx0 - 0.5f, fy0 - 0.5f, fw_ + 1.0f, fh_ + 1.0f);
    nvgStrokeColor(ctx, nvgRGB(200, 200, 200));
    nvgStrokeWidth(ctx, 1.0f);
    nvgStroke(ctx);

    // The path, its points, and where the preview moment is on it.
    const auto& points = window_->motion_.points;
    nvgBeginPath(ctx);
    for (std::size_t i = 0; i < points.size(); ++i) {
      if (i == 0) nvgMoveTo(ctx, x + sx(points[i].x), y + sy(points[i].y));
      else nvgLineTo(ctx, x + sx(points[i].x), y + sy(points[i].y));
    }
    nvgStrokeColor(ctx, nvgRGB(255, 220, 60));
    nvgStrokeWidth(ctx, 1.5f);
    nvgStroke(ctx);
    nvgFontFace(ctx, "sans");
    nvgFontSize(ctx, 12);
    nvgTextAlign(ctx, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
    for (std::size_t i = 0; i < points.size(); ++i) {
      const float px = x + sx(points[i].x), py = y + sy(points[i].y);
      const bool chosen = static_cast<int>(i) == window_->selected_;
      nvgBeginPath(ctx);
      nvgRect(ctx, px - 4, py - 4, 8, 8);
      nvgFillColor(ctx, chosen ? nvgRGB(255, 220, 60) : nvgRGB(30, 30, 30));
      nvgFill(ctx);
      nvgStrokeColor(ctx, nvgRGB(255, 220, 60));
      nvgStroke(ctx);
      nvgFillColor(ctx, nvgRGB(240, 240, 240));
      nvgText(ctx, px + 6, py - 4, std::to_string(i + 1).c_str(), nullptr);
    }
    const mpp::MotionPoint now = mpp::motion_at(window_->motion_, window_->preview_t_);
    nvgBeginPath(ctx);
    nvgCircle(ctx, x + sx(now.x), y + sy(now.y), 3.5f);
    nvgFillColor(ctx, nvgRGB(255, 80, 80));
    nvgFill(ctx);
  }

 private:
  MotionWindow* window_;
  int fw_ = kFrameShown, fh_ = 180;
  NvgImage image_;
  bool dirty_ = true;
  bool dragging_ = false;
};

MotionWindow::MotionWindow(nanogui::Widget* parent, std::optional<mpp::Motion> motion,
                           mpp::Image picture, int frame_width, int frame_height,
                           std::function<void(const std::optional<mpp::Motion>&)> apply)
    : nanogui::Window(parent, "Motion"),
      motion_(motion ? *motion : mpp::default_motion()),
      picture_(std::move(picture)),
      frame_width_(std::max(1, frame_width)),
      frame_height_(std::max(1, frame_height)),
      apply_(std::move(apply)) {
  using nanogui::Button;
  using nanogui::Label;
  set_modal(true);
  set_layout(new nanogui::BoxLayout(nanogui::Orientation::Vertical,
                                    nanogui::Alignment::Middle, 12, 8));
  nanogui::Widget* body = hrow(this, 14);
  stage_ = new MotionStage(body, this);

  nanogui::Widget* controls = new nanogui::Widget(body);
  controls->set_layout(new nanogui::BoxLayout(nanogui::Orientation::Vertical,
                                              nanogui::Alignment::Minimum, 0, 8));
  point_label_ = new Label(controls, "", "sans-bold", 18);
  nanogui::Widget* buttons = hrow(controls, 4);
  Button* add = new Button(buttons, "Add Point");
  add->set_callback([this] { add_point(); });
  Button* remove = new Button(buttons, "Delete Point");
  remove->set_callback([this] { delete_point(); });
  Button* centre = new Button(buttons, "Centre");
  centre->set_callback([this] {
    mpp::MotionPoint m = motion_.points[static_cast<std::size_t>(selected_)];
    m.x = m.y = 0.5;
    set_point(m);
  });

  const auto slider = [&](const char* name, nanogui::Slider*& s, Label*& value, float lo, float hi,
                          auto set) {
    nanogui::Widget* row = hrow(controls);
    Label* l = new Label(row, name);
    l->set_fixed_width(70);
    s = new nanogui::Slider(row);
    s->set_range({lo, hi});
    s->set_fixed_width(180);
    s->set_callback([this, set](float v) {
      mpp::MotionPoint m = motion_.points[static_cast<std::size_t>(selected_)];
      set(m, v);
      set_point(m);
    });
    value = new Label(row, "");
    value->set_fixed_width(56);
  };
  slider("Time", time_, time_value_, 0.0f, 1.0f, [](mpp::MotionPoint& m, float v) { m.t = v; });
  slider("Rotation", rotation_, rotation_value_, -360.0f, 360.0f,
         [](mpp::MotionPoint& m, float v) { m.rotation = std::round(v); });
  slider("Zoom", zoom_, zoom_value_, 0.0f, 400.0f, [](mpp::MotionPoint& m, float v) { m.zoom = std::round(v); });

  nanogui::Widget* preview_row = hrow(controls);
  Label* pl = new Label(preview_row, "Preview");
  pl->set_fixed_width(70);
  preview_ = new nanogui::Slider(preview_row);
  preview_->set_fixed_width(180);
  preview_->set_callback([this](float v) { set_preview(v); });
  Label* hint = new Label(controls,
                          "Drag the points on the left. A point outside the frame starts or "
                          "ends the clip off screen.");
  hint->set_font_size(13);
  hint->set_fixed_width(320);

  nanogui::Widget* bottom = hrow(this, 8);
  Button* none = new Button(bottom, "Remove Motion");
  none->set_callback([this] { remove_motion(); });
  Button* cancel = new Button(bottom, "Cancel");
  cancel->set_callback([this] { this->cancel(); });
  Button* ok = new Button(bottom, "OK");
  ok->set_callback([this] { this->ok(); });
  update();
}

void MotionWindow::select(int point) {
  selected_ = std::clamp(point, 0, static_cast<int>(motion_.points.size()) - 1);
  update();
}

void MotionWindow::add_point() {
  auto& points = motion_.points;
  const std::size_t at = static_cast<std::size_t>(selected_);
  mpp::MotionPoint m = points[at];
  if (at + 1 < points.size()) {
    m = mpp::motion_at(motion_, (points[at].t + points[at + 1].t) / 2);
  } else if (at > 0) {
    m = mpp::motion_at(motion_, (points[at - 1].t + points[at].t) / 2);
    points.insert(points.begin() + static_cast<std::ptrdiff_t>(at), m);
    update();
    return;
  } else {
    m.t = std::min(1.0, m.t + 0.5);
  }
  points.insert(points.begin() + static_cast<std::ptrdiff_t>(at + 1), m);
  selected_ = static_cast<int>(at + 1);
  update();
}

void MotionWindow::delete_point() {
  if (motion_.points.size() <= 1) return;
  motion_.points.erase(motion_.points.begin() + selected_);
  selected_ = std::min(selected_, static_cast<int>(motion_.points.size()) - 1);
  update();
}

void MotionWindow::set_point(const mpp::MotionPoint& point) {
  auto& points = motion_.points;
  const std::size_t i = static_cast<std::size_t>(selected_);
  mpp::MotionPoint m = point;
  // The first point is the clip's start and the last its end; the rest
  // stay between their neighbours.
  if (i == 0) m.t = 0;
  else if (i + 1 == points.size()) m.t = 1;
  else m.t = std::clamp(m.t, points[i - 1].t, points[i + 1].t);
  points[i] = m;
  removed_ = false;
  update();
}

void MotionWindow::set_preview(double t) {
  preview_t_ = std::clamp(t, 0.0, 1.0);
  update();
}

void MotionWindow::remove_motion() {
  removed_ = true;
  update();
}

nanogui::Vector2i MotionWindow::stage_point(double x, double y) const { return stage_->point(x, y); }

void MotionWindow::update() {
  stage_->refresh();
  const mpp::MotionPoint& m = motion_.points[static_cast<std::size_t>(selected_)];
  point_label_->set_caption(removed_ ? "No motion (OK removes it)"
                                     : "Point " + std::to_string(selected_ + 1) + " of " +
                                           std::to_string(motion_.points.size()));
  const bool ends = selected_ == 0 || selected_ + 1 == static_cast<int>(motion_.points.size());
  time_->set_enabled(!ends);
  time_->set_value(static_cast<float>(m.t));
  rotation_->set_value(static_cast<float>(m.rotation));
  zoom_->set_value(static_cast<float>(m.zoom));
  preview_->set_value(static_cast<float>(preview_t_));
  time_value_->set_caption(number(m.t * 100, "%"));
  rotation_value_->set_caption(number(m.rotation, "°"));
  zoom_value_->set_caption(number(m.zoom, "%"));
}

void MotionWindow::ok() {
  const std::optional<mpp::Motion> motion = removed_ ? std::nullopt : std::optional<mpp::Motion>(motion_);
  std::function<void(const std::optional<mpp::Motion>&)> apply = apply_;
  cancel();
  if (apply) apply(motion);
}

void MotionWindow::cancel() {
  std::function<void()> closed = std::move(closed_);
  dispose();
  if (closed) closed();
}

SpeedWindow::SpeedWindow(nanogui::Widget* parent, int speed, std::int64_t source_frames, mpp::Rate rate,
                         std::function<void(int)> apply)
    : nanogui::Window(parent, "Speed"), source_frames_(source_frames), rate_(rate), apply_(std::move(apply)) {
  set_modal(true);
  set_layout(new nanogui::BoxLayout(nanogui::Orientation::Vertical,
                                    nanogui::Alignment::Fill, 12, 8));
  nanogui::Widget* row = hrow(this);
  nanogui::Label* l = new nanogui::Label(row, "New speed");
  l->set_fixed_width(80);
  speed_ = new nanogui::IntBox<int>(row, speed);
  speed_->set_editable(true);
  speed_->set_units("%");
  speed_->set_min_max_values(1, 1000);
  speed_->set_spinnable(true);
  speed_->set_fixed_width(110);
  speed_->set_callback([this](int) { update(); });
  length_ = new nanogui::Label(this, "");
  nanogui::Widget* buttons = hrow(this, 8);
  nanogui::Button* cancel = new nanogui::Button(buttons, "Cancel");
  cancel->set_callback([this] { this->cancel(); });
  nanogui::Button* ok = new nanogui::Button(buttons, "OK");
  ok->set_callback([this] { this->ok(); });
  update();
}

void SpeedWindow::set_speed(int speed) {
  speed_->set_value(std::clamp(speed, 1, 1000));
  update();
}

void SpeedWindow::update() {
  const int speed = std::clamp(speed_->value(), 1, 1000);
  const auto frames = std::max<std::int64_t>(1, std::llround(source_frames_ * 100.0 / speed));
  length_->set_caption("New duration " + mpp::format_timecode(frames, rate_));
}

void SpeedWindow::ok() {
  const int speed = std::clamp(speed_->value(), 1, 1000);
  std::function<void(int)> apply = apply_;
  cancel();
  if (apply) apply(speed);
}

void SpeedWindow::cancel() {
  std::function<void()> closed = std::move(closed_);
  dispose();
  if (closed) closed();
}
