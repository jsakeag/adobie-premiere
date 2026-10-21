#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "mpp/media.h"
#include "mpp/rate.h"

namespace mpp {

// The output format: what Preview shows and Export writes.
struct ProjectSettings {
  int width = 320;
  int height = 240;
  Rate rate{30, 1, false};
  int audio_rate = 22050;
  int audio_bits = 8;               // 8 as in Premiere 1.0, or 16 (2.0)
  std::int64_t still_frames = 150;  // how long a still lasts when placed

  bool operator==(const ProjectSettings& o) const {
    return width == o.width && height == o.height && rate == o.rate &&
           audio_rate == o.audio_rate && audio_bits == o.audio_bits &&
           still_frames == o.still_frames;
  }
  bool operator!=(const ProjectSettings& o) const { return !(*this == o); }
};

// A file in the Project window. `in` and `out` are the marks set in the
// Clip window, in project frames from the start of the source (out is
// exclusive); placing the item uses that range.
struct MediaItem {
  int id = 0;
  std::string path;
  std::string name;
  MediaInfo info;
  std::int64_t in = 0;
  std::int64_t out = 0;
};

// Everything a project file holds. A plain value: Editor keeps a copy for
// undo, and the UI reads it but changes it only through commands.
struct Project {
  ProjectSettings settings;
  std::vector<MediaItem> bin;
  int next_id = 1;  // ids are never reused within a project

  const MediaItem* item(int id) const;
  MediaItem* item(int id);
  // Index in `bin`, or -1.
  int item_index(int id) const;
};

// How long a source runs in project frames, or nullopt for a still.
std::optional<std::int64_t> source_length(const MediaInfo& info, Rate project_rate);

// A new bin item for a file: name from the file name, marks around all of
// it (a still gets `still_frames`).
MediaItem make_item(const std::string& path, const MediaInfo& info,
                    const ProjectSettings& settings);

// The source frame showing `position` project frames into a source.
std::int64_t source_frame(const MediaInfo& info, Rate project_rate, double position);

// "clip.mov" from "/a/b/clip.mov".
std::string file_name(const std::string& path);

}  // namespace mpp
