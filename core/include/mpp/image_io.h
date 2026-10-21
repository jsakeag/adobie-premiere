#pragma once

#include <string>

#include "mpp/image.h"

namespace mpp {

// Loads PNG, JPEG, BMP, TGA or GIF as RGBA8. Throws std::runtime_error.
Image load_image(const std::string& path);

// Throws std::runtime_error if the file can't be written.
void save_png(const Image& image, const std::string& path);

// JPEG has no transparency, so the image is flattened onto white first.
// `quality` is 1..100. Throws std::runtime_error if the file can't be
// written.
void save_jpeg(const Image& image, const std::string& path, int quality = 90);

// PNG or JPEG, chosen by the extension (.png, .jpg, .jpeg; any case).
// Throws std::runtime_error for any other extension.
void save_image(const Image& image, const std::string& path);

}  // namespace mpp
