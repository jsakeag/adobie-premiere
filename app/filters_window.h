#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <nanogui/window.h>

#include "mpp/filters.h"
#include "mpp/image.h"

class FilterList;
class FrameView;
namespace nanogui {
class Button;
class ComboBox;
class Label;
class Slider;
}  // namespace nanogui

// Clip > Filters: the clip's filters in order, with Add, Remove, Up and
// Down, and each setting's value at the clip's first frame (Start) and last
// (End). For a picture it previews the first and last frames with every
// filter applied. OK calls `apply` with the whole list.
class FiltersWindow : public nanogui::Window {
 public:
  static constexpr int kMaxParams = 5;

  // `first` and `last` are the clip's first and last pictures before
  // filters, with their frame numbers; empty for a sound clip, which gets
  // the audio filters instead.
  FiltersWindow(nanogui::Widget* parent, const std::string& clip_name,
                std::vector<mpp::FilterInstance> filters, mpp::Image first, std::int64_t first_frame,
                mpp::Image last, std::int64_t last_frame,
                std::function<void(const std::vector<mpp::FilterInstance>&)> apply);

  const std::vector<mpp::FilterInstance>& filters() const { return filters_; }
  int selected() const { return selected_; }

  // What the controls do, for the self-test as well.
  void add(const std::string& id);
  void remove();
  void move(int steps);  // -1 up, +1 down
  void select(int index);
  void set_value(std::size_t param, bool end, double value);
  void ok();
  void cancel();
  void set_closed_callback(std::function<void()> closed) { closed_ = std::move(closed); }

 private:
  struct ParamRow {
    nanogui::Widget* row = nullptr;
    nanogui::Label* name = nullptr;
    nanogui::Slider* start = nullptr;
    nanogui::Label* start_value = nullptr;
    nanogui::Slider* end = nullptr;
    nanogui::Label* end_value = nullptr;
  };

  bool sound() const { return first_.empty(); }
  const std::vector<mpp::FilterParam>* params_of(const mpp::FilterInstance& f) const;
  void update();

  std::function<void()> closed_;
  std::vector<mpp::FilterInstance> filters_;
  int selected_ = -1;
  mpp::Image first_, last_;
  std::int64_t first_frame_ = 0, last_frame_ = 0;
  std::function<void(const std::vector<mpp::FilterInstance>&)> apply_;
  std::vector<std::string> ids_;  // what the Add menu offers, in order

  FilterList* list_ = nullptr;
  nanogui::Label* heading_ = nullptr;
  std::array<ParamRow, kMaxParams> rows_;
  nanogui::ComboBox* add_menu_ = nullptr;
  nanogui::Button* remove_ = nullptr;
  nanogui::Button* up_ = nullptr;
  nanogui::Button* down_ = nullptr;
  FrameView* first_view_ = nullptr;
  FrameView* last_view_ = nullptr;
};
