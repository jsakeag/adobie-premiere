#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include "mpp/image.h"
#include "mpp/rate.h"

namespace mpp {

enum class MovieFormat {
  H264,       // H.264 picture and AAC sound in an MPEG-4 (.mp4) file
  Animation,  // QuickTime Animation picture and PCM sound in a .mov: lossless
};

const char* movie_format_name(MovieFormat format);
// ".mp4" or ".mov".
const char* movie_extension(MovieFormat format);

struct MovieSettings {
  MovieFormat format = MovieFormat::H264;
  int width = 320;
  int height = 240;
  Rate rate;
  bool sound = true;
  int sample_rate = 22050;
  int sample_bits = 16;  // Animation movies keep 8-bit sound as 8-bit
};

// Encodes a movie through FFmpeg (libavformat, libavcodec, libswscale).
// Pictures go in one per frame, in order; sound goes in as interleaved
// stereo in any size of chunk. finish() must be called for a playable file.
class MovieWriter {
 public:
  // Throws std::runtime_error if the file can't be created or the format
  // can't take the settings (H.264 needs an even width and height).
  MovieWriter(const std::string& path, const MovieSettings& settings);
  ~MovieWriter();  // without finish(), closes and deletes the partial file

  MovieWriter(const MovieWriter&) = delete;
  MovieWriter& operator=(const MovieWriter&) = delete;

  // Throws std::runtime_error if encoding fails. Pictures must be the
  // movie's size.
  void write_picture(const Image& frame);
  void write_sound(const float* stereo, std::size_t frames);
  void finish();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace mpp
