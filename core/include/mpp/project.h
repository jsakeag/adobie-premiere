#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "mpp/keys.h"
#include "mpp/media.h"
#include "mpp/rate.h"
#include "mpp/transitions.h"

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

// The Construction window's tracks, top to bottom. The transition track
// sits between Video A and Video B and holds transitions, not clips.
enum class Track { VideoA, VideoB, Superimpose, AudioA, AudioB, AudioC };
inline constexpr Track kAllTracks[] = {Track::VideoA, Track::VideoB, Track::Superimpose,
                                       Track::AudioA, Track::AudioB, Track::AudioC};
inline constexpr Track kAudioTracks[] = {Track::AudioA, Track::AudioB, Track::AudioC};

inline bool is_audio_track(Track t) {
  return t == Track::AudioA || t == Track::AudioB || t == Track::AudioC;
}
inline bool is_video_track(Track t) { return !is_audio_track(t); }
const char* track_name(Track t);

// A handle on a clip's rubber band: the Super track's opacity or a sound
// clip's volume. `position` is in project frames into the source, so a
// handle stays with its picture or sound when the clip is trimmed.
struct FadePoint {
  double position = 0;
  double level = 1.0;  // 0..1

  bool operator==(const FadePoint& o) const { return position == o.position && level == o.level; }
};

// The rubber band's level at a source position: straight lines between
// handles, flat before the first and after the last, 1 with no handles.
double fade_level(const std::vector<FadePoint>& fade, double position);

// One use of a bin item on a track. `in` and `out` are project frames into
// the source (out exclusive); the clip covers timeline frames
// [start, end()). A picture and its sound placed together are two clips
// that name each other in `link`, and move, trim and delete together.
struct Clip {
  int id = 0;
  int item = 0;
  Track track = Track::VideoA;
  std::int64_t start = 0;
  std::int64_t in = 0;
  std::int64_t out = 0;
  int link = 0;
  std::vector<FadePoint> fade;  // sorted by position; empty is full level
  TransparencyKey key;          // used on the Super track

  std::int64_t length() const { return out - in; }
  std::int64_t end() const { return start + length(); }
  bool covers(std::int64_t frame) const { return frame >= start && frame < end(); }
};

// A transition on the T track, covering timeline frames [start, end()).
// It plays from the Video A picture to Video B (or B to A, per its
// settings) wherever it lies; where a track has no clip it reads as black.
struct Transition {
  int id = 0;
  std::string kind;  // a TransitionDef id
  std::int64_t start = 0;
  std::int64_t length = 1;
  TransitionSettings settings;

  std::int64_t end() const { return start + length; }
  bool covers(std::int64_t frame) const { return frame >= start && frame < end(); }
};

// Everything a project file holds. A plain value: Editor keeps a copy for
// undo, and the UI reads it but changes it only through commands.
struct Project {
  ProjectSettings settings;
  std::vector<MediaItem> bin;
  std::vector<Clip> clips;  // in no particular order
  std::vector<Transition> transitions;
  // The yellow bar over the ruler: what Preview renders and Export writes.
  std::int64_t work_in = 0;
  std::int64_t work_out = 0;
  int next_id = 1;  // ids are never reused within a project

  const MediaItem* item(int id) const;
  MediaItem* item(int id);
  // Index in `bin`, or -1.
  int item_index(int id) const;

  const Clip* clip(int id) const;
  Clip* clip(int id);
  // The clip on a track covering a frame, or nullptr.
  const Clip* clip_at(Track track, std::int64_t frame) const;
  // Clips on a track, sorted by start.
  std::vector<const Clip*> clips_on(Track track) const;
  const Transition* transition(int id) const;
  Transition* transition(int id);
  const Transition* transition_at(std::int64_t frame) const;

  // One past the last frame any clip or transition covers; 0 when empty.
  std::int64_t duration() const;
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
