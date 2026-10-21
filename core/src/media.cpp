#include "mpp/media.h"

#include <algorithm>
#include <cctype>

#include "mpp/ffmpeg_source.h"
#include "mpp/image_io.h"

namespace mpp {

namespace {

std::string lower_extension(const std::string& path) {
  const std::size_t slash = path.find_last_of("/\\");
  const std::size_t dot = path.find_last_of('.');
  if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return "";
  std::string ext = path.substr(dot + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return ext;
}

// One picture shown for as long as a clip lasts.
class StillSource : public MediaSource {
 public:
  explicit StillSource(const std::string& path) : image_(load_image(path)) {
    info_.kind = MediaKind::Still;
    info_.width = image_.width();
    info_.height = image_.height();
    info_.has_video = true;
  }

  const MediaInfo& info() const override { return info_; }
  Image frame(std::int64_t) override { return image_; }
  AudioBuffer audio(int sample_rate) override { return {sample_rate, {}}; }

 private:
  Image image_;
  MediaInfo info_;
};

}  // namespace

const char* media_kind_name(MediaKind kind) {
  switch (kind) {
    case MediaKind::Movie: return "Movie";
    case MediaKind::Still: return "Still";
    case MediaKind::Sequence: return "Sequence";
    case MediaKind::Title: return "Title";
    case MediaKind::Filmstrip: return "Filmstrip";
  }
  return "";
}

bool is_image_path(const std::string& path) {
  const std::string ext = lower_extension(path);
  return ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "bmp" ||
         ext == "tga" || ext == "gif";
}

std::unique_ptr<MediaSource> open_media(const std::string& path) {
  if (is_image_path(path)) return std::make_unique<StillSource>(path);
  return std::make_unique<FfmpegSource>(path);
}

}  // namespace mpp
