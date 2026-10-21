#include "construction_view.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <utility>

#include <nanogui/opengl.h>
#include <nanogui/screen.h>

#include "mpp/rate.h"

using mpp::Clip;
using mpp::Track;
using nanogui::Vector2i;

namespace {

constexpr int kWorkBarHeight = 10;
constexpr int kRulerHeight = 22;
constexpr int kScrollBarHeight = 12;
constexpr int kTrimZone = 6;      // pixels at each clip end that trim
constexpr double kSnapPixels = 8;
constexpr int kThumbW = 40;
constexpr int kThumbH = 30;
constexpr std::size_t kMaxThumbs = 300;

NVGcolor clip_color(Track t) {
  if (t == Track::Superimpose) return nvgRGB(140, 100, 180);
  if (mpp::is_audio_track(t)) return nvgRGB(70, 140, 95);
  return nvgRGB(75, 110, 175);
}

const char* row_label(int row) {
  static const char* labels[] = {"Video A", "T", "Video B", "Super", "Audio A", "Audio B", "Audio C"};
  return labels[row];
}

}  // namespace

ConstructionView::ConstructionView(nanogui::Widget* parent, ConstructionActions actions)
    : nanogui::Widget(parent), actions_(std::move(actions)) {
  scale_.px_per_frame = mpp::kZoomLevels[zoom_];
}

void ConstructionView::set_state(const mpp::Project& project, std::int64_t edit_line,
                                 const std::vector<int>& selected, TimelineTool tool) {
  project_ = &project;
  edit_line_ = edit_line;
  selected_ = selected;
  tool_ = tool;
  if (drag_ == Drag::None) {
    work_in_ = project.work_in;
    work_out_ = project.work_out;
  }
}

ConstructionView::Row ConstructionView::row_of(Track t) {
  switch (t) {
    case Track::VideoA: return Row::VideoA;
    case Track::VideoB: return Row::VideoB;
    case Track::Superimpose: return Row::Super;
    case Track::AudioA: return Row::AudioA;
    case Track::AudioB: return Row::AudioB;
    case Track::AudioC: return Row::AudioC;
  }
  return Row::VideoA;
}

std::optional<Track> ConstructionView::track_of(Row row) {
  switch (row) {
    case Row::VideoA: return Track::VideoA;
    case Row::Transition: return std::nullopt;
    case Row::VideoB: return Track::VideoB;
    case Row::Super: return Track::Superimpose;
    case Row::AudioA: return Track::AudioA;
    case Row::AudioB: return Track::AudioB;
    case Row::AudioC: return Track::AudioC;
  }
  return std::nullopt;
}

int ConstructionView::row_height(Row row) const {
  switch (row) {
    case Row::Transition: return 22;
    case Row::AudioA:
    case Row::AudioB:
    case Row::AudioC: return 28;
    default: return 40;
  }
}

int ConstructionView::tracks_top() const { return kWorkBarHeight + kRulerHeight; }

int ConstructionView::row_top(Row row) const {
  int top = tracks_top();
  for (int r = 0; r < static_cast<int>(row); ++r) top += row_height(static_cast<Row>(r));
  return top;
}

int ConstructionView::tracks_bottom() const {
  return row_top(Row::AudioC) + row_height(Row::AudioC);
}

std::optional<ConstructionView::Row> ConstructionView::row_at(int local_y) const {
  for (int r = 0; r <= static_cast<int>(Row::AudioC); ++r) {
    const Row row = static_cast<Row>(r);
    if (local_y >= row_top(row) && local_y < row_top(row) + row_height(row)) return row;
  }
  return std::nullopt;
}

float ConstructionView::local_x_of(double frame) const {
  return static_cast<float>(kHeaderWidth + scale_.x_of(frame));
}

double ConstructionView::snap_tolerance() const { return kSnapPixels / scale_.px_per_frame; }

std::int64_t ConstructionView::visible_frames() const {
  return static_cast<std::int64_t>((m_size.x() - kHeaderWidth) / scale_.px_per_frame);
}

void ConstructionView::clamp_scroll() {
  scale_.scroll = std::max(0.0, std::floor(scale_.scroll));
}

void ConstructionView::zoom(int steps) {
  const int level = std::clamp(zoom_ - steps, 0, mpp::kZoomLevelCount - 1);
  if (level == zoom_) return;
  const double anchor_x = scale_.x_of(static_cast<double>(edit_line_));
  zoom_ = level;
  scale_.px_per_frame = mpp::kZoomLevels[zoom_];
  // Keep the edit line where it was on screen, if it was on screen.
  const double width = m_size.x() - kHeaderWidth;
  if (anchor_x >= 0 && anchor_x <= width) {
    scale_.scroll = edit_line_ - anchor_x / scale_.px_per_frame;
  }
  clamp_scroll();
}

void ConstructionView::reveal(std::int64_t frame) {
  const std::int64_t visible = std::max<std::int64_t>(1, visible_frames());
  if (frame < scale_.scroll) {
    scale_.scroll = static_cast<double>(frame);
  } else if (frame >= scale_.scroll + visible) {
    scale_.scroll = static_cast<double>(frame - visible + visible / 4);
  }
  clamp_scroll();
}

