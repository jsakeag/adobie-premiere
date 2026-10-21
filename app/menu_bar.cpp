#include "menu_bar.h"

#include <algorithm>
#include <utility>

#include <nanogui/opengl.h>
#include <nanogui/screen.h>
#include <nanogui/theme.h>

using nanogui::Vector2i;

namespace {

constexpr float kFontSize = 15.0f;
constexpr int kItemHeight = 22;
constexpr int kSeparatorHeight = 9;
constexpr int kTitlePad = 12;

}  // namespace

// The open menu's list. A child of the screen, kept alive by the bar and
// moved to the front each time it opens.
class MenuDropdown : public nanogui::Widget {
 public:
  explicit MenuDropdown(MenuBar* bar) : nanogui::Widget(nullptr), bar_(bar) {}

  void set_items(std::vector<MenuItem> items, NVGcontext* ctx) {
    items_ = std::move(items);
    hover_ = -1;
    int width = 160, height = 6;
    nvgFontFace(ctx, "sans");
    nvgFontSize(ctx, kFontSize);
    for (const MenuItem& item : items_) {
      height += item.label.empty() ? kSeparatorHeight : kItemHeight;
      const float label = nvgTextBounds(ctx, 0, 0, item.label.c_str(), nullptr, nullptr);
      const float shortcut = nvgTextBounds(ctx, 0, 0, item.shortcut.c_str(), nullptr, nullptr);
      width = std::max(width, static_cast<int>(label + shortcut) + 70);
    }
    set_size({width, height});
  }

  const std::vector<MenuItem>& items() const { return items_; }

  // Runs an item after closing the menu, so the action may open another.
  void run(std::size_t index) {
    if (index >= items_.size() || !items_[index].enabled || items_[index].label.empty()) return;
    std::function<void()> action = items_[index].action;
    bar_->close();
    if (action) action();
  }

  bool mouse_button_event(const Vector2i& p, int button, bool down, int) override {
    if (button != GLFW_MOUSE_BUTTON_LEFT || down) return true;
    const int index = item_at(p.y() - m_pos.y());
    if (index >= 0) run(static_cast<std::size_t>(index));
    return true;
  }

  bool mouse_motion_event(const Vector2i& p, const Vector2i&, int, int) override {
    hover_ = item_at(p.y() - m_pos.y());
    return true;
  }

  bool mouse_enter_event(const Vector2i& p, bool enter) override {
    if (!enter) hover_ = -1;
    return nanogui::Widget::mouse_enter_event(p, enter);
  }

