#include "filters_window.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <nanogui/button.h>
#include <nanogui/combobox.h>
#include <nanogui/label.h>
#include <nanogui/layout.h>
#include <nanogui/opengl.h>
#include <nanogui/screen.h>
#include <nanogui/slider.h>

#include "frame_view.h"

using nanogui::Vector2i;

namespace {

constexpr int kListWidth = 214;
constexpr int kListRow = 24;
constexpr int kListRows = 8;

nanogui::Widget* box(nanogui::Widget* parent, nanogui::Orientation o, int spacing,
                     nanogui::Alignment a = nanogui::Alignment::Middle) {
  nanogui::Widget* w = new nanogui::Widget(parent);
  w->set_layout(new nanogui::BoxLayout(o, a, 0, spacing));
  return w;
}

std::string format_value(double v, const mpp::FilterParam& p) {
  char text[32];
  if (p.whole) std::snprintf(text, sizeof(text), "%d%s", static_cast<int>(std::lround(v)), p.unit.c_str());
  else std::snprintf(text, sizeof(text), "%.1f%s", v, p.unit.c_str());
  return text;
}

}  // namespace

// The clip's filters, one row each; a click selects.
class FilterList : public nanogui::Widget {
 public:
  FilterList(nanogui::Widget* parent, std::function<void(int)> select)
      : nanogui::Widget(parent), select_(std::move(select)) {
    set_fixed_size({kListWidth, kListRow * kListRows});
  }

  void set(std::vector<std::string> names, int selected) {
    names_ = std::move(names);
    selected_ = selected;
  }

  Vector2i row_center(int index) const {
    return absolute_position() + Vector2i(kListWidth / 2, index * kListRow + kListRow / 2);
  }

  bool mouse_button_event(const Vector2i& p, int button, bool down, int) override {
    if (button == GLFW_MOUSE_BUTTON_LEFT && down) {
      const int index = (p.y() - m_pos.y()) / kListRow;
      if (index >= 0 && index < static_cast<int>(names_.size()) && select_) select_(index);
    }
    return true;
  }