ConstructionView::Hit ConstructionView::hit(const Vector2i& local) const {
  Hit h;
  if (!project_) return h;
  const auto row = row_at(local.y());
  const double x = timeline_x(local.x());
  if (row == Row::Transition) {
    for (const mpp::Transition& t : project_->transitions) {
      const double x0 = scale_.x_of(static_cast<double>(t.start));
      const double x1 = scale_.x_of(static_cast<double>(t.end()));
      if (x < x0 || x >= x1) continue;
      h.transition = &t;
      const double zone = std::min<double>(kTrimZone, (x1 - x0) / 3);
      h.head = x < x0 + zone;
      h.tail = x >= x1 - zone;
      return h;
    }
    return h;
  }
  const auto track = row ? track_of(*row) : std::nullopt;
  if (!track) return h;
  for (const Clip& c : project_->clips) {
    if (c.track != *track) continue;
    const double x0 = scale_.x_of(static_cast<double>(c.start));
    const double x1 = scale_.x_of(static_cast<double>(c.end()));
    if (x < x0 || x >= x1) continue;
    h.clip = &c;
    const double zone = std::min<double>(kTrimZone, (x1 - x0) / 3);
    h.head = x < x0 + zone;
    h.tail = x >= x1 - zone;
    return h;
  }
  return h;
}

std::optional<DropTarget> ConstructionView::drop_target(const Vector2i& screen,
                                                        std::int64_t length, bool has_video,
                                                        bool has_audio) const {
  if (!project_) return std::nullopt;
  const Vector2i local = screen - absolute_position();
  if (local.x() < kHeaderWidth || local.x() >= m_size.x() || local.y() < 0 ||
      local.y() >= m_size.y()) {
    return std::nullopt;
  }
  const auto row = row_at(local.y());
  const auto track = row ? track_of(*row) : std::nullopt;
  if (!track) return std::nullopt;
  if (mpp::is_video_track(*track) && !has_video) return std::nullopt;
  if (mpp::is_audio_track(*track) && !has_audio) return std::nullopt;
  const std::int64_t start = std::max<std::int64_t>(0, scale_.boundary_at(timeline_x(local.x())));
  const auto points = mpp::snap_points(*project_, edit_line_, {});
  return DropTarget{*track, std::max<std::int64_t>(0, mpp::snap_span(start, length, points,
                                                                     snap_tolerance()))};
}

void ConstructionView::show_drop(std::optional<DropTarget> target, std::int64_t length) {
  drop_ = target;
  drop_length_ = length;
}

std::optional<std::int64_t> ConstructionView::transition_drop_frame(const Vector2i& screen) const {
  const Vector2i local = screen - absolute_position();
  if (local.x() < kHeaderWidth || local.x() >= m_size.x()) return std::nullopt;
  if (row_at(local.y()) != Row::Transition) return std::nullopt;
  return std::max<std::int64_t>(0, scale_.frame_at(timeline_x(local.x())));
}

void ConstructionView::show_transition_drop(std::int64_t start, std::int64_t length) {
  transition_drop_start_ = start;
  transition_drop_length_ = length;
}

bool ConstructionView::is_selected(int id) const {
  return std::find(selected_.begin(), selected_.end(), id) != selected_.end();
}

Vector2i ConstructionView::point_at(Track track, std::int64_t frame) const {
  // A quarter of a frame in: inside the frame, and nearest its left edge.
  const Row row = row_of(track);
  return absolute_position() +
         Vector2i(static_cast<int>(std::lround(local_x_of(frame + 0.25))),
                  row_top(row) + row_height(row) / 2);
}

Vector2i ConstructionView::transition_point(std::int64_t frame) const {
  return absolute_position() +
         Vector2i(static_cast<int>(std::lround(local_x_of(frame + 0.25))),
                  row_top(Row::Transition) + row_height(Row::Transition) / 2);
}

float ConstructionView::fade_y(Track track, double level) const {
  const Row row = row_of(track);
  const float top = row_top(row) + 5.0f, bottom = row_top(row) + row_height(row) - 5.0f;
  return bottom - static_cast<float>(level) * (bottom - top);
}

double ConstructionView::fade_level_at(Track track, int local_y) const {
  const Row row = row_of(track);
  const double top = row_top(row) + 5.0, bottom = row_top(row) + row_height(row) - 5.0;
  return std::clamp((bottom - local_y) / (bottom - top), 0.0, 1.0);
}

float ConstructionView::fade_x(const Clip& clip, double position) const {
  return local_x_of(clip.start + (position - clip.in));
}

Vector2i ConstructionView::fade_point(Track track, std::int64_t frame, double level) const {
  return absolute_position() + Vector2i(static_cast<int>(std::lround(local_x_of(frame + 0.25))),
                                        static_cast<int>(std::lround(fade_y(track, level))));
}

bool ConstructionView::press_fade(const Clip& clip, const Vector2i& local) {
  if (!has_fade_band(clip.track)) return false;
  drag_fade_ = clip.fade;
  // On a handle: drag it.
  for (std::size_t i = 0; i < drag_fade_.size(); ++i) {
    const float hx = fade_x(clip, drag_fade_[i].position);
    const float hy = fade_y(clip.track, drag_fade_[i].level);
    if (std::abs(local.x() - hx) <= 5 && std::abs(local.y() - hy) <= 5) {
      fade_index_ = i;
      return true;
    }
  }
  // On the line: add a handle there and drag it.
  const double position =
      std::clamp(clip.in + (timeline_x(local.x()) / scale_.px_per_frame + scale_.scroll - clip.start),
                 static_cast<double>(clip.in), static_cast<double>(clip.out));
  const double level = mpp::fade_level(clip.fade, position);
  if (std::abs(local.y() - fade_y(clip.track, level)) > 4) return false;
  auto at = std::upper_bound(drag_fade_.begin(), drag_fade_.end(), position,
                             [](double p, const mpp::FadePoint& f) { return p < f.position; });
  fade_index_ = static_cast<std::size_t>(at - drag_fade_.begin());
  drag_fade_.insert(at, {position, level});
  return true;
}

