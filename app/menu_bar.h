#pragma once

#include <functional>
#include <string>
#include <vector>

#include <nanogui/widget.h>

// One entry in a pull-down menu. An empty label is a separator.
struct MenuItem {
  std::string label;
  std::string shortcut;  // shown on the right, e.g. "Cmd+I"
  std::function<void()> action;
  bool enabled = true;
  bool checked = false;
};

// A menu title and how to build its items. Items are built each time the
// menu opens, so their enabled and checked states are current.
struct Menu {
  std::string title;
  std::function<std::vector<MenuItem>()> items;
};

class MenuDropdown;

// The menu bar along the top of the screen, as on a classic Mac. A click on
// a title pulls its menu down; moving across the bar while one is open
// switches menus; a click on an item runs it.
class MenuBar : public nanogui::Widget {
 public:
  static constexpr int kHeight = 24;

  MenuBar(nanogui::Widget* parent, std::vector<Menu> menus);

  // Opens a menu by index (closing any other); -1 closes.
  void open(int index);
  void close() { open(-1); }
  int open_index() const { return open_; }
  // Whether a screen position is on the bar or the open menu.
  bool covers(const nanogui::Vector2i& p) const;
  // Runs the open menu's item with this label as a click would (for the
  // self-test). Returns false if there is no such enabled item.
  bool choose(const std::string& label);

  bool mouse_button_event(const nanogui::Vector2i& p, int button, bool down,
                          int modifiers) override;
  bool mouse_motion_event(const nanogui::Vector2i& p, const nanogui::Vector2i& rel,
                          int button, int modifiers) override;
  void draw(NVGcontext* ctx) override;

 private:
  int title_at(int x) const;  // index of the title at window x, or -1

  std::vector<Menu> menus_;
  std::vector<int> title_x_;  // left edge of each title, then the end
  int open_ = -1;
  nanogui::ref<MenuDropdown> dropdown_;
};
