#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#include <nanogui/widget.h>

#include "frame_view.h"
#include "mpp/project.h"
#include "mpp/time_scale.h"

enum class TimelineTool { Select, Razor };

// What gestures in the Construction window do. The app turns each into a
// command; nothing here changes the project.
struct ConstructionActions {
  std::function<void(std::int64_t frame)> set_edit_line;
  std::function<void(std::vector<int> clips)> select;
  std::function<void(int clip, mpp::Track track, std::int64_t start)> move;
  std::function<void(int clip, std::int64_t start, std::int64_t in, std::int64_t out)> trim;
  std::function<void(int clip, std::int64_t frame)> razor;
  std::function<void(std::int64_t in, std::int64_t out)> set_work_area;
  std::function<void(int transition, std::int64_t start, std::int64_t length)> set_transition_span;
  std::function<void(int transition)> edit_transition;  // double-click
  std::function<void(int clip)> open_clip;              // double-click
  std::function<void(int clip, std::vector<mpp::FadePoint> fade)> set_fade;
  std::function<void()> focus;  // the view was clicked
  // A small picture of an item at a source position (project frames), for
  // the head of each video clip.
  std::function<mpp::Image(int item, std::int64_t position)> thumbnail;
};

// Where a dragged bin item would land.
struct DropTarget {
  mpp::Track track;
  std::int64_t start;
};

// The tracks of the Construction window, drawn in one widget: work area
// bar, timecode ruler, Video A, the transition track, Video B, Super and
// three audio tracks, the edit line, and a scroll bar. Clips are dragged to
// move, dragged by their edges to trim, and cut with the razor.
class ConstructionView : public nanogui::Widget {
 public:
  static constexpr int kHeaderWidth = 76;

  ConstructionView(nanogui::Widget* parent, ConstructionActions actions);

  // What to draw. Call whenever any of it changes.
  void set_state(const mpp::Project& project, std::int64_t edit_line,
                 const std::vector<int>& selected, TimelineTool tool);

  const mpp::TimeScale& scale() const { return scale_; }
  int zoom_level() const { return zoom_; }
  // One step closer (+1) or farther (-1), keeping the edit line in place.
  void zoom(int steps);
  // Drops the head-frame thumbnails, so they're drawn again (a title
  // changed).
  void forget_thumbnails() { forget_thumbs_ = true; }
  // Scrolls so a frame is visible.
  void reveal(std::int64_t frame);

  // For a bin item dragged over the window (screen coordinates): where it
  // would land, snapped to nearby edges. nullopt when not over a track that
  // can take it. show_drop draws the ghost; pass nullopt to hide it.
  std::optional<DropTarget> drop_target(const nanogui::Vector2i& screen, std::int64_t length,
                                        bool has_video, bool has_audio) const;
  void show_drop(std::optional<DropTarget> target, std::int64_t length);
  // The same for a transition dragged from the Transitions window: the
  // frame under the pointer if it is over the T track, and the span to
  // outline (length 0 hides it).
  std::optional<std::int64_t> transition_drop_frame(const nanogui::Vector2i& screen) const;
  void show_transition_drop(std::int64_t start, std::int64_t length);

  // Screen position just inside a frame on a track, and on the ruler and
  // the work area bar (self-test; exact at 4 or more pixels a frame).
  nanogui::Vector2i point_at(mpp::Track track, std::int64_t frame) const;
  nanogui::Vector2i transition_point(std::int64_t frame) const;
  // Screen position on a clip's rubber band at a frame and level (self-test).
  nanogui::Vector2i fade_point(mpp::Track track, std::int64_t frame, double level) const;
  nanogui::Vector2i ruler_point(std::int64_t frame) const;
  nanogui::Vector2i work_area_point(std::int64_t frame) const;