Vector2i ConstructionView::ruler_point(std::int64_t frame) const {
  return absolute_position() + Vector2i(static_cast<int>(std::lround(local_x_of(frame + 0.5))),
                                        kWorkBarHeight + kRulerHeight / 2);
}

Vector2i ConstructionView::work_area_point(std::int64_t frame) const {
  return absolute_position() +
         Vector2i(static_cast<int>(std::lround(local_x_of(static_cast<double>(frame)))),
                  kWorkBarHeight / 2);
}

bool ConstructionView::mouse_button_event(const Vector2i& p, int button, bool down,
                                          int modifiers) {
  if (button != GLFW_MOUSE_BUTTON_LEFT || !project_) return true;
  const Vector2i local = p - m_pos;
  if (down) {
    if (actions_.focus) actions_.focus();
    press_ = local;
    moved_ = false;
    drag_ = Drag::None;
    if (local.y() >= m_size.y() - kScrollBarHeight) {
      drag_ = Drag::ScrollBar;
      return true;
    }
    if (local.x() < kHeaderWidth) return true;
    const double x = timeline_x(local.x());
    if (local.y() < kWorkBarHeight) {
      if (work_out_ <= work_in_) {
        // No work area yet: dragging draws one.
        work_in_ = std::max<std::int64_t>(0, scale_.boundary_at(x));
        work_out_ = work_in_ + 1;
        drag_ = Drag::WorkOut;
        return true;
      }
      const double in_x = scale_.x_of(static_cast<double>(work_in_));
      const double out_x = scale_.x_of(static_cast<double>(work_out_));
      if (std::abs(x - in_x) <= 6) drag_ = Drag::WorkIn;
      else if (std::abs(x - out_x) <= 6) drag_ = Drag::WorkOut;
      else if (x > in_x && x < out_x) drag_ = Drag::WorkMove;
      else drag_ = x < in_x ? Drag::WorkIn : Drag::WorkOut;
      grab_offset_ = scale_.boundary_at(x) - static_cast<double>(work_in_);
      mouse_drag_event(p, {0, 0}, button, modifiers);
      return true;
    }
    if (local.y() < tracks_top()) {
      drag_ = Drag::EditLine;
      if (actions_.set_edit_line) actions_.set_edit_line(std::max<std::int64_t>(0, scale_.frame_at(x)));
      return true;
    }
    const Hit h = hit(local);
    if (h.transition) {
      const int id = h.transition->id;
      if (actions_.select && !is_selected(id)) actions_.select({id});
      if (modifiers & nanogui::MOD_DOUBLE_CLICK) {
        if (actions_.edit_transition) actions_.edit_transition(id);
        return true;
      }
      drag_clip_ = id;
      drag_start_ = h.transition->start;
      drag_length_ = h.transition->length;
      grab_offset_ = scale_.boundary_at(x) - static_cast<double>(h.transition->start);
      drag_ = h.head ? Drag::TransitionHead : h.tail ? Drag::TransitionTail : Drag::TransitionMove;
      return true;
    }
    if (!h.clip) {
      if (actions_.select) actions_.select({});
      return true;
    }
    if (modifiers & nanogui::MOD_DOUBLE_CLICK) {
      if (actions_.open_clip) actions_.open_clip(h.clip->id);
      return true;
    }
    if (tool_ == TimelineTool::Razor) {
      const std::int64_t frame = mpp::snap(scale_.boundary_at(x),
                                           {edit_line_}, snap_tolerance());
      if (actions_.razor) actions_.razor(h.clip->id, frame);
      return true;
    }
    // Shift toggles the clip in the selection; a plain click on an
    // unselected clip selects just it.
    std::vector<int> selection = selected_;
    const bool was_selected =
        std::find(selection.begin(), selection.end(), h.clip->id) != selection.end();
    if (modifiers & GLFW_MOD_SHIFT) {
      if (was_selected) {
        selection.erase(std::find(selection.begin(), selection.end(), h.clip->id));
      } else {
        selection.push_back(h.clip->id);
      }
      if (actions_.select) actions_.select(selection);
      return true;
    }
    if (!was_selected && actions_.select) actions_.select({h.clip->id});
    if (!h.head && !h.tail && press_fade(*h.clip, local)) {
      drag_clip_ = h.clip->id;
      drag_track_ = h.clip->track;
      fade_remove_ = false;
      drag_ = Drag::Fade;
      return true;
    }
    drag_clip_ = h.clip->id;
    drag_track_ = h.clip->track;
    drag_start_ = h.clip->start;
    drag_in_ = h.clip->in;
    drag_out_ = h.clip->out;
    grab_offset_ = scale_.boundary_at(x) - static_cast<double>(h.clip->start);
    drag_ = h.head ? Drag::TrimHead : h.tail ? Drag::TrimTail : Drag::Move;
    return true;
  }

  // Release: commit what the drag changed, as one command.
  const Drag finished = drag_;
  drag_ = Drag::None;
  const Clip* clip = project_->clip(drag_clip_);
  switch (finished) {
    case Drag::WorkIn:
    case Drag::WorkOut:
    case Drag::WorkMove:
      if ((work_in_ != project_->work_in || work_out_ != project_->work_out) &&
          actions_.set_work_area) {
        actions_.set_work_area(work_in_, work_out_);
      }
      work_in_ = project_->work_in;
      work_out_ = project_->work_out;
      break;
    case Drag::Move:
      if (moved_ && clip && (drag_start_ != clip->start || drag_track_ != clip->track) &&
          actions_.move) {
        actions_.move(drag_clip_, drag_track_, drag_start_);
      }
      break;
    case Drag::Fade:
      if (clip && actions_.set_fade) {
        std::vector<mpp::FadePoint> fade = drag_fade_;
        if (fade_remove_) fade.erase(fade.begin() + static_cast<std::ptrdiff_t>(fade_index_));
        if (fade != clip->fade) actions_.set_fade(drag_clip_, fade);
      }
      break;
    case Drag::TransitionMove:
    case Drag::TransitionHead:
    case Drag::TransitionTail: {
      const mpp::Transition* t = project_->transition(drag_clip_);
      if (moved_ && t && (drag_start_ != t->start || drag_length_ != t->length) &&
          actions_.set_transition_span) {
        actions_.set_transition_span(drag_clip_, drag_start_, drag_length_);
      }
      break;
    }
    case Drag::TrimHead:
    case Drag::TrimTail:
      if (moved_ && clip && (drag_start_ != clip->start || drag_in_ != clip->in ||
                             drag_out_ != clip->out) &&
          actions_.trim) {
        actions_.trim(drag_clip_, drag_start_, drag_in_, drag_out_);
      }
      break;
    default:
      break;
  }
  drag_clip_ = 0;
  return true;
}

