#include "sequence_window.h"

#include <algorithm>
#include <map>
#include <utility>

#include <nanogui/button.h>
#include <nanogui/combobox.h>
#include <nanogui/label.h>
#include <nanogui/layout.h>
#include <nanogui/opengl.h>
#include <nanogui/textbox.h>
#include <nanogui/vscrollpanel.h>

#include "frame_view.h"
#include "mpp/timeline_commands.h"
#include "mpp/transitions.h"

using nanogui::Vector2i;

namespace {

constexpr int kListWidth = 300;
constexpr int kRow = 40;
constexpr int kVisibleRows = 6;

}  // namespace

// The sequence, one row per item: its place, a thumbnail, its name and
// length. Click selects.
class SequenceList : public nanogui::Widget {
 public:
  struct Row {
    int item = 0;
    std::string name;
    std::string length;
  };

  SequenceList(nanogui::Widget* parent, SequenceWindowActions* actions)
      : nanogui::Widget(parent), actions_(actions) {
    set_rows({});
  }

  void set_rows(std::vector<Row> rows) {
    rows_ = std::move(rows);
    if (selected_ >= static_cast<int>(rows_.size())) selected_ = static_cast<int>(rows_.size()) - 1;
    set_fixed_size({kListWidth, std::max(1, static_cast<int>(rows_.size())) * kRow});
  }

  int selected() const { return selected_; }
  void select(int row) { selected_ = row >= 0 && row < static_cast<int>(rows_.size()) ? row : -1; }

  int insert_row(const Vector2i& screen) const {
    const Vector2i local = screen - absolute_position();
    if (local.x() < 0 || local.x() >= kListWidth || local.y() < -kRow / 2) return -1;
    return std::clamp((local.y() + kRow / 2) / kRow, 0, static_cast<int>(rows_.size()));
  }

  Vector2i preferred_size_impl(NVGcontext*) const override { return fixed_size(); }

  bool mouse_button_event(const Vector2i& p, int button, bool down, int) override {
    if (button == GLFW_MOUSE_BUTTON_LEFT && down) select((p.y() - m_pos.y()) / kRow);
    return true;
  }

  void draw(NVGcontext* ctx) override {
    for (NvgImage& image : stale_) image.release(ctx);
    stale_.clear();
    const float x = m_pos.x(), y = m_pos.y();
    nvgFontFace(ctx, "sans");
    for (std::size_t i = 0; i < rows_.size(); ++i) {
      const Row& row = rows_[i];
      const float top = y + static_cast<float>(i) * kRow;
      if (static_cast<int>(i) == selected_) {
        nvgBeginPath(ctx);
        nvgRect(ctx, x, top, kListWidth, kRow);
        nvgFillColor(ctx, nvgRGBA(70, 110, 180, 160));
        nvgFill(ctx);
      }
      nvgFontSize(ctx, 14);
      nvgFillColor(ctx, nvgRGB(160, 160, 160));
      nvgTextAlign(ctx, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
      nvgText(ctx, x + 24, top + kRow / 2.0f, std::to_string(i + 1).c_str(), nullptr);
      NvgImage& thumb = thumbs_[row.item];
      if (!thumb.valid() && actions_->thumbnail) {
        const mpp::Image picture = actions_->thumbnail(row.item);
        if (!picture.empty()) thumb.set(ctx, picture);
      }
      if (thumb.valid()) {
        const float tw = thumb.width(), th = thumb.height();
        NVGpaint paint = nvgImagePattern(ctx, x + 32, top + (kRow - th) / 2, tw, th, 0, thumb.id(), 1.0f);
        nvgBeginPath(ctx);
        nvgRect(ctx, x + 32, top + (kRow - th) / 2, tw, th);
        nvgFillPaint(ctx, paint);
        nvgFill(ctx);
      }
      nvgTextAlign(ctx, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
      nvgFontSize(ctx, 15);
      nvgFillColor(ctx, nvgRGB(230, 230, 230));
      nvgText(ctx, x + 86, top + kRow / 2.0f, row.name.c_str(), nullptr);
      nvgTextAlign(ctx, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
      nvgFontSize(ctx, 13);
      nvgFillColor(ctx, nvgRGB(190, 190, 190));
      nvgText(ctx, x + kListWidth - 6, top + kRow / 2.0f, row.length.c_str(), nullptr);
    }
    if (rows_.empty()) {
      nvgFontSize(ctx, 14);
      nvgFillColor(ctx, nvgRGB(150, 150, 150));
      nvgTextAlign(ctx, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
      nvgText(ctx, x + 8, y + kRow / 2.0f, "Drag items here from the Project window.", nullptr);
    }
  }

  // Thumbnails of items no longer in the list are freed on the next draw.
  void forget_missing() {
    for (auto it = thumbs_.begin(); it != thumbs_.end();) {
      const bool used = std::any_of(rows_.begin(), rows_.end(), [&](const Row& r) { return r.item == it->first; });
      if (used) {
        ++it;
      } else {
        stale_.push_back(it->second);
        it = thumbs_.erase(it);
      }
    }
  }

 private:
  SequenceWindowActions* actions_;
  std::vector<Row> rows_;
  int selected_ = -1;
  std::map<int, NvgImage> thumbs_;
  std::vector<NvgImage> stale_;
};

SequenceWindow::SequenceWindow(nanogui::Widget* parent, SequenceWindowActions actions)
    : nanogui::Window(parent, "Sequence"), actions_(std::move(actions)) {
  using nanogui::Alignment;
  using nanogui::BoxLayout;
  using nanogui::Button;
  using nanogui::Orientation;
  set_layout(new BoxLayout(Orientation::Vertical, Alignment::Fill, 6, 6));
  nanogui::VScrollPanel* scroll = new nanogui::VScrollPanel(this);
  scroll->set_fixed_size({kListWidth + 12, kVisibleRows * kRow});
  list_ = new SequenceList(scroll, &actions_);

  nanogui::Widget* edit = new nanogui::Widget(this);
  edit->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Middle, 0, 4));
  Button* add = new Button(edit, "Add");
  add->set_tooltip("Add the item selected in the Project window");
  add->set_callback([this] { if (actions_.add_selected) actions_.add_selected(); });
  Button* remove = new Button(edit, "Remove");
  remove->set_callback([this] { this->remove(); });
  Button* up = new Button(edit, "Up");
  up->set_callback([this] { move(-1); });
  Button* down = new Button(edit, "Down");
  down->set_callback([this] { move(1); });

  nanogui::Widget* options = new nanogui::Widget(this);
  options->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Middle, 0, 4));
  std::vector<std::string> names = {"Cuts only"};
  for (const auto& t : mpp::transitions()) names.push_back(t.name);
  transition_ = new nanogui::ComboBox(options, names);
  transition_->set_fixed_width(170);
  overlap_ = new nanogui::IntBox<int>(options, 15);
  overlap_->set_editable(true);
  overlap_->set_units("fr");
  overlap_->set_min_value(1);
  overlap_->set_fixed_width(70);
  overlap_->set_tooltip("Frames each clip overlaps the next, under the transition");

