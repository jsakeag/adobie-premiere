#include "mpp/bin_commands.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace mpp {

void RemoveItemCommand::apply(Project& p) {
  const int i = p.item_index(id);
  if (i < 0) throw std::invalid_argument("No such item in the Project window");
  p.bin.erase(p.bin.begin() + i);
  p.clips.erase(std::remove_if(p.clips.begin(), p.clips.end(),
                               [this](const Clip& c) { return c.item == id; }),
                p.clips.end());
}

void SetSettingsCommand::apply(Project& p) {
  if (settings.width < 16 || settings.height < 16 || settings.width > 4096 ||
      settings.height > 4096) {
    throw std::invalid_argument("Frame size must be between 16 and 4096 pixels");
  }
  if (settings.rate.num <= 0 || settings.rate.den <= 0) {
    throw std::invalid_argument("Frame rate must be positive");
  }
  if (settings.audio_bits != 8 && settings.audio_bits != 16) {
    throw std::invalid_argument("Audio must be 8 or 16 bit");
  }
  if (settings.audio_rate < 8000 || settings.audio_rate > 96000) {
    throw std::invalid_argument("Audio rate must be between 8 and 96 kHz");
  }
  if (settings.still_frames < 1) throw std::invalid_argument("Still length must be at least one frame");
  if (settings.rate != p.settings.rate) {
    // Floor both ends of every range, so clips that touched still touch
    // and none overlap.
    const double scale = settings.rate.fps() / p.settings.rate.fps();
    const auto at = [scale](std::int64_t frame) {
      return static_cast<std::int64_t>(std::floor(frame * scale + 1e-9));
    };
    for (MediaItem& item : p.bin) {
      const std::int64_t in = at(item.in);
      std::int64_t out = std::max(in + 1, at(item.out));
      if (const auto length = source_length(item.info, settings.rate)) {
        out = std::min(out, *length);
      }
      item.in = std::min(in, out - 1);
      item.out = out;
    }
    for (Clip& clip : p.clips) {
      const std::int64_t start = at(clip.start);
      const std::int64_t length = std::max<std::int64_t>(1, at(clip.end()) - start);
      clip.start = start;
      clip.in = at(clip.in);
      clip.out = clip.in + length;
      const MediaItem* item = p.item(clip.item);
      const auto source = item ? source_length(item->info, settings.rate) : std::nullopt;
      if (source && clip.out > *source) {
        clip.out = *source;
        clip.in = std::min(clip.in, clip.out - 1);
      }
    }
    for (Transition& t : p.transitions) {
      const std::int64_t start = at(t.start);
      t.length = std::max<std::int64_t>(1, at(t.end()) - start);
      t.start = start;
    }
    p.work_in = at(p.work_in);
    p.work_out = std::max(p.work_in + 1, at(p.work_out));
  }
  p.settings = settings;
}

}  // namespace mpp
