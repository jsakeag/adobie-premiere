#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include <nanogui/window.h>

#include "frame_view.h"
#include "mpp/project.h"

namespace nanogui {
class Label;
class VScrollPanel;
}  // namespace nanogui

class BinList;

// What the Project window's controls do; the app implements each.
struct ProjectWindowActions {
  std::function<void()> import;
  std::function<void(int id)> select;
  std::function<void(int id)> open;  // double-click: show in the Clip window
  std::function<void()> clear;       // remove the selected item
  // A row dragged out of the list, in screen coordinates. `released` is
  // true for the final position.
  std::function<void(int id, nanogui::Vector2i at, bool released)> drag;
  // A small picture for a row, made on first draw.
  std::function<mpp::Image(int id)> thumbnail;
};

// The Project window: every imported file with a thumbnail, its kind,
// size and duration.
class ProjectWindow : public nanogui::Window {
 public:
  ProjectWindow(nanogui::Widget* parent, ProjectWindowActions actions);

  // Brings the rows up to date with the project. Thumbnails are kept for
  // items whose marks haven't moved.
  void refresh(const mpp::Project& project, int selected);

  // Where an item's row is, in screen coordinates (for the self-test).
  nanogui::Vector2i row_center(int id) const;

 private:
  ProjectWindowActions actions_;
  BinList* list_ = nullptr;
  nanogui::VScrollPanel* scroll_ = nullptr;
  nanogui::Label* count_ = nullptr;
};
