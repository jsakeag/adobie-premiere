#include "mpp/project.h"

#include <algorithm>
#include <cmath>

namespace mpp {

const MediaItem* Project::item(int id) const {
  const int i = item_index(id);
  return i < 0 ? nullptr : &bin[static_cast<std::size_t>(i)];
}

MediaItem* Project::item(int id) {
  const int i = item_index(id);
  return i < 0 ? nullptr : &bin[static_cast<std::size_t>(i)];
}

int Project::item_index(int id) const {
  for (std::size_t i = 0; i < bin.size(); ++i) {
    if (bin[i].id == id) return static_cast<int>(i);
  }
  return -1;
}

std::optional<std::int64_t> source_length(const MediaInfo& info, Rate project_rate) {
  if (info.is_still()) return std::nullopt;
  const double frames = info.duration_seconds() * project_rate.fps();
  return std::max<std::int64_t>(1, static_cast<std::int64_t>(std::floor(frames + 1e-6)));
}

std::string file_name(const std::string& path) {
  const std::size_t slash = path.find_last_of("/\\");
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

MediaItem make_item(const std::string& path, const MediaInfo& info,
                    const ProjectSettings& settings) {
  MediaItem item;
  item.path = path;
  item.name = file_name(path);
  item.info = info;
  item.in = 0;
  item.out = source_length(info, settings.rate).value_or(settings.still_frames);
  return item;
}

std::int64_t source_frame(const MediaInfo& info, Rate project_rate, double position) {
  if (info.is_still()) return 0;
  const std::int64_t index = info.rate.frame_at(position * project_rate.den / project_rate.num);
  return std::clamp<std::int64_t>(index, 0, info.frames - 1);
}

}  // namespace mpp
