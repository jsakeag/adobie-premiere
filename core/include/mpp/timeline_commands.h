#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "mpp/command.h"

namespace mpp {

// Commands for the Construction window. Clips on one track never overlap;
// a command that would make them throws std::invalid_argument and changes
// nothing. A clip's linked partner (picture and sound placed together)
// moves, trims, splits and deletes with it.

// Places a bin item's marked range, or `in`..`out` if given. On a video
// track the picture goes there and, if the item has sound and `with_sound`
// is set, the sound goes on the first audio track that is free for that
// stretch (A, then B, then C). On an audio track only the sound is placed.
class PlaceClipCommand : public Command {
 public:
  PlaceClipCommand(int item, Track track, std::int64_t start, std::int64_t in,
                   std::int64_t out, bool with_sound = true)
      : item(item), track(track), start(start), in(in), out(out), with_sound(with_sound) {}
  std::string name() const override { return "Place"; }
  const int item;
  const Track track;
  const std::int64_t start;
  const std::int64_t in;
  const std::int64_t out;
  const bool with_sound;

 protected:
  void apply(Project& p) override;
};

// Moves a clip to `start` on `track` (a video track for a picture, an audio
// track for sound). Its linked clip shifts by the same time and keeps its
// track.
class MoveClipCommand : public Command {
 public:
  MoveClipCommand(int clip, Track track, std::int64_t start)
      : clip(clip), track(track), start(start) {}
  std::string name() const override { return "Move"; }
  const int clip;
  const Track track;
  const std::int64_t start;

 protected:
  void apply(Project& p) override;
};

// Sets a clip's start and source range, as dragging its edges does. The
// linked clip changes by the same amounts.
class TrimClipCommand : public Command {
 public:
  TrimClipCommand(int clip, std::int64_t start, std::int64_t in, std::int64_t out)
      : clip(clip), start(start), in(in), out(out) {}
  std::string name() const override { return "Trim"; }
  const int clip;
  const std::int64_t start;
  const std::int64_t in;
  const std::int64_t out;

 protected:
  void apply(Project& p) override;
};

// Cuts a clip, and its linked clip, in two at a timeline frame strictly
// inside it. The new right-hand pieces get new ids and link to each other.
class RazorCommand : public Command {
 public:
  RazorCommand(int clip, std::int64_t frame) : clip(clip), frame(frame) {}
  std::string name() const override { return "Razor"; }
  const int clip;
  const std::int64_t frame;

 protected:
  void apply(Project& p) override;
};

// Removes clips and their linked clips.
class DeleteClipsCommand : public Command {
 public:
  explicit DeleteClipsCommand(std::vector<int> clips) : clips(std::move(clips)) {}
  std::string name() const override { return "Clear"; }
  const std::vector<int> clips;

 protected:
  void apply(Project& p) override;
};

// The yellow work area bar: the frames [in, out) that Preview renders.
class SetWorkAreaCommand : public Command {
 public:
  SetWorkAreaCommand(std::int64_t in, std::int64_t out) : in(in), out(out) {}
  std::string name() const override { return "Work Area"; }
  const std::int64_t in;
  const std::int64_t out;

 protected:
  void apply(Project& p) override;
};

// Throws std::invalid_argument if a clip's item is missing, its track
// doesn't suit the item, its range runs outside the source, or it overlaps
// another clip on its track.
void check_clip(const Project& p, const Clip& clip);

// The first audio track free for [start, end), skipping clip `ignore`; or
// nullopt if all three are taken.
std::optional<Track> free_audio_track(const Project& p, std::int64_t start, std::int64_t end,
                                      int ignore = 0);

}  // namespace mpp
