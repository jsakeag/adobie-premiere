#include "make_windows.h"

#include <string>
#include <vector>

#include <nanogui/button.h>
#include <nanogui/combobox.h>
#include <nanogui/label.h>
#include <nanogui/layout.h>
#include <nanogui/opengl.h>

namespace {

nanogui::Widget* row(nanogui::Widget* parent, const char* label) {
  nanogui::Widget* w = new nanogui::Widget(parent);
  w->set_layout(new nanogui::BoxLayout(nanogui::Orientation::Horizontal,
                                       nanogui::Alignment::Middle, 0, 8));
  if (label) {
    nanogui::Label* l = new nanogui::Label(w, label);
    l->set_fixed_width(70);
  }
  return w;
}

}  // namespace

ExportWindow::ExportWindow(nanogui::Widget* parent, bool has_work_area,
                           std::function<void(mpp::MovieFormat, bool)> apply)
    : nanogui::Window(parent, "Make Movie"), apply_(std::move(apply)), has_work_area_(has_work_area) {
  set_modal(true);
  set_layout(new nanogui::BoxLayout(nanogui::Orientation::Vertical,
                                    nanogui::Alignment::Fill, 12, 8));
  format_ = new nanogui::ComboBox(row(this, "Format"),
                                  {mpp::movie_format_name(mpp::MovieFormat::H264),
                                   mpp::movie_format_name(mpp::MovieFormat::Animation)});
  format_->set_fixed_width(280);
  std::vector<std::string> ranges = {"Entire program"};
  if (has_work_area) ranges.insert(ranges.begin(), "Work area");
  range_ = new nanogui::ComboBox(row(this, "Range"), ranges);
  range_->set_fixed_width(280);

  nanogui::Widget* buttons = row(this, nullptr);
  nanogui::Button* cancel = new nanogui::Button(buttons, "Cancel");
  cancel->set_callback([this] { this->cancel(); });
  nanogui::Button* ok = new nanogui::Button(buttons, "Save...");
  ok->set_callback([this] { this->ok(); });
}

void ExportWindow::choose(mpp::MovieFormat format, bool whole_program) {
  format_->set_selected_index(format == mpp::MovieFormat::H264 ? 0 : 1);
  if (has_work_area_) range_->set_selected_index(whole_program ? 1 : 0);
}

void ExportWindow::ok() {
  const mpp::MovieFormat format =
      format_->selected_index() == 0 ? mpp::MovieFormat::H264 : mpp::MovieFormat::Animation;
  const bool whole = !has_work_area_ || range_->selected_index() == 1;
  std::function<void(mpp::MovieFormat, bool)> apply = apply_;
  cancel();
  if (apply) apply(format, whole);
}

void ExportWindow::cancel() {
  std::function<void()> closed = std::move(closed_);
  dispose();
  if (closed) closed();
}

FullScreenView::FullScreenView(nanogui::Widget* parent, std::function<void()> stop)
    : nanogui::Widget(parent), stop_(std::move(stop)) {
  view_ = new FrameView(this);
  fill_parent();
}

void FullScreenView::fill_parent() {
  if (!parent()) return;
  set_position({0, 0});
  set_fixed_size(parent()->size());
  set_size(parent()->size());
  view_->set_position({0, 0});
  view_->set_fixed_size(parent()->size());
  view_->set_size(parent()->size());
}

bool FullScreenView::mouse_button_event(const nanogui::Vector2i&, int, bool down, int) {
  if (down && stop_) stop_();
  return true;
}

void FullScreenView::draw(NVGcontext* ctx) {
  nvgBeginPath(ctx);
  nvgRect(ctx, m_pos.x(), m_pos.y(), m_size.x(), m_size.y());
  nvgFillColor(ctx, nvgRGB(0, 0, 0));
  nvgFill(ctx);
  nanogui::Widget::draw(ctx);
}