  nanogui::Widget* bottom = new nanogui::Widget(this);
  bottom->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Middle, 0, 8));
  Button* place = new Button(bottom, "Place at Edit Line");
  place->set_callback([this] { this->place(); });
  total_ = new nanogui::Label(bottom, "");
  total_->set_fixed_width(120);
}

void SequenceWindow::refresh(const mpp::Project& project) {
  items_ = project.sequence;
  std::vector<SequenceList::Row> rows;
  for (int id : items_) {
    const mpp::MediaItem* item = project.item(id);
    if (!item) continue;
    rows.push_back({id, item->name, mpp::format_timecode(item->out - item->in, project.settings.rate)});
  }
  list_->set_rows(std::move(rows));
  list_->forget_missing();
  const bool transitions = transition_->selected_index() > 0;
  total_->set_caption(mpp::format_timecode(
      mpp::sequence_length(project, items_, transitions, overlap_->value()), project.settings.rate));
}

int SequenceWindow::selected() const { return list_->selected(); }

void SequenceWindow::select(int row) { list_->select(row); }

int SequenceWindow::drop_row(const nanogui::Vector2i& screen) const {
  const Vector2i local = screen - absolute_position();
  if (local.x() < 0 || local.y() < 0 || local.x() >= width() || local.y() >= height()) return -1;
  const int row = list_->insert_row(screen);
  return row < 0 ? static_cast<int>(items_.size()) : row;
}

void SequenceWindow::choose(const std::string& transition, int overlap) {
  int index = 0;
  const auto& all = mpp::transitions();
  for (std::size_t i = 0; i < all.size(); ++i) {
    if (all[i].id == transition) index = static_cast<int>(i) + 1;
  }
  transition_->set_selected_index(index);
  overlap_->set_value(std::max(1, overlap));
}

void SequenceWindow::place() {
  const int index = transition_->selected_index();
  const std::string id = index > 0 ? mpp::transitions()[static_cast<std::size_t>(index - 1)].id : "";
  if (actions_.place) actions_.place(id, overlap_->value());
}

void SequenceWindow::remove() {
  const int row = selected();
  if (row < 0 || row >= static_cast<int>(items_.size())) return;
  std::vector<int> items = items_;
  items.erase(items.begin() + row);
  if (actions_.set) actions_.set(items);
}

void SequenceWindow::move(int steps) {
  const int row = selected(), to = row + steps;
  if (row < 0 || to < 0 || to >= static_cast<int>(items_.size())) return;
  std::vector<int> items = items_;
  std::swap(items[static_cast<std::size_t>(row)], items[static_cast<std::size_t>(to)]);
  list_->select(to);
  if (actions_.set) actions_.set(items);
}