  void draw(NVGcontext* ctx) override {
    const float x = m_pos.x(), y = m_pos.y(), w = m_size.x(), h = m_size.y();
    nvgSave(ctx);
    nvgResetScissor(ctx);
    // Drop shadow, then a light panel like a System 7 menu.
    nvgBeginPath(ctx);
    nvgRect(ctx, x + 3, y + 3, w, h);
    nvgFillColor(ctx, nvgRGBA(0, 0, 0, 90));
    nvgFill(ctx);
    nvgBeginPath(ctx);
    nvgRect(ctx, x, y, w, h);
    nvgFillColor(ctx, nvgRGB(236, 236, 236));
    nvgFill(ctx);
    nvgStrokeColor(ctx, nvgRGB(20, 20, 20));
    nvgStrokeWidth(ctx, 1.0f);
    nvgStroke(ctx);

    nvgFontFace(ctx, "sans");
    nvgFontSize(ctx, kFontSize);
    float top = y + 3;
    for (std::size_t i = 0; i < items_.size(); ++i) {
      const MenuItem& item = items_[i];
      if (item.label.empty()) {
        nvgBeginPath(ctx);
        nvgMoveTo(ctx, x + 1, top + kSeparatorHeight / 2.0f);
        nvgLineTo(ctx, x + w - 1, top + kSeparatorHeight / 2.0f);
        nvgStrokeColor(ctx, nvgRGB(160, 160, 160));
        nvgStroke(ctx);
        top += kSeparatorHeight;
        continue;
      }
      const bool lit = static_cast<int>(i) == hover_ && item.enabled;
      if (lit) {
        nvgBeginPath(ctx);
        nvgRect(ctx, x + 1, top, w - 2, kItemHeight);
        nvgFillColor(ctx, nvgRGB(20, 20, 20));
        nvgFill(ctx);
      }
      const NVGcolor ink = !item.enabled ? nvgRGB(150, 150, 150)
                           : lit         ? nvgRGB(255, 255, 255)
                                         : nvgRGB(10, 10, 10);
      nvgFillColor(ctx, ink);
      nvgTextAlign(ctx, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
      if (item.checked) nvgText(ctx, x + 7, top + kItemHeight / 2.0f, "\xE2\x9C\x93", nullptr);
      nvgText(ctx, x + 22, top + kItemHeight / 2.0f, item.label.c_str(), nullptr);
      nvgTextAlign(ctx, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
      nvgText(ctx, x + w - 10, top + kItemHeight / 2.0f, item.shortcut.c_str(), nullptr);
      top += kItemHeight;
    }
    nvgRestore(ctx);
  }

 private:
  int item_at(int local_y) const {
    int top = 3;
    for (std::size_t i = 0; i < items_.size(); ++i) {
      const int h = items_[i].label.empty() ? kSeparatorHeight : kItemHeight;
      if (local_y >= top && local_y < top + h) return static_cast<int>(i);
      top += h;
    }
    return -1;
  }

  MenuBar* bar_;
  std::vector<MenuItem> items_;
  int hover_ = -1;
};

MenuBar::MenuBar(nanogui::Widget* parent, std::vector<Menu> menus)
    : nanogui::Widget(parent), menus_(std::move(menus)) {
  dropdown_ = new MenuDropdown(this);
}

int MenuBar::title_at(int x) const {
  for (std::size_t i = 0; i + 1 < title_x_.size(); ++i) {
    if (x >= title_x_[i] && x < title_x_[i + 1]) return static_cast<int>(i);
  }
  return -1;
}

void MenuBar::open(int index) {
  nanogui::Screen* s = screen();
  if (dropdown_->parent()) s->remove_child(dropdown_.get());
  open_ = index;
  if (index < 0 || index >= static_cast<int>(menus_.size())) {
    open_ = -1;
    return;
  }
  dropdown_->set_items(menus_[index].items(), s->nvg_context());
  const int x = title_x_.size() > static_cast<std::size_t>(index) ? title_x_[index] : 0;
  dropdown_->set_position({x, kHeight});
  s->add_child(dropdown_.get());  // last, so it draws on top
}

bool MenuBar::covers(const Vector2i& p) const {
  if (contains(p)) return true;
  return open_ >= 0 && dropdown_->contains(p);
}

bool MenuBar::choose(const std::string& label) {
  if (open_ < 0) return false;
  const std::vector<MenuItem>& items = dropdown_->items();
  for (std::size_t i = 0; i < items.size(); ++i) {
    if (items[i].label == label && items[i].enabled) {
      dropdown_->run(i);
      return true;
    }
  }
  return false;
}

bool MenuBar::mouse_button_event(const Vector2i& p, int button, bool down, int) {
  if (button != GLFW_MOUSE_BUTTON_LEFT || !down) return true;
  const int index = title_at(p.x() - m_pos.x());
  open(index == open_ ? -1 : index);
  return true;
}

bool MenuBar::mouse_motion_event(const Vector2i& p, const Vector2i&, int, int) {
  if (open_ < 0) return false;
  const int index = title_at(p.x() - m_pos.x());
  if (index >= 0 && index != open_) open(index);
  return true;
}

void MenuBar::draw(NVGcontext* ctx) {
  const float x = m_pos.x(), y = m_pos.y(), w = m_size.x(), h = m_size.y();
  nvgBeginPath(ctx);
  nvgRect(ctx, x, y, w, h);
  nvgFillColor(ctx, nvgRGB(236, 236, 236));
  nvgFill(ctx);
  nvgBeginPath(ctx);
  nvgMoveTo(ctx, x, y + h - 0.5f);
  nvgLineTo(ctx, x + w, y + h - 0.5f);
  nvgStrokeColor(ctx, nvgRGB(20, 20, 20));
  nvgStrokeWidth(ctx, 1.0f);
  nvgStroke(ctx);

  nvgFontFace(ctx, "sans-bold");
  nvgFontSize(ctx, kFontSize);
  nvgTextAlign(ctx, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
  title_x_.clear();
  float left = 10;
  for (std::size_t i = 0; i < menus_.size(); ++i) {
    const float width =
        nvgTextBounds(ctx, 0, 0, menus_[i].title.c_str(), nullptr, nullptr) + 2 * kTitlePad;
    title_x_.push_back(static_cast<int>(left));
    if (static_cast<int>(i) == open_) {
      nvgBeginPath(ctx);
      nvgRect(ctx, x + left, y, width, h - 1);
      nvgFillColor(ctx, nvgRGB(20, 20, 20));
      nvgFill(ctx);
    }
    nvgFillColor(ctx, static_cast<int>(i) == open_ ? nvgRGB(255, 255, 255) : nvgRGB(10, 10, 10));
    nvgText(ctx, x + left + kTitlePad, y + h / 2.0f, menus_[i].title.c_str(), nullptr);
    left += width;
  }
  title_x_.push_back(static_cast<int>(left));
}
