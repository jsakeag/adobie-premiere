#pragma once

#include <functional>
#include <string>
#include <vector>

#include <nanogui/window.h>

#include "mpp/image.h"
#include "mpp/project.h"

class SequenceList;
namespace nanogui {
class ComboBox;
class Label;
template <typename T> class IntBox;
}  // namespace nanogui

struct SequenceWindowActions {
  std::function<void(std::vector<int> items)> set;   // the new list of bin item ids
  std::function<void()> add_selected;                  // add the Project window's selection
  std::function<void(const std::string& transition, int overlap)> place;  // at the edit line
  std::function<mpp::Image(int item)> thumbnail;
};

// The Sequence window (Premiere 2.0): bin items in the order they should
// play, a rough cut before the Construction window. Items come from the
// Project window (dragged in, or Add); Remove, Up and Down rearrange them;
// Place lays them out from the edit line with cuts or a transition.
class SequenceWindow : public nanogui::Window {
 public:
  SequenceWindow(nanogui::Widget* parent, SequenceWindowActions actions);

  // Brings the list up to date with the project.
  void refresh(const mpp::Project& project);

  int selected() const;  // row index, or -1
  void select(int row);
  // The row a screen position would insert before, or -1 when outside the
  // list (for dropping bin items).
  int drop_row(const nanogui::Vector2i& screen) const;
  // Sets the transition (empty for cuts) and overlap as the controls would.
  void choose(const std::string& transition, int overlap);
  void place();
  void remove();
  void move(int steps);

 private:
  SequenceWindowActions actions_;
  std::vector<int> items_;
  SequenceList* list_ = nullptr;
  nanogui::ComboBox* transition_ = nullptr;
  nanogui::IntBox<int>* overlap_ = nullptr;
  nanogui::Label* total_ = nullptr;
};
