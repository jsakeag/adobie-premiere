#pragma once

#include "mpp/image.h"

namespace mpp {

// Scales to exactly width x height with a tent filter that widens when
// shrinking, so reductions average rather than alias. Works on
// premultiplied colour, so transparent pixels don't bleed their colour.
Image resized(const Image& src, int width, int height);

// Scales to fit inside width x height keeping the aspect ratio, centred on
// `background` (letterboxed or pillarboxed). The picture's pixels replace
// the background rather than blending with it.
Image fit_into(const Image& src, int width, int height, Color background = kBlack);

// The size fit_into gives the picture itself, at least 1 x 1.
Point fitted_size(int src_width, int src_height, int width, int height);

}  // namespace mpp
