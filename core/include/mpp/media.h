#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "mpp/image.h"
#include "mpp/rate.h"

namespace mpp {

enum class MediaKind { Movie, Still, Sequence, Title, Filmstrip };

const char* media_kind_name(MediaKind kind);

// What a source holds. A still has no length of its own (frames == 0): it
// shows the same picture for as long as a clip asks.
struct MediaInfo {
  MediaKind kind = MediaKind::Movie;
  int width = 0;
  int height = 0;
  Rate rate;                // the source's own frame rate
  std::int64_t frames = 0;  // source frames; 0 for a still
  bool has_video = false;
  bool has_audio = false;
  int sample_rate = 0;  // of the file's audio, before resampling
  int channels = 0;

  bool is_still() const { return frames == 0; }
  double duration_seconds() const { return is_still() ? 0.0 : rate.seconds(frames); }
};

// Interleaved stereo samples in -1..1 at one sample rate.
struct AudioBuffer {
  int sample_rate = 0;
  std::vector<float> samples;  // left, right, left, right, ...

  std::size_t frames() const { return samples.size() / 2; }
};

// Anything that yields pictures and sound: a movie file, a still, an image
// sequence, a title. Not thread-safe; MediaCache serializes access.
class MediaSource {
 public:
  virtual ~MediaSource() = default;

  virtual const MediaInfo& info() const = 0;
  // The picture at a source frame, at the source's own size. Indexes past
  // either end are clamped. A source with no video returns a black frame.
  virtual Image frame(std::int64_t index) = 0;
  // The whole sound track as stereo at `sample_rate`; empty if none.
  virtual AudioBuffer audio(int sample_rate) = 0;
};

// Opens a file by its extension: images become stills, anything else goes
// to FFmpeg. Throws std::runtime_error if the file can't be read.
std::unique_ptr<MediaSource> open_media(const std::string& path);

// Whether open_media would treat the file as a still image.
bool is_image_path(const std::string& path);

}  // namespace mpp
