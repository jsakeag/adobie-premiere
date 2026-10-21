#include "clip_window.h"

#include <algorithm>
#include <utility>

#include <nanogui/button.h>
#include <nanogui/icons.h>
#include <nanogui/label.h>
#include <nanogui/layout.h>
#include <nanogui/opengl.h>
#include <nanogui/textbox.h>

#include "frame_view.h"

using nanogui::Vector2i;

namespace {

constexpr int kViewW = 320;
constexpr int kViewH = 240;

}  // namespace

// A horizontal bar from the start of the source to its end, with the in-out
// range shaded and a line at the current position. Clicking or dragging
// seeks.
class Scrubber : public nanogui::Widget {
 public:
  Scrubber(nanogui::Widget* parent, std::function<void(std::int64_t)> seek)
      : nanogui::Widget(parent), seek_(std::move(seek)) {
    set_fixed_size({kViewW, 22});
  }

  void set_state(std::int64_t position, std::int64_t in, std::int64_t out, std::int64_t length) {
    position_ = position;
    in_ = in;
    out_ = out;
    length_ = std::max<std::int64_t>(1, length);
  }

  float x_of(std::int64_t frame) const {
    return m_pos.x() + 4 + (m_size.x() - 8) * static_cast<float>(frame) / length_;
  }

  bool mouse_button_event(const Vector2i& p, int button, bool down, int) override {
    if (button == GLFW_MOUSE_BUTTON_LEFT && down) seek_to(p.x());
    return true;
  }

  bool mouse_drag_event(const Vector2i& p, const Vector2i&, int, int) override {
    seek_to(p.x());
    return true;
  }

  void draw(NVGcontext* ctx) override {
    const float x = m_pos.x(), y = m_pos.y(), w = m_size.x(), h = m_size.y();
    nvgBeginPath(ctx);
    nvgRoundedRect(ctx, x, y + 6, w, h - 12, 3);
    nvgFillColor(ctx, nvgRGB(40, 40, 40));
    nvgFill(ctx);
    nvgBeginPath(ctx);
    nvgRect(ctx, x_of(in_), y + 6, std::max(1.0f, x_of(out_) - x_of(in_)), h - 12);
    nvgFillColor(ctx, nvgRGB(90, 130, 200));
    nvgFill(ctx);
    // In and out brackets.
    nvgStrokeColor(ctx, nvgRGB(230, 230, 230));
    nvgStrokeWidth(ctx, 1.5f);
    for (const auto& [frame, sign] : {std::pair{in_, 1.0f}, std::pair{out_, -1.0f}}) {
      const float bx = x_of(frame);
      nvgBeginPath(ctx);
      nvgMoveTo(ctx, bx + 4 * sign, y + 3);
      nvgLineTo(ctx, bx, y + 3);
      nvgLineTo(ctx, bx, y + h - 3);
      nvgLineTo(ctx, bx + 4 * sign, y + h - 3);
      nvgStroke(ctx);
    }
    const float px = x_of(position_);
    nvgBeginPath(ctx);
    nvgMoveTo(ctx, px, y);
    nvgLineTo(ctx, px, y + h);
    nvgStrokeColor(ctx, nvgRGB(255, 60, 60));
    nvgStrokeWidth(ctx, 2.0f);
    nvgStroke(ctx);
  }

 private:
  void seek_to(int window_x) {
    const float t = (window_x - m_pos.x() - 4) / static_cast<float>(m_size.x() - 8);
    const auto frame = static_cast<std::int64_t>(std::clamp(t, 0.0f, 1.0f) * length_);
    if (seek_) seek_(std::min(frame, length_ - 1));
  }

  std::function<void(std::int64_t)> seek_;
  std::int64_t position_ = 0, in_ = 0, out_ = 0, length_ = 1;
};

