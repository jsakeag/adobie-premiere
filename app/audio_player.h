#pragma once

#include <cstddef>
#include <memory>

#include "mpp/media.h"

// Plays stereo sound through the default output device (miniaudio) and
// reports how far it has got, which playback uses as its clock.
class AudioPlayer {
 public:
  AudioPlayer();
  ~AudioPlayer();

  AudioPlayer(const AudioPlayer&) = delete;
  AudioPlayer& operator=(const AudioPlayer&) = delete;

  // Off, play() does nothing and returns false (the self-test runs silent).
  void set_enabled(bool enabled) { enabled_ = enabled; }

  // Starts playing `sound` from sample frame `start`. Returns false when
  // there's no sound to play or no device, in which case the caller times
  // playback by the clock instead.
  bool play(std::shared_ptr<const mpp::AudioBuffer> sound, std::size_t start);
  void stop();
  bool playing() const;
  // Seconds of sound played since play().
  double position() const;

 private:
  struct Device;
  std::unique_ptr<Device> device_;
  bool enabled_ = true;
};
