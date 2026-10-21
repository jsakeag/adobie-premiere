#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "mpp/image.h"
#include "mpp/rate.h"

namespace mpp {

// A Filmstrip (Premiere 2.0's .flm, here a PNG): frames stacked top to
// bottom, each followed by a dark band with its timecode, so the whole
// stretch can be painted on in an image editor (mock-photoshop) and brought
// back as a clip. The first band's top row carries the frame size, count
// and rate as grey pixels; leave the bands alone and the strip reads back.
inline constexpr int kFilmstripBand = 16;

struct FilmstripInfo {
  int width = 0;
  int height = 0;
  int count = 0;
  Rate rate;
  std::int64_t first = 0;  // timeline frame of the first picture, for its label
};

// Stacks frames (all the same size) into a strip.
Image make_filmstrip(const std::vector<Image>& frames, Rate rate, std::int64_t first_frame);

// What a strip holds, or nullopt if the image isn't one (or its first band
// was painted over).
std::optional<FilmstripInfo> read_filmstrip_info(const Image& strip);

// One frame out of a strip; throws std::out_of_range for a bad index.
Image filmstrip_frame(const Image& strip, const FilmstripInfo& info, int index);

}  // namespace mpp
