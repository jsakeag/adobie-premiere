#include "mpp/image_io.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <stdexcept>
#include <vector>

// Static so these don't clash with the stb copy bundled inside NanoGUI.
#define STB_IMAGE_STATIC
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_FAILURE_USERMSG
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#include "stb_image.h"
#include "stb_image_write.h"
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace mpp {

Image load_image(const std::string& path) {
  int width = 0, height = 0, channels = 0;
  stbi_uc* pixels =
      stbi_load(path.c_str(), &width, &height, &channels, Image::kChannels);
  if (!pixels) {
    throw std::runtime_error("Could not load '" + path +
                             "': " + stbi_failure_reason());
  }
  Image image(width, height);
  std::memcpy(image.data(), pixels, image.size_bytes());
  stbi_image_free(pixels);
  return image;
}

void save_png(const Image& image, const std::string& path) {
  if (image.empty()) throw std::runtime_error("Cannot save an empty image");
  const int ok =
      stbi_write_png(path.c_str(), image.width(), image.height(),
                     Image::kChannels, image.data(),
                     image.width() * Image::kChannels);
  if (!ok) throw std::runtime_error("Could not write '" + path + "'");
}

void save_jpeg(const Image& image, const std::string& path, int quality) {
  if (image.empty()) throw std::runtime_error("Cannot save an empty image");
  std::vector<std::uint8_t> rgb(static_cast<std::size_t>(image.width()) *
                                image.height() * 3);
  const std::uint8_t* p = image.data();
  for (std::size_t i = 0; i < rgb.size() / 3; ++i, p += Image::kChannels) {
    // Over white: c * a + 255 * (1 - a).
    for (int ch = 0; ch < 3; ++ch) {
      rgb[i * 3 + ch] =
          static_cast<std::uint8_t>((p[ch] * p[3] + 255 * (255 - p[3]) + 127) / 255);
    }
  }
  const int ok = stbi_write_jpg(path.c_str(), image.width(), image.height(), 3,
                                rgb.data(), std::clamp(quality, 1, 100));
  if (!ok) throw std::runtime_error("Could not write '" + path + "'");
}

void save_image(const Image& image, const std::string& path) {
  const std::size_t dot = path.find_last_of('.');
  std::string ext = dot == std::string::npos ? "" : path.substr(dot);
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (ext == ".png") {
    save_png(image, path);
  } else if (ext == ".jpg" || ext == ".jpeg") {
    save_jpeg(image, path);
  } else {
    throw std::runtime_error("Export needs a .png, .jpg or .jpeg file name");
  }
}

}  // namespace mpp