bool ConstructionView::mouse_drag_event(const Vector2i& p, const Vector2i& rel, int, int) {
  if (!project_ || drag_ == Drag::None) return true;
  const Vector2i local = p - m_pos;
  if (std::abs(local.x() - press_.x()) > 2 || std::abs(local.y() - press_.y()) > 2) moved_ = true;
  const double x = timeline_x(local.x());
  const std::int64_t boundary = std::max<std::int64_t>(0, scale_.boundary_at(x));
  const double tolerance = snap_tolerance();
  switch (drag_) {
    case Drag::ScrollBar: {
      const double width = m_size.x() - kHeaderWidth;
      const double total = std::max<double>(project_->duration() + visible_frames(), 1);
      scale_.scroll += rel.x() * total / width;
      clamp_scroll();
      break;
    }
    case Drag::EditLine:
      if (actions_.set_edit_line) {
        actions_.set_edit_line(std::max<std::int64_t>(0, scale_.frame_at(x)));
      }
      break;
    case Drag::WorkIn:
    case Drag::WorkOut: {
      const auto points = mpp::snap_points(*project_, edit_line_, {});
      const std::int64_t frame = mpp::snap(boundary, points, tolerance);
      if (drag_ == Drag::WorkIn) work_in_ = std::min(frame, work_out_ - 1);
      else work_out_ = std::max(frame, work_in_ + 1);
      break;
    }
    case Drag::WorkMove: {
      const std::int64_t length = work_out_ - work_in_;
      work_in_ = std::max<std::int64_t>(0, boundary - static_cast<std::int64_t>(grab_offset_));
      work_out_ = work_in_ + length;
      break;
    }
    case Drag::Move: {
      const Clip* clip = project_->clip(drag_clip_);
      if (!clip) break;
      // Stay within the clip's kind of track.
      if (const auto row = row_at(local.y())) {
        if (const auto track = track_of(*row)) {
          if (mpp::is_video_track(*track) == mpp::is_video_track(clip->track)) drag_track_ = *track;
        }
      }
      std::vector<int> ignore = {clip->id};
      if (clip->link) ignore.push_back(clip->link);
      const auto points = mpp::snap_points(*project_, edit_line_, ignore);
      const std::int64_t start =
          std::max<std::int64_t>(0, boundary - static_cast<std::int64_t>(grab_offset_));
      drag_start_ = std::max<std::int64_t>(0, mpp::snap_span(start, clip->length(), points, tolerance));
      break;
    }
    case Drag::Fade: {
      const Clip* clip = project_->clip(drag_clip_);
      if (!clip || fade_index_ >= drag_fade_.size()) break;
      // Between its neighbours, within the clip; off the track removes it.
      const double lo = fade_index_ > 0 ? drag_fade_[fade_index_ - 1].position : clip->in;
      const double hi = fade_index_ + 1 < drag_fade_.size() ? drag_fade_[fade_index_ + 1].position
                                                            : clip->out;
      const double position =
          clip->in + (x / scale_.px_per_frame + scale_.scroll - clip->start);
      drag_fade_[fade_index_].position = std::clamp(position, lo, hi);
      drag_fade_[fade_index_].level = fade_level_at(clip->track, local.y());
      const Row row = row_of(clip->track);
      fade_remove_ = local.y() < row_top(row) - 20 || local.y() > row_top(row) + row_height(row) + 20;
      break;
    }
    case Drag::TransitionMove:
    case Drag::TransitionHead:
    case Drag::TransitionTail: {
      const mpp::Transition* t = project_->transition(drag_clip_);
      if (!t) break;
      const auto points = mpp::snap_points(*project_, edit_line_, {t->id});
      if (drag_ == Drag::TransitionMove) {
        const std::int64_t start =
            std::max<std::int64_t>(0, boundary - static_cast<std::int64_t>(grab_offset_));
        drag_start_ = std::max<std::int64_t>(0, mpp::snap_span(start, t->length, points, tolerance));
        drag_length_ = t->length;
      } else if (drag_ == Drag::TransitionHead) {
        const std::int64_t start = std::min(mpp::snap(boundary, points, tolerance), t->end() - 1);
        drag_start_ = start;
        drag_length_ = t->end() - start;
      } else {
        const std::int64_t end = std::max(mpp::snap(boundary, points, tolerance), t->start + 1);
        drag_start_ = t->start;
        drag_length_ = end - t->start;
      }
      break;
    }
    case Drag::TrimHead: {
      const Clip* clip = project_->clip(drag_clip_);
      if (!clip) break;
      std::vector<int> ignore = {clip->id};
      if (clip->link) ignore.push_back(clip->link);
      std::int64_t start = mpp::snap(boundary, mpp::snap_points(*project_, edit_line_, ignore),
                                     tolerance);
      // Can't pass the end, or reach before the start of the source.
      start = std::min(start, clip->end() - 1);
      start = std::max(start, clip->start - clip->in);
      drag_start_ = start;
      drag_in_ = clip->in + (start - clip->start);
      drag_out_ = clip->out;
      break;
    }
    case Drag::TrimTail: {
      const Clip* clip = project_->clip(drag_clip_);
      if (!clip) break;
      std::vector<int> ignore = {clip->id};
      if (clip->link) ignore.push_back(clip->link);
      std::int64_t end = mpp::snap(boundary, mpp::snap_points(*project_, edit_line_, ignore),
                                   tolerance);
      end = std::max(end, clip->start + 1);
      std::int64_t out = clip->in + (end - clip->start);
      const mpp::MediaItem* item = project_->item(clip->item);
      if (item) {
        if (const auto length = mpp::source_length(item->info, project_->settings.rate)) {
          out = std::min(out, *length);
        }
      }
      drag_start_ = clip->start;
      drag_in_ = clip->in;
      drag_out_ = out;
      break;
    }
    default:
      break;
  }
  return true;
}

