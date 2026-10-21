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

const char* track_name(Track t) {
  switch (t) {
    case Track::VideoA: return "Video A";
    case Track::VideoB: return "Video B";
    case Track::Superimpose: return "Super";
    case Track::AudioA: return "Audio A";
    case Track::AudioB: return "Audio B";
    case Track::AudioC: return "Audio C";
  }
  return "";
}

const Clip* Project::clip(int id) const {
  for (const Clip& c : clips) {
    if (c.id == id) return &c;
  }
  return nullptr;
}

Clip* Project::clip(int id) {
  for (Clip& c : clips) {
    if (c.id == id) return &c;
  }
  return nullptr;
}

const Clip* Project::clip_at(Track track, std::int64_t frame) const {
  for (const Clip& c : clips) {
    if (c.track == track && c.covers(frame)) return &c;
  }
  return nullptr;
}

std::vector<const Clip*> Project::clips_on(Track track) const {
  std::vector<const Clip*> out;
  for (const Clip& c : clips) {
    if (c.track == track) out.push_back(&c);
  }
  std::sort(out.begin(), out.end(),
            [](const Clip* a, const Clip* b) { return a->start < b->start; });
  return out;
}

const Transition* Project::transition(int id) const {
  for (const Transition& t : transitions) {
    if (t.id == id) return &t;
  }
  return nullptr;
}

Transition* Project::transition(int id) {
  for (Transition& t : transitions) {
    if (t.id == id) return &t;
  }
  return nullptr;
}

const Transition* Project::transition_at(std::int64_t frame) const {
  for (const Transition& t : transitions) {
    if (t.covers(frame)) return &t;
  }
  return nullptr;
}

std::int64_t Project::duration() const {
  std::int64_t end = 0;
  for (const Clip& c : clips) end = std::max(end, c.end());
  for (const Transition& t : transitions) end = std::max(end, t.end());
  return end;
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