ClipWindow::ClipWindow(nanogui::Widget* parent, ClipWindowActions actions)
    : nanogui::Window(parent, "Clip"), actions_(std::move(actions)) {
  using nanogui::Alignment;
  using nanogui::BoxLayout;
  using nanogui::Button;
  using nanogui::Orientation;
  set_layout(new BoxLayout(Orientation::Vertical, Alignment::Middle, 6, 4));

  view_ = new FrameView(this);
  view_->set_fixed_size({kViewW, kViewH});
  scrubber_ = new Scrubber(this, [this](std::int64_t f) { if (actions_.seek) actions_.seek(f); });

  nanogui::Widget* transport = new nanogui::Widget(this);
  transport->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Middle, 0, 4));
  const auto icon_button = [&](int icon, std::function<void()> callback, const char* tip) {
    Button* b = new Button(transport, "", icon);
    b->set_fixed_size({34, 26});
    b->set_tooltip(tip);
    b->set_callback(std::move(callback));
    return b;
  };
  icon_button(FA_FAST_BACKWARD, [this] { if (actions_.go_to_in) actions_.go_to_in(); }, "Go to In");
  icon_button(FA_STEP_BACKWARD, [this] { if (actions_.step) actions_.step(-1); }, "Back one frame");
  play_ = icon_button(FA_PLAY, [this] { if (actions_.toggle_play) actions_.toggle_play(); }, "Play (Space)");
  icon_button(FA_STEP_FORWARD, [this] { if (actions_.step) actions_.step(1); }, "Forward one frame");
  icon_button(FA_FAST_FORWARD, [this] { if (actions_.go_to_out) actions_.go_to_out(); }, "Go to Out");
  position_ = new nanogui::TextBox(transport, "00:00:00:00");
  position_->set_fixed_size({110, 26});
  position_->set_editable(true);
  position_->set_tooltip("Type a timecode and press Return to go there");

  nanogui::Widget* marks = new nanogui::Widget(this);
  marks->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Middle, 0, 4));
  Button* in = new Button(marks, "Mark In");
  in->set_tooltip("I");
  in->set_callback([this] { if (actions_.mark_in) actions_.mark_in(); });
  Button* out = new Button(marks, "Mark Out");
  out->set_tooltip("O");
  out->set_callback([this] { if (actions_.mark_out) actions_.mark_out(); });
  marks_ = new nanogui::Label(this, "");
  marks_->set_fixed_width(kViewW);
  marks_->set_font_size(14);
}

void ClipWindow::show(const ClipWindowState& state, const mpp::Image& frame) {
  state_ = state;
  set_title(state.has_item ? "Clip: " + state.name : "Clip");
  if (!state.has_item) {
    view_->clear();
  } else if (!frame.empty()) {
    view_->set_frame(frame);
  }
  scrubber_->set_state(state.position, state.in, state.out, state.length);
  play_->set_icon(state.playing ? FA_STOP : FA_PLAY);
  if (!position_->focused()) {
    position_->set_value(mpp::format_timecode(state.position, state.rate));
  }
  // The text box parses what was typed against the current rate.
  const mpp::Rate rate = state.rate;
  position_->set_callback([this, rate](const std::string& text) {
    const auto frame = mpp::parse_timecode(text, rate);
    if (frame && actions_.seek) actions_.seek(*frame);
    return frame.has_value();
  });
  marks_->set_caption(state.has_item
                          ? "In " + mpp::format_timecode(state.in, state.rate) + "   Out " +
                                mpp::format_timecode(state.out, state.rate) + "   Dur " +
                                mpp::format_timecode(state.out - state.in, state.rate)
                          : "Double-click an item in the Project window.");
}

nanogui::Vector2i ClipWindow::scrub_point(std::int64_t position) const {
  return {static_cast<int>(scrubber_->x_of(position)) + scrubber_->parent()->absolute_position().x(),
          scrubber_->absolute_position().y() + scrubber_->height() / 2};
}

bool ClipWindow::mouse_button_event(const Vector2i& p, int button, bool down, int modifiers) {
  if (down && actions_.focus) actions_.focus();
  return nanogui::Window::mouse_button_event(p, button, down, modifiers);
}