  void draw(NVGcontext* ctx) override {
    const float x = m_pos.x(), y = m_pos.y();
    nvgBeginPath(ctx);
    nvgRect(ctx, x, y, m_size.x(), m_size.y());
    nvgFillColor(ctx, nvgRGB(30, 30, 32));
    nvgFill(ctx);
    nvgFontFace(ctx, "sans");
    nvgFontSize(ctx, 15);
    nvgTextAlign(ctx, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    for (std::size_t i = 0; i < names_.size(); ++i) {
      const float top = y + static_cast<float>(i) * kListRow;
      if (static_cast<int>(i) == selected_) {
        nvgBeginPath(ctx);
        nvgRect(ctx, x, top, m_size.x(), kListRow);
        nvgFillColor(ctx, nvgRGBA(70, 110, 180, 200));
        nvgFill(ctx);
      }
      nvgFillColor(ctx, nvgRGB(230, 230, 230));
      const std::string label = std::to_string(i + 1) + ".  " + names_[i];
      nvgText(ctx, x + 8, top + kListRow / 2.0f, label.c_str(), nullptr);
    }
    if (names_.empty()) {
      nvgFillColor(ctx, nvgRGB(140, 140, 140));
      nvgText(ctx, x + 8, y + kListRow / 2.0f, "No filters", nullptr);
    }
  }

 private:
  std::function<void(int)> select_;
  std::vector<std::string> names_;
  int selected_ = -1;
};

FiltersWindow::FiltersWindow(nanogui::Widget* parent, const std::string& clip_name,
                             std::vector<mpp::FilterInstance> filters, mpp::Image first,
                             std::int64_t first_frame, mpp::Image last, std::int64_t last_frame,
                             std::function<void(const std::vector<mpp::FilterInstance>&)> apply)
    : nanogui::Window(parent, "Filters: " + clip_name),
      filters_(std::move(filters)),
      selected_(filters_.empty() ? -1 : 0),
      first_(std::move(first)),
      last_(std::move(last)),
      first_frame_(first_frame),
      last_frame_(last_frame),
      apply_(std::move(apply)) {
  using nanogui::Alignment;
  using nanogui::Button;
  using nanogui::Label;
  using nanogui::Orientation;
  set_modal(true);
  set_layout(new nanogui::BoxLayout(Orientation::Vertical, Alignment::Middle, 12, 10));

  nanogui::Widget* top = box(this, Orientation::Horizontal, 14, Alignment::Minimum);
  nanogui::Widget* left = box(top, Orientation::Vertical, 6, Alignment::Fill);
  list_ = new FilterList(left, [this](int i) { select(i); });

  // Add menu and list buttons.
  std::vector<std::string> names;
  if (sound()) {
    for (const auto& f : mpp::audio_filters()) ids_.push_back(f.id), names.push_back(f.name);
  } else {
    for (const auto& f : mpp::video_filters()) ids_.push_back(f.id), names.push_back(f.name);
  }
  add_menu_ = new nanogui::ComboBox(left, names);
  add_menu_->set_fixed_width(kListWidth);
  nanogui::Widget* edit_row = box(left, Orientation::Horizontal, 4);
  Button* add_button = new Button(edit_row, "Add");
  add_button->set_callback([this] {
    const int i = add_menu_->selected_index();
    if (i >= 0 && i < static_cast<int>(ids_.size())) add(ids_[static_cast<std::size_t>(i)]);
  });
  remove_ = new Button(edit_row, "Remove");
  remove_->set_callback([this] { remove(); });
  up_ = new Button(edit_row, "Up");
  up_->set_callback([this] { move(-1); });
  down_ = new Button(edit_row, "Down");
  down_->set_callback([this] { move(1); });

  // Settings of the selected filter: fixed rows, shown as needed, so
  // nothing is rebuilt inside a callback.
  nanogui::Widget* right = box(top, Orientation::Vertical, 6, Alignment::Minimum);
  heading_ = new Label(right, "", "sans-bold", 18);
  nanogui::Widget* header = box(right, Orientation::Horizontal, 8);
  for (const auto& [text, width] : {std::pair{"", 100}, std::pair{"Start (first frame)", 196},
                                    std::pair{"End (last frame)", 196}}) {
    Label* label = new Label(header, text);
    label->set_fixed_width(width);
  }
  for (std::size_t i = 0; i < rows_.size(); ++i) {
    ParamRow& r = rows_[i];
    r.row = box(right, Orientation::Horizontal, 8);
    r.row->set_fixed_height(28);
    r.name = new Label(r.row, "");
    r.name->set_fixed_width(100);
    const auto slider = [&](nanogui::Slider*& s, Label*& value, bool end) {
      s = new nanogui::Slider(r.row);
      s->set_fixed_width(130);
      s->set_callback([this, i, end](float v) { set_value(i, end, v); });
      value = new Label(r.row, "");
      value->set_fixed_width(58);
      value->set_font_size(14);
    };
    slider(r.start, r.start_value, false);
    slider(r.end, r.end_value, true);
  }

  if (!sound()) {
    nanogui::Widget* previews = box(this, Orientation::Horizontal, 16);
    const auto preview = [&](const char* title, FrameView*& view) {
      nanogui::Widget* col = box(previews, Orientation::Vertical, 4);
      new Label(col, title, "sans-bold");
      view = new FrameView(col);
      view->set_fixed_size({200, 150});
    };
    preview("First frame", first_view_);
    preview("Last frame", last_view_);
  }

  nanogui::Widget* buttons = box(this, Orientation::Horizontal, 8);
  Button* cancel = new Button(buttons, "Cancel");
  cancel->set_callback([this] { this->cancel(); });
  Button* ok = new Button(buttons, "OK");
  ok->set_callback([this] { this->ok(); });
  update();
}

const std::vector<mpp::FilterParam>* FiltersWindow::params_of(const mpp::FilterInstance& f) const {
  if (const mpp::FilterDef* v = mpp::find_video_filter(f.id)) return &v->params;
  if (const mpp::AudioFilterDef* a = mpp::find_audio_filter(f.id)) return &a->params;
  return nullptr;
}

void FiltersWindow::add(const std::string& id) {
  filters_.push_back(mpp::default_instance(id));
  selected_ = static_cast<int>(filters_.size()) - 1;
  update();
}

void FiltersWindow::remove() {
  if (selected_ < 0) return;
  filters_.erase(filters_.begin() + selected_);
  selected_ = std::min(selected_, static_cast<int>(filters_.size()) - 1);
  update();
}

void FiltersWindow::move(int steps) {
  const int to = selected_ + steps;
  if (selected_ < 0 || to < 0 || to >= static_cast<int>(filters_.size())) return;
  std::swap(filters_[static_cast<std::size_t>(selected_)], filters_[static_cast<std::size_t>(to)]);
  selected_ = to;
  update();
}

void FiltersWindow::select(int index) {
  if (index < 0 || index >= static_cast<int>(filters_.size())) return;
  selected_ = index;
  update();
}

void FiltersWindow::set_value(std::size_t param, bool end, double value) {
  if (selected_ < 0) return;
  mpp::FilterInstance& f = filters_[static_cast<std::size_t>(selected_)];
  const std::vector<mpp::FilterParam>* params = params_of(f);
  if (!params || param >= params->size()) return;
  const mpp::FilterParam& p = (*params)[param];
  value = std::clamp(value, p.min, p.max);
  if (p.whole) value = std::round(value);
  (end ? f.end : f.start)[param] = value;
  update();
}

void FiltersWindow::update() {
  std::vector<std::string> names;
  for (const mpp::FilterInstance& f : filters_) {
    const mpp::FilterDef* v = mpp::find_video_filter(f.id);
    const mpp::AudioFilterDef* a = mpp::find_audio_filter(f.id);
    names.push_back(v ? v->name : a ? a->name : f.id);
  }
  list_->set(names, selected_);
  remove_->set_enabled(selected_ >= 0);
  up_->set_enabled(selected_ > 0);
  down_->set_enabled(selected_ >= 0 && selected_ + 1 < static_cast<int>(filters_.size()));

  const mpp::FilterInstance* f = selected_ >= 0 ? &filters_[static_cast<std::size_t>(selected_)] : nullptr;
  const std::vector<mpp::FilterParam>* params = f ? params_of(*f) : nullptr;
  heading_->set_caption(!f ? "Add a filter from the menu"
                        : params && params->empty() ? names[static_cast<std::size_t>(selected_)] + " has no settings"
                                                    : names[static_cast<std::size_t>(selected_)]);
  for (std::size_t i = 0; i < rows_.size(); ++i) {
    ParamRow& r = rows_[i];
    const bool used = params && i < params->size();
    for (nanogui::Widget* w : std::initializer_list<nanogui::Widget*>{r.name, r.start, r.start_value, r.end, r.end_value}) {
      w->set_visible(used);
    }
    if (!used) continue;
    const mpp::FilterParam& p = (*params)[i];
    r.name->set_caption(p.name);
    for (nanogui::Slider* s : {r.start, r.end}) s->set_range({static_cast<float>(p.min), static_cast<float>(p.max)});
    r.start->set_value(static_cast<float>(f->start[i]));
    r.end->set_value(static_cast<float>(f->end[i]));
    r.start_value->set_caption(format_value(f->start[i], p));
    r.end_value->set_caption(format_value(f->end[i], p));
  }
  if (!sound()) {
    first_view_->set_frame(mpp::apply_video_filters(first_, filters_, 0.0, first_frame_));
    last_view_->set_frame(mpp::apply_video_filters(last_, filters_, 1.0, last_frame_));
  }
  // Rows that just appeared need positions and sizes. Every control has a
  // fixed size, so this moves nothing else.
  if (nanogui::Screen* s = screen()) perform_layout(s->nvg_context());
}

void FiltersWindow::ok() {
  const std::vector<mpp::FilterInstance> filters = filters_;
  std::function<void(const std::vector<mpp::FilterInstance>&)> apply = apply_;
  cancel();
  if (apply) apply(filters);
}

void FiltersWindow::cancel() {
  std::function<void()> closed = std::move(closed_);
  dispose();
  if (closed) closed();
}
