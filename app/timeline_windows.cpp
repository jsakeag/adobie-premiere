#include "timeline_windows.h"

#include <utility>

#include <nanogui/button.h>
#include <nanogui/icons.h>
#include <nanogui/label.h>
#include <nanogui/layout.h>
#include <nanogui/progressbar.h>
#include <nanogui/screen.h>

#include "frame_view.h"

namespace {

constexpr int kTrackAreaHeight = 270;

}  // namespace

ConstructionWindow::ConstructionWindow(nanogui::Widget* parent, ConstructionActions actions,
                                       ConstructionToolbarActions toolbar)
    : nanogui::Window(parent, "Construction"), toolbar_(std::move(toolbar)) {
  using nanogui::Alignment;
  using nanogui::BoxLayout;
  using nanogui::Button;
  using nanogui::Orientation;
  set_layout(new BoxLayout(Orientation::Vertical, Alignment::Fill, 6, 4));

  nanogui::Widget* bar = new nanogui::Widget(this);
  bar->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Middle, 0, 4));
  select_ = new Button(bar, "", FA_MOUSE_POINTER);
  select_->set_tooltip("Selection tool (V): drag clips to move them, their edges to trim");
  select_->set_flags(Button::RadioButton);
  select_->set_callback([this] { if (toolbar_.set_tool) toolbar_.set_tool(TimelineTool::Select); });
  razor_ = new Button(bar, "", FA_CUT);
  razor_->set_tooltip("Razor tool (C): click a clip to cut it in two");
  razor_->set_flags(Button::RadioButton);
  razor_->set_callback([this] { if (toolbar_.set_tool) toolbar_.set_tool(TimelineTool::Razor); });
  for (Button* b : {select_, razor_}) b->set_fixed_size({34, 26});

  Button* out = new Button(bar, "", FA_SEARCH_MINUS);
  out->set_tooltip("Zoom out (-)");
  out->set_fixed_size({34, 26});
  out->set_callback([this] { if (toolbar_.zoom) toolbar_.zoom(-1); });
  Button* in = new Button(bar, "", FA_SEARCH_PLUS);
  in->set_tooltip("Zoom in (=)");
  in->set_fixed_size({34, 26});
  in->set_callback([this] { if (toolbar_.zoom) toolbar_.zoom(1); });

  time_ = new nanogui::Label(bar, "00:00:00:00", "sans-bold", 18);
  time_->set_fixed_width(130);
  info_ = new nanogui::Label(bar, "");
  info_->set_fixed_width(600);
  info_->set_font_size(14);

  view_ = new ConstructionView(this, std::move(actions));
  fit_width(1400);
}

void ConstructionWindow::fit_width(int width) {
  view_->set_fixed_size({width - 12, kTrackAreaHeight});
}

void ConstructionWindow::show(TimelineTool tool, const std::string& edit_line,
                              const std::string& info) {
  select_->set_pushed(tool == TimelineTool::Select);
  razor_->set_pushed(tool == TimelineTool::Razor);
  time_->set_caption(edit_line);
  info_->set_caption(info);
}

PreviewWindow::PreviewWindow(nanogui::Widget* parent, PreviewActions actions)
    : nanogui::Window(parent, "Preview"), actions_(std::move(actions)) {
  using nanogui::Button;
  set_layout(new nanogui::BoxLayout(nanogui::Orientation::Vertical,
                                    nanogui::Alignment::Middle, 6, 4));
  view_ = new FrameView(this);
  view_->set_fixed_size({320, 240});
  nanogui::Widget* bar = new nanogui::Widget(this);
  bar->set_layout(new nanogui::BoxLayout(nanogui::Orientation::Horizontal,
                                         nanogui::Alignment::Middle, 0, 6));
  play_ = new Button(bar, "", FA_PLAY);
  play_->set_tooltip("Render the work area if needed, then play it (Return)");
  play_->set_fixed_size({34, 26});
  play_->set_callback([this] { if (actions_.play) actions_.play(); });
  Button* stop = new Button(bar, "", FA_STOP);
  stop->set_tooltip("Stop, or cancel a render (Esc)");
  stop->set_fixed_size({34, 26});
  stop->set_callback([this] { if (actions_.stop) actions_.stop(); });
  progress_ = new nanogui::ProgressBar(bar);
  progress_->set_fixed_size({236, 12});
  progress_->set_visible(false);
  caption_ = new nanogui::Label(this, "");
  caption_->set_fixed_width(320);
  caption_->set_font_size(14);
}

void PreviewWindow::set_caption_text(const std::string& text) { caption_->set_caption(text); }

void PreviewWindow::set_progress(double progress) {
  const bool show = progress >= 0;
  if (show) progress_->set_value(static_cast<float>(progress));
  if (show != progress_->visible()) {
    progress_->set_visible(show);
    if (nanogui::Screen* s = screen()) perform_layout(s->nvg_context());
  }
}

void PreviewWindow::set_playing(bool playing) { play_->set_pushed(playing); }