bool ConstructionView::mouse_motion_event(const Vector2i& p, const Vector2i&, int, int) {
  // A trim cursor over clip edges, a hand over clip bodies.
  const Vector2i local = p - m_pos;
  const Hit h = hit(local);
  const bool over = h.clip || h.transition;
  if (tool_ == TimelineTool::Razor && h.clip) set_cursor(nanogui::Cursor::Crosshair);
  else if (over && (h.head || h.tail)) set_cursor(nanogui::Cursor::HResize);
  else if (over) set_cursor(nanogui::Cursor::Hand);
  else set_cursor(nanogui::Cursor::Arrow);
  return true;
}

bool ConstructionView::scroll_event(const Vector2i&, const nanogui::Vector2f& rel, int) {
  GLFWwindow* window = screen()->glfw_window();
  const bool zoom = glfwGetKey(window, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS ||
                    glfwGetKey(window, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS;
  if (zoom) {
    if (rel.y() != 0) this->zoom(rel.y() > 0 ? 1 : -1);
    return true;
  }
  // Both directions scroll through time, so a mouse wheel works too.
  const double pixels = (std::abs(rel.x()) > std::abs(rel.y()) ? -rel.x() : -rel.y()) * 20.0;
  scale_.scroll += pixels / scale_.px_per_frame;
  clamp_scroll();
  return true;
}

void ConstructionView::draw(NVGcontext* ctx) {
  if (thumbs_.size() > kMaxThumbs) {
    for (auto& [key, image] : thumbs_) image.release(ctx);
    thumbs_.clear();
  }
  nvgSave(ctx);
  nvgTranslate(ctx, m_pos.x(), m_pos.y());
  nvgIntersectScissor(ctx, 0, 0, m_size.x(), m_size.y());
  nvgBeginPath(ctx);
  nvgRect(ctx, 0, 0, m_size.x(), m_size.y());
  nvgFillColor(ctx, nvgRGB(38, 38, 40));
  nvgFill(ctx);
  if (project_) {
    draw_tracks(ctx);
    draw_ruler(ctx);
    draw_scroll_bar(ctx);
  }
  nvgRestore(ctx);
}

void ConstructionView::draw_ruler(NVGcontext* ctx) {
  const float w = m_size.x();
  // Work area bar.
  nvgBeginPath(ctx);
  nvgRect(ctx, kHeaderWidth, 0, w - kHeaderWidth, kWorkBarHeight);
  nvgFillColor(ctx, nvgRGB(30, 30, 30));
  nvgFill(ctx);
  if (work_out_ > work_in_) {
    const float x0 = std::max<float>(kHeaderWidth, local_x_of(static_cast<double>(work_in_)));
    const float x1 = local_x_of(static_cast<double>(work_out_));
    if (x1 > x0) {
      nvgBeginPath(ctx);
      nvgRect(ctx, x0, 1, x1 - x0, kWorkBarHeight - 2);
      nvgFillColor(ctx, nvgRGB(230, 200, 40));
      nvgFill(ctx);
    }
  }

  // Ruler with timecode labels.
  const float top = kWorkBarHeight;
  nvgBeginPath(ctx);
  nvgRect(ctx, kHeaderWidth, top, w - kHeaderWidth, kRulerHeight);
  nvgFillColor(ctx, nvgRGB(58, 58, 62));
  nvgFill(ctx);
  const mpp::Rate rate = project_->settings.rate;
  const std::int64_t step = mpp::tick_step(rate, scale_.px_per_frame, 90);
  const std::int64_t minor = std::max<std::int64_t>(1, step / 5);
  const std::int64_t first = static_cast<std::int64_t>(scale_.scroll) / minor * minor;
  const std::int64_t last = static_cast<std::int64_t>(scale_.scroll) + visible_frames() + step;
  nvgFontFace(ctx, "sans");
  nvgFontSize(ctx, 12);
  nvgTextAlign(ctx, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
  nvgStrokeColor(ctx, nvgRGB(170, 170, 170));
  nvgStrokeWidth(ctx, 1.0f);
  for (std::int64_t f = first; f <= last; f += minor) {
    const float x = std::floor(local_x_of(static_cast<double>(f))) + 0.5f;
    if (x < kHeaderWidth) continue;
    const bool major = f % step == 0;
    nvgBeginPath(ctx);
    nvgMoveTo(ctx, x, top + kRulerHeight - (major ? 10 : 4));
    nvgLineTo(ctx, x, top + kRulerHeight);
    nvgStroke(ctx);
    if (major) {
      nvgFillColor(ctx, nvgRGB(210, 210, 210));
      nvgText(ctx, x + 3, top + 2, mpp::format_timecode(f, rate).c_str(), nullptr);
    }
  }

  // Track headers.
  nvgBeginPath(ctx);
  nvgRect(ctx, 0, 0, kHeaderWidth, m_size.y());
  nvgFillColor(ctx, nvgRGB(50, 50, 54));
  nvgFill(ctx);
  nvgFontSize(ctx, 13);
  nvgTextAlign(ctx, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
  for (int r = 0; r <= static_cast<int>(Row::AudioC); ++r) {
    const Row row = static_cast<Row>(r);
    nvgFillColor(ctx, row == Row::Transition ? nvgRGB(160, 160, 160) : nvgRGB(220, 220, 220));
    nvgText(ctx, 8, row_top(row) + row_height(row) / 2.0f, row_label(r), nullptr);
  }

  // Edit line, over everything.
  const float ex = std::floor(local_x_of(static_cast<double>(edit_line_))) + 0.5f;
  if (ex >= kHeaderWidth) {
    nvgBeginPath(ctx);
    nvgMoveTo(ctx, ex, top);
    nvgLineTo(ctx, ex, static_cast<float>(tracks_bottom()));
    nvgStrokeColor(ctx, nvgRGB(255, 70, 70));
    nvgStrokeWidth(ctx, 1.5f);
    nvgStroke(ctx);
    nvgBeginPath(ctx);
    nvgMoveTo(ctx, ex - 5, top);
    nvgLineTo(ctx, ex + 5, top);
    nvgLineTo(ctx, ex, top + 7);
    nvgFillColor(ctx, nvgRGB(255, 70, 70));
    nvgFill(ctx);
  }
}

void ConstructionView::draw_tracks(NVGcontext* ctx) {
  const float w = m_size.x();
  for (int r = 0; r <= static_cast<int>(Row::AudioC); ++r) {
    const Row row = static_cast<Row>(r);
    nvgBeginPath(ctx);
    nvgRect(ctx, kHeaderWidth, row_top(row), w - kHeaderWidth, row_height(row));
    nvgFillColor(ctx, row == Row::Transition ? nvgRGB(32, 32, 34)
                      : r % 2                ? nvgRGB(44, 44, 47)
                                             : nvgRGB(48, 48, 51));
    nvgFill(ctx);
    nvgBeginPath(ctx);
    nvgMoveTo(ctx, 0, row_top(row) + row_height(row) - 0.5f);
    nvgLineTo(ctx, w, row_top(row) + row_height(row) - 0.5f);
    nvgStrokeColor(ctx, nvgRGB(25, 25, 25));
    nvgStrokeWidth(ctx, 1.0f);
    nvgStroke(ctx);
  }

  nvgSave(ctx);
  nvgIntersectScissor(ctx, kHeaderWidth, 0, w - kHeaderWidth, m_size.y());
  const bool dragging_transition = drag_ == Drag::TransitionMove ||
                                   drag_ == Drag::TransitionHead || drag_ == Drag::TransitionTail;
  for (const mpp::Transition& t : project_->transitions) {
    const bool ghosted = dragging_transition && moved_ && t.id == drag_clip_;
    draw_transition(ctx, t, t.start, t.length, ghosted ? 0.35f : 1.0f, is_selected(t.id));
  }
  if (dragging_transition && moved_) {
    if (const mpp::Transition* t = project_->transition(drag_clip_)) {
      draw_transition(ctx, *t, drag_start_, drag_length_, 0.85f, true);
    }
  }
  if (transition_drop_length_ > 0) {
    const float x0 = local_x_of(static_cast<double>(transition_drop_start_));
    const float x1 = local_x_of(static_cast<double>(transition_drop_start_ + transition_drop_length_));
    nvgBeginPath(ctx);
    nvgRect(ctx, x0, row_top(Row::Transition) + 2, std::max(2.0f, x1 - x0),
            row_height(Row::Transition) - 4);
    nvgFillColor(ctx, nvgRGBA(255, 255, 255, 60));
    nvgFill(ctx);
    nvgStrokeColor(ctx, nvgRGB(255, 255, 255));
    nvgStrokeWidth(ctx, 1.5f);
    nvgStroke(ctx);
  }
  const bool dragging_clip =
      drag_ == Drag::Move || drag_ == Drag::TrimHead || drag_ == Drag::TrimTail;
  for (const Clip& c : project_->clips) {
    const bool selected = is_selected(c.id);
    const bool ghosted = dragging_clip && moved_ && c.id == drag_clip_;
    if (drag_ == Drag::Fade && c.id == drag_clip_) {
      Clip shown = c;
      shown.fade = drag_fade_;
      if (fade_remove_) shown.fade.erase(shown.fade.begin() + static_cast<std::ptrdiff_t>(fade_index_));
      draw_clip(ctx, shown, c.start, c.track, 1.0f, selected);
      continue;
    }
    draw_clip(ctx, c, c.start, c.track, ghosted ? 0.35f : 1.0f, selected);
  }
  if (dragging_clip && moved_) {
    if (const Clip* c = project_->clip(drag_clip_)) {
      Clip ghost = *c;
      ghost.in = drag_in_;
      ghost.out = drag_out_;
      draw_clip(ctx, ghost, drag_start_, drag_track_, 0.85f, true);
      // The linked clip follows a move or trim in time.
      if (const Clip* partner = c->link ? project_->clip(c->link) : nullptr) {
        Clip pg = *partner;
        pg.in += drag_in_ - c->in;
        pg.out += drag_out_ - c->out;
        draw_clip(ctx, pg, partner->start + (drag_start_ - c->start), partner->track, 0.6f, true);
      }
    }
  }
  if (drop_) {
    const Row row = row_of(drop_->track);
    const float x0 = local_x_of(static_cast<double>(drop_->start));
    const float x1 = local_x_of(static_cast<double>(drop_->start + drop_length_));
    nvgBeginPath(ctx);
    nvgRect(ctx, x0, row_top(row) + 2, std::max(2.0f, x1 - x0), row_height(row) - 4);
    nvgFillColor(ctx, nvgRGBA(255, 255, 255, 60));
    nvgFill(ctx);
    nvgStrokeColor(ctx, nvgRGB(255, 255, 255));
    nvgStrokeWidth(ctx, 1.5f);
    nvgStroke(ctx);
  }
  nvgRestore(ctx);
}

void ConstructionView::draw_clip(NVGcontext* ctx, const Clip& clip, std::int64_t start,
                                 Track track, float alpha, bool selected) {
  const Row row = row_of(track);
  const float x0 = local_x_of(static_cast<double>(start));
  const float x1 = local_x_of(static_cast<double>(start + clip.length()));
  if (x1 < kHeaderWidth || x0 > m_size.x()) return;
  const float y = row_top(row) + 2.0f, h = row_height(row) - 4.0f;
  const float cw = std::max(2.0f, x1 - x0);
  NVGcolor color = clip_color(track);
  if (selected) color = nvgLerpRGBA(color, nvgRGB(255, 255, 255), 0.25f);
  color.a = alpha;
  nvgBeginPath(ctx);
  nvgRoundedRect(ctx, x0, y, cw, h, 2);
  nvgFillColor(ctx, color);
  nvgFill(ctx);
  nvgStrokeColor(ctx, selected ? nvgRGBAf(1, 1, 1, alpha) : nvgRGBAf(0, 0, 0, alpha * 0.6f));
  nvgStrokeWidth(ctx, selected ? 1.5f : 1.0f);
  nvgStroke(ctx);

  nvgSave(ctx);
  nvgIntersectScissor(ctx, x0, y, cw, h);
  float text_x = x0 + 4;
  const mpp::MediaItem* item = project_->item(clip.item);
  // The head frame, as Premiere's "head only" display.
  if (item && mpp::is_video_track(track) && item->info.has_video && cw > kThumbW + 8 &&
      actions_.thumbnail && alpha > 0.5f) {
    const auto key = std::make_pair(clip.item, clip.in);
    NvgImage& thumb = thumbs_[key];
    if (!thumb.valid()) {
      const mpp::Image picture = actions_.thumbnail(clip.item, clip.in);
      if (!picture.empty()) thumb.set(ctx, picture);
    }
    if (thumb.valid()) {
      const float th = std::min<float>(h - 4, kThumbH);
      const float tw = th * thumb.width() / std::max(1, thumb.height());
      NVGpaint paint = nvgImagePattern(ctx, x0 + 2, y + 2, tw, th, 0, thumb.id(), alpha);
      nvgBeginPath(ctx);
      nvgRect(ctx, x0 + 2, y + 2, tw, th);
      nvgFillPaint(ctx, paint);
      nvgFill(ctx);
      text_x = x0 + tw + 6;
    }
  }
  if (item) {
    nvgFontFace(ctx, "sans");
    nvgFontSize(ctx, 13);
    nvgTextAlign(ctx, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgFillColor(ctx, nvgRGBAf(1, 1, 1, alpha));
    nvgText(ctx, text_x, y + 3, item->name.c_str(), nullptr);
  }
  if (has_fade_band(track)) {
    // The rubber band, through its handles, across the clip.
    Clip placed = clip;
    placed.start = start;
    nvgBeginPath(ctx);
    nvgMoveTo(ctx, x0, fade_y(track, mpp::fade_level(clip.fade, static_cast<double>(clip.in))));
    for (const mpp::FadePoint& f : clip.fade) {
      if (f.position > clip.in && f.position < clip.out) {
        nvgLineTo(ctx, fade_x(placed, f.position), fade_y(track, f.level));
      }
    }
    nvgLineTo(ctx, x1, fade_y(track, mpp::fade_level(clip.fade, static_cast<double>(clip.out))));
    nvgStrokeColor(ctx, nvgRGBAf(1.0f, 0.35f, 0.3f, alpha));
    nvgStrokeWidth(ctx, 1.5f);
    nvgStroke(ctx);
    for (const mpp::FadePoint& f : clip.fade) {
      if (f.position < clip.in || f.position > clip.out) continue;
      const float hx = fade_x(placed, f.position), hy = fade_y(track, f.level);
      nvgBeginPath(ctx);
      nvgRect(ctx, hx - 3, hy - 3, 6, 6);
      nvgFillColor(ctx, nvgRGBAf(1, 1, 1, alpha));
      nvgFill(ctx);
      nvgStrokeColor(ctx, nvgRGBAf(0.8f, 0.2f, 0.2f, alpha));
      nvgStrokeWidth(ctx, 1.0f);
      nvgStroke(ctx);
    }
  }
  nvgRestore(ctx);
}

void ConstructionView::draw_transition(NVGcontext* ctx, const mpp::Transition& t,
                                       std::int64_t start, std::int64_t length, float alpha,
                                       bool selected) {
  const float x0 = local_x_of(static_cast<double>(start));
  const float x1 = local_x_of(static_cast<double>(start + length));
  if (x1 < kHeaderWidth || x0 > m_size.x()) return;
  const float y = row_top(Row::Transition) + 2.0f, h = row_height(Row::Transition) - 4.0f;
  const float w = std::max(2.0f, x1 - x0);
  NVGcolor color = nvgRGB(190, 150, 50);
  if (selected) color = nvgLerpRGBA(color, nvgRGB(255, 255, 255), 0.3f);
  color.a = alpha;
  nvgBeginPath(ctx);
  nvgRoundedRect(ctx, x0, y, w, h, 2);
  nvgFillColor(ctx, color);
  nvgFill(ctx);
  nvgStrokeColor(ctx, selected ? nvgRGBAf(1, 1, 1, alpha) : nvgRGBAf(0, 0, 0, alpha * 0.6f));
  nvgStrokeWidth(ctx, selected ? 1.5f : 1.0f);
  nvgStroke(ctx);
  nvgSave(ctx);
  nvgIntersectScissor(ctx, x0, y, w, h);
  // The track arrow: down for A to B, up for B to A.
  const float ax = x0 + 9, ay = y + h / 2;
  nvgBeginPath(ctx);
  if (t.settings.b_to_a) {
    nvgMoveTo(ctx, ax - 5, ay + 4);
    nvgLineTo(ctx, ax + 5, ay + 4);
    nvgLineTo(ctx, ax, ay - 5);
  } else {
    nvgMoveTo(ctx, ax - 5, ay - 4);
    nvgLineTo(ctx, ax + 5, ay - 4);
    nvgLineTo(ctx, ax, ay + 5);
  }
  nvgFillColor(ctx, nvgRGBAf(0.1f, 0.1f, 0.1f, alpha));
  nvgFill(ctx);
  if (const mpp::TransitionDef* def = mpp::find_transition(t.kind)) {
    nvgFontFace(ctx, "sans");
    nvgFontSize(ctx, 12);
    nvgTextAlign(ctx, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFillColor(ctx, nvgRGBAf(0.08f, 0.08f, 0.08f, alpha));
    nvgText(ctx, x0 + 18, ay, def->name.c_str(), nullptr);
  }
  nvgRestore(ctx);
}

void ConstructionView::draw_scroll_bar(NVGcontext* ctx) {
  const float top = m_size.y() - kScrollBarHeight;
  const float width = m_size.x() - kHeaderWidth;
  nvgBeginPath(ctx);
  nvgRect(ctx, kHeaderWidth, top, width, kScrollBarHeight);
  nvgFillColor(ctx, nvgRGB(30, 30, 30));
  nvgFill(ctx);
  const double visible = std::max<double>(1, visible_frames());
  const double total = std::max<double>(project_->duration() + visible, 1);
  const float thumb_w = static_cast<float>(std::max(20.0, width * visible / total));
  const float thumb_x = kHeaderWidth + static_cast<float>(width * scale_.scroll / total);
  nvgBeginPath(ctx);
  nvgRoundedRect(ctx, thumb_x, top + 2, std::min(thumb_w, kHeaderWidth + width - thumb_x),
                 kScrollBarHeight - 4, 3);
  nvgFillColor(ctx, nvgRGB(110, 110, 115));
  nvgFill(ctx);
}
