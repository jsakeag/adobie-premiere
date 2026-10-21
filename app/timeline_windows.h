#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include <nanogui/window.h>

#include "construction_view.h"

class FrameView;
namespace nanogui {
class Button;
class Label;
class ProgressBar;
}  // namespace nanogui

struct ConstructionToolbarActions {
  std::function<void(TimelineTool)> set_tool;
  std::function<void(int)> zoom;  // +1 closer, -1 farther
};

// The Construction window: a toolbar (Selection and Razor tools, zoom, the
// edit line's timecode) over the tracks.
class ConstructionWindow : public nanogui::Window {
 public:
  ConstructionWindow(nanogui::Widget* parent, ConstructionActions actions,
                     ConstructionToolbarActions toolbar);

  ConstructionView* view() const { return view_; }
  // Updates the toolbar; the view is updated through view().
  void show(TimelineTool tool, const std::string& edit_line, const std::string& info);
  // Sizes the window and its tracks to a width.
  void fit_width(int width);

 private:
  ConstructionToolbarActions toolbar_;
  ConstructionView* view_ = nullptr;
  nanogui::Button* select_ = nullptr;
  nanogui::Button* razor_ = nullptr;
  nanogui::Label* time_ = nullptr;
  nanogui::Label* info_ = nullptr;
};

struct PreviewActions {
  std::function<void()> play;  // render the work area if needed, then play it
  std::function<void()> stop;  // stop playing, or cancel a render
};

// The Preview window: the program at the edit line, or the rendered work
// area as it plays, with Play and Stop and a progress bar while rendering.
class PreviewWindow : public nanogui::Window {
 public:
  PreviewWindow(nanogui::Widget* parent, PreviewActions actions);

  FrameView* view() const { return view_; }
  void set_caption_text(const std::string& text);
  // Shows the progress bar at `progress` (0..1), or hides it when negative.
  void set_progress(double progress);
  void set_playing(bool playing);

 private:
  PreviewActions actions_;
  FrameView* view_ = nullptr;
  nanogui::Label* caption_ = nullptr;
  nanogui::ProgressBar* progress_ = nullptr;
  nanogui::Button* play_ = nullptr;
};