  bool mouse_button_event(const nanogui::Vector2i& p, int button, bool down,
                          int modifiers) override;
  bool mouse_drag_event(const nanogui::Vector2i& p, const nanogui::Vector2i& rel, int button,
                        int modifiers) override;
  bool mouse_motion_event(const nanogui::Vector2i& p, const nanogui::Vector2i& rel, int button,
                          int modifiers) override;
  bool scroll_event(const nanogui::Vector2i& p, const nanogui::Vector2f& rel, int flags) override;
  void draw(NVGcontext* ctx) override;

 private:
  enum class Row { VideoA, Transition, VideoB, Super, AudioA, AudioB, AudioC };
  enum class Drag {
    None, EditLine, WorkIn, WorkOut, WorkMove, Move, TrimHead, TrimTail, ScrollBar,
    TransitionMove, TransitionHead, TransitionTail, Fade
  };
  struct Hit {
    const mpp::Clip* clip = nullptr;
    const mpp::Transition* transition = nullptr;
    bool head = false;  // within the trim zone at the start
    bool tail = false;  // within the trim zone at the end
  };

  static Row row_of(mpp::Track track);
  static std::optional<mpp::Track> track_of(Row row);
  int row_top(Row row) const;     // relative to the widget
  int row_height(Row row) const;
  std::optional<Row> row_at(int local_y) const;
  int tracks_top() const;
  int tracks_bottom() const;
  double timeline_x(int local_x) const { return local_x - kHeaderWidth; }
  float local_x_of(double frame) const;
  Hit hit(const nanogui::Vector2i& local) const;
  double snap_tolerance() const;
  std::int64_t visible_frames() const;
  void clamp_scroll();

  void draw_ruler(NVGcontext* ctx);
  void draw_tracks(NVGcontext* ctx);
  void draw_clip(NVGcontext* ctx, const mpp::Clip& clip, std::int64_t start, mpp::Track track,
                 float alpha, bool selected);
  void draw_transition(NVGcontext* ctx, const mpp::Transition& t, std::int64_t start,
                       std::int64_t length, float alpha, bool selected);
  bool is_selected(int id) const;
  // The rubber band: where a level sits vertically in a clip on a track,
  // and the level at a vertical position.
  float fade_y(mpp::Track track, double level) const;
  double fade_level_at(mpp::Track track, int local_y) const;
  float fade_x(const mpp::Clip& clip, double position) const;
  static bool has_fade_band(mpp::Track track) { return track != mpp::Track::VideoA && track != mpp::Track::VideoB; }
  // Starts a rubber band drag if the press is on a handle or the line.
  bool press_fade(const mpp::Clip& clip, const nanogui::Vector2i& local);
  void draw_scroll_bar(NVGcontext* ctx);

  ConstructionActions actions_;
  const mpp::Project* project_ = nullptr;
  std::int64_t edit_line_ = 0;
  std::vector<int> selected_;
  TimelineTool tool_ = TimelineTool::Select;
  mpp::TimeScale scale_;
  int zoom_ = 2;

  // The gesture under way.
  Drag drag_ = Drag::None;
  nanogui::Vector2i press_{0, 0};
  bool moved_ = false;
  int drag_clip_ = 0;
  mpp::Track drag_track_ = mpp::Track::VideoA;
  std::int64_t drag_start_ = 0, drag_in_ = 0, drag_out_ = 0;  // ghost values
  std::int64_t drag_length_ = 0;                               // a transition's
  std::int64_t work_in_ = 0, work_out_ = 0;                    // ghost work area
  double grab_offset_ = 0;  // frames between the clip start and the press
  std::vector<mpp::FadePoint> drag_fade_;  // the rubber band being dragged
  std::size_t fade_index_ = 0;             // the handle being dragged
  bool fade_remove_ = false;               // dragged off the track: remove it

  std::optional<DropTarget> drop_;
  std::int64_t drop_length_ = 0;
  std::int64_t transition_drop_start_ = 0, transition_drop_length_ = 0;

  std::map<std::pair<int, std::int64_t>, NvgImage> thumbs_;  // by item and in point
  bool forget_thumbs_ = false;
};
