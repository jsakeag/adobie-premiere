#pragma once

#include "mpp/image.h"

namespace mpp {

// Draws `top` over `base` (straight alpha, same size), with top's alpha
// scaled by `opacity` (0..1). Throws std::invalid_argument on a size
// mismatch.
void composite_over(Image& base, const Image& top, double opacity = 1.0);

// Mixes two pictures of the same size: (1 - t) * a + t * b per channel,
// alpha included.
Image mix(const Image& a, const Image& b, double t);

// `image` flattened onto an opaque colour.
Image flattened(const Image& image, Color background = kBlack);

}  // namespace mpp
