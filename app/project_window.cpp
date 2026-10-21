#include "project_window.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>

#include <nanogui/button.h>
#include <nanogui/label.h>
#include <nanogui/layout.h>
#include <nanogui/opengl.h>
#include <nanogui/screen.h>
#include <nanogui/vscrollpanel.h>

#include "mpp/rate.h"

using nanogui::Vector2i;

namespace {

constexpr int kListWidth = 392;
constexpr int kRowHeight = 44;
constexpr int kThumbW = 48;
constexpr int kThumbH = 36;
constexpr int kVisibleRows = 6;

}  // namespace

// The rows of the Project window, drawn rather than built from child
// widgets so the list itself receives clicks and drags.
class BinList : public nanogui::Widget {
 public:
  struct Row {
    int id = 0;
    std::string name;
    std::string detail;    // "Movie  160 x 120  15 fps  Sound"
    std::string duration;  // timecode, or "Still"
    std::string thumb_key;  // changes when the thumbnail should change
    bool sound_only = false;
  };

  BinList(nanogui::Widget* parent, ProjectWindowActions* actions)
      : nanogui::Widget(parent), actions_(actions) {}

  void set_rows(std::vector<Row> rows, int selected) {
    std::map<int, Thumb> kept;
    for (const Row& row : rows) {
      auto it = thumbs_.find(row.id);
      if (it != thumbs_.end() && it->second.key == row.thumb_key) {
        kept.emplace(row.id, it->second);
        thumbs_.erase(it);
      }
    }
    for (auto& [id, thumb] : thumbs_) stale_.push_back(thumb.image);
    thumbs_ = std::move(kept);
    rows_ = std::move(rows);
    selected_ = selected;
    set_fixed_size({kListWidth, std::max(1, static_cast<int>(rows_.size())) * kRowHeight});
  }

  Vector2i row_center(int id) const {
    for (std::size_t i = 0; i < rows_.size(); ++i) {
      if (rows_[i].id == id) {
        return absolute_position() + Vector2i(kListWidth / 2, static_cast<int>(i) * kRowHeight + kRowHeight / 2);
      }
    }
    return {-1, -1};
  }

  Vector2i preferred_size_impl(NVGcontext*) const override { return fixed_size(); }

  bool mouse_button_event(const Vector2i& p, int button, bool down, int modifiers) override {
    if (button != GLFW_MOUSE_BUTTON_LEFT) return true;
    const Vector2i local = p - m_pos;
    if (down) {
      const int index = local.y() / kRowHeight;
      press_id_ = index >= 0 && index < static_cast<int>(rows_.size()) ? rows_[index].id : 0;
      press_at_ = local;
      dragging_ = false;
      if (press_id_ && actions_->select) actions_->select(press_id_);
      if (press_id_ && (modifiers & nanogui::MOD_DOUBLE_CLICK) && actions_->open) {
        actions_->open(press_id_);
        press_id_ = 0;
      }
    } else {
      if (dragging_ && actions_->drag) actions_->drag(press_id_, p + parent()->absolute_position(), true);
      dragging_ = false;
      press_id_ = 0;
    }
    return true;
  }

  bool mouse_drag_event(const Vector2i& p, const Vector2i&, int, int) override {
    if (!press_id_) return true;
    const Vector2i local = p - m_pos;
    if (!dragging_ && (std::abs(local.x() - press_at_.x()) > 4 || std::abs(local.y() - press_at_.y()) > 4)) {
      dragging_ = true;
    }
    if (dragging_ && actions_->drag) actions_->drag(press_id_, p + parent()->absolute_position(), false);
    return true;
  }

