#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace mpp {

// A frame rate as a fraction (30000/1001 for NTSC's 29.97), plus whether
// timecode at this rate counts in drop-frame.
struct Rate {
  int num = 30;
  int den = 1;
  bool drop_frame = false;

  double fps() const { return static_cast<double>(num) / den; }
  // Whole frames per second as timecode counts them: 30 for 29.97.
  int nominal() const;
  // Drop-frame needs a rate of 29.97 or 59.94.
  bool can_drop_frame() const { return den == 1001 && (num == 30000 || num == 60000); }

  double seconds(std::int64_t frames) const { return frames * static_cast<double>(den) / num; }
  // The frame showing at `seconds`: the last frame that starts at or before it.
  std::int64_t frame_at(double seconds) const;

  bool operator==(const Rate& o) const {
    return num == o.num && den == o.den && drop_frame == o.drop_frame;
  }
  bool operator!=(const Rate& o) const { return !(*this == o); }
};

// SMPTE timecode for a frame count: "HH:MM:SS:FF", or "HH:MM:SS;FF" in
// drop-frame (which skips frame labels 00 and 01 at the start of every
// minute except each tenth, so the clock keeps up with 29.97 fps).
// Negative counts get a leading '-'.
std::string format_timecode(std::int64_t frames, Rate rate);

// Reads "HH:MM:SS:FF" (either separator before the frames, and fewer
// fields, so "1:15" is one second and 15 frames). Returns nullopt for
// malformed text, out-of-range fields, or a label drop-frame skips.
std::optional<std::int64_t> parse_timecode(const std::string& text, Rate rate);

// Short rate label for menus: "29.97 fps DF", "15 fps".
std::string rate_name(Rate rate);

}  // namespace mpp
