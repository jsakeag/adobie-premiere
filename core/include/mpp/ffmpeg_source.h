#pragma once

#include <memory>
#include <string>

#include "mpp/media.h"

namespace mpp {

// A movie or sound file read through FFmpeg (libavformat, libavcodec,
// libswscale, libswresample).
//
// Frames come back as RGBA8 at the file's size, keeping any alpha channel.
// Reading frames in order decodes forward; jumping seeks to the keyframe
// before the target and decodes up to it.
//
// For a file with sound and no picture, `info().rate` is the sample rate and
// `info().frames` the number of samples, so the duration stays exact.
class FfmpegSource : public MediaSource {
 public:
  // Throws std::runtime_error if FFmpeg can't open the file or it has
  // neither a video nor an audio stream.
  explicit FfmpegSource(const std::string& path);
  ~FfmpegSource() override;

  FfmpegSource(const FfmpegSource&) = delete;
  FfmpegSource& operator=(const FfmpegSource&) = delete;

  const MediaInfo& info() const override;
  Image frame(std::int64_t index) override;
  AudioBuffer audio(int sample_rate) override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace mpp