  void draw(NVGcontext* ctx) override {
    for (NvgImage& image : stale_) image.release(ctx);
    stale_.clear();
    const float x = m_pos.x(), y = m_pos.y();
    nvgFontFace(ctx, "sans");
    for (std::size_t i = 0; i < rows_.size(); ++i) {
      const Row& row = rows_[i];
      const float top = y + static_cast<float>(i) * kRowHeight;
      if (row.id == selected_) {
        nvgBeginPath(ctx);
        nvgRect(ctx, x, top, kListWidth, kRowHeight);
        nvgFillColor(ctx, nvgRGBA(70, 110, 180, 160));
        nvgFill(ctx);
      }
      draw_thumbnail(ctx, row, x + 4, top + (kRowHeight - kThumbH) / 2.0f);
      nvgFontSize(ctx, 16);
      nvgFillColor(ctx, nvgRGB(235, 235, 235));
      nvgTextAlign(ctx, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
      nvgText(ctx, x + kThumbW + 12, top + 5, row.name.c_str(), nullptr);
      nvgFontSize(ctx, 13);
      nvgFillColor(ctx, nvgRGB(170, 170, 170));
      nvgText(ctx, x + kThumbW + 12, top + 24, row.detail.c_str(), nullptr);
      nvgTextAlign(ctx, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
      nvgFontSize(ctx, 14);
      nvgFillColor(ctx, nvgRGB(220, 220, 220));
      nvgText(ctx, x + kListWidth - 8, top + 6, row.duration.c_str(), nullptr);
      nvgBeginPath(ctx);
      nvgMoveTo(ctx, x, top + kRowHeight - 0.5f);
      nvgLineTo(ctx, x + kListWidth, top + kRowHeight - 0.5f);
      nvgStrokeColor(ctx, nvgRGBA(255, 255, 255, 30));
      nvgStroke(ctx);
    }
    if (rows_.empty()) {
      nvgFontSize(ctx, 14);
      nvgFillColor(ctx, nvgRGB(150, 150, 150));
      nvgTextAlign(ctx, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
      nvgText(ctx, x + 10, y + kRowHeight / 2.0f, "Import files with File > Import, or drop them here.",
              nullptr);
    }
  }

 private:
  struct Thumb {
    std::string key;
    NvgImage image;
  };

  void draw_thumbnail(NVGcontext* ctx, const Row& row, float x, float y) {
    nvgBeginPath(ctx);
    nvgRect(ctx, x, y, kThumbW, kThumbH);
    nvgFillColor(ctx, nvgRGB(0, 0, 0));
    nvgFill(ctx);
    if (row.sound_only) {
      // A little waveform for sound files.
      nvgBeginPath(ctx);
      for (int i = 0; i <= 40; ++i) {
        const float px = x + 4 + i, amp = 12.0f * std::abs(std::sin(i * 0.7f)) * (i % 5 + 1) / 5;
        nvgMoveTo(ctx, px, y + kThumbH / 2.0f - amp);
        nvgLineTo(ctx, px, y + kThumbH / 2.0f + amp);
      }
      nvgStrokeColor(ctx, nvgRGB(120, 220, 120));
      nvgStroke(ctx);
      return;
    }
    Thumb& thumb = thumbs_[row.id];
    if (!thumb.image.valid() && actions_->thumbnail) {
      thumb.key = row.thumb_key;
      const mpp::Image picture = actions_->thumbnail(row.id);
      if (!picture.empty()) thumb.image.set(ctx, picture);
    }
    if (!thumb.image.valid()) return;
    const float w = thumb.image.width(), h = thumb.image.height();
    const float tx = x + (kThumbW - w) / 2, ty = y + (kThumbH - h) / 2;
    NVGpaint paint = nvgImagePattern(ctx, tx, ty, w, h, 0, thumb.image.id(), 1.0f);
    nvgBeginPath(ctx);
    nvgRect(ctx, tx, ty, w, h);
    nvgFillPaint(ctx, paint);
    nvgFill(ctx);
  }

  ProjectWindowActions* actions_;
  std::vector<Row> rows_;
  int selected_ = 0;
  std::map<int, Thumb> thumbs_;
  std::vector<NvgImage> stale_;  // freed on the next draw
  int press_id_ = 0;
  Vector2i press_at_{0, 0};
  bool dragging_ = false;
};

ProjectWindow::ProjectWindow(nanogui::Widget* parent, ProjectWindowActions actions)
    : nanogui::Window(parent, "Project"), actions_(std::move(actions)) {
  set_layout(new nanogui::BoxLayout(nanogui::Orientation::Vertical,
                                    nanogui::Alignment::Fill, 6, 6));
  nanogui::Widget* tools = new nanogui::Widget(this);
  tools->set_layout(new nanogui::BoxLayout(nanogui::Orientation::Horizontal,
                                           nanogui::Alignment::Middle, 0, 6));
  nanogui::Button* import = new nanogui::Button(tools, "Import...");
  import->set_callback([this] { if (actions_.import) actions_.import(); });
  nanogui::Button* clear = new nanogui::Button(tools, "Clear");
  clear->set_callback([this] { if (actions_.clear) actions_.clear(); });
  count_ = new nanogui::Label(tools, "0 items");
  count_->set_fixed_width(200);

  scroll_ = new nanogui::VScrollPanel(this);
  scroll_->set_fixed_size({kListWidth + 12, kVisibleRows * kRowHeight});
  list_ = new BinList(scroll_, &actions_);
  list_->set_rows({}, 0);
}

void ProjectWindow::refresh(const mpp::Project& project, int selected) {
  std::vector<BinList::Row> rows;
  const mpp::Rate rate = project.settings.rate;
  for (const mpp::MediaItem& item : project.bin) {
    BinList::Row row;
    row.id = item.id;
    row.name = item.name;
    const mpp::MediaInfo& info = item.info;
    char detail[160];
    if (info.has_video) {
      std::snprintf(detail, sizeof(detail), "%s   %d x %d%s%s%s",
                    mpp::media_kind_name(info.kind), info.width, info.height,
                    info.is_still() ? "" : "   ",
                    info.is_still() ? "" : mpp::rate_name(info.rate).c_str(),
                    info.has_audio ? "   Sound" : "");
    } else {
      std::snprintf(detail, sizeof(detail), "Sound   %.4g kHz", info.sample_rate / 1000.0);
    }
    row.detail = detail;
    const auto length = mpp::source_length(info, rate);
    row.duration = length ? mpp::format_timecode(*length, rate) : "Still";
    row.thumb_key = item.path + "@" + std::to_string(item.in);
    row.sound_only = !info.has_video;
    rows.push_back(std::move(row));
  }
  list_->set_rows(std::move(rows), selected);
  const std::size_t n = project.bin.size();
  count_->set_caption(std::to_string(n) + (n == 1 ? " item" : " items"));
}

nanogui::Vector2i ProjectWindow::row_center(int id) const { return list_->row_center(id); }
