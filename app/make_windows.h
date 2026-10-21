#pragma once

#include <functional>
#include <utility>

#include <nanogui/widget.h>
#include <nanogui/window.h>

#include "frame_view.h"
#include "mpp/movie_writer.h"

namespace nanogui {
class ComboBox;
}

// Make > Movie: the movie format and whether to export the work area or the
// whole program. OK calls `apply`, which asks where to save.
class ExportWindow : public nanogui::Window {
 public:
  ExportWindow(nanogui::Widget* parent, bool has_work_area,
               std::function<void(mpp::MovieFormat, bool whole_program)> apply);

  void choose(mpp::MovieFormat format, bool whole_program);
  void ok();
  void cancel();
  void set_closed_callback(std::function<void()> closed) { closed_ = std::move(closed); }

 private:
  std::function<void()> closed_;
  std::function<void(mpp::MovieFormat, bool)> apply_;
  nanogui::ComboBox* format_ = nullptr;
  nanogui::ComboBox* range_ = nullptr;
  bool has_work_area_ = false;
};

// Print to Video: the whole screen black with the program playing in the
// middle, as Premiere played a program out to a video recorder. A click or
// Esc (handled by the app) ends it.
class FullScreenView : public nanogui::Widget {
 public:
  FullScreenView(nanogui::Widget* parent, std::function<void()> stop);

  void set_frame(const mpp::Image& frame) { view_->set_frame(frame); }
  const mpp::Image& frame() const { return view_->frame(); }
  // Covers the parent.
  void fill_parent();

  bool mouse_button_event(const nanogui::Vector2i& p, int button, bool down,
                          int modifiers) override;
  void draw(NVGcontext* ctx) override;

 private:
  std::function<void()> stop_;
  FrameView* view_ = nullptr;
};
