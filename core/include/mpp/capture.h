#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "mpp/image.h"
#include "mpp/movie_writer.h"
#include "mpp/project.h"

namespace mpp {

struct CaptureDevice {
  int index = 0;  // AVFoundation's number for it
  std::string name;
};

struct CaptureDevices {
  std::vector<CaptureDevice> cameras;
  std::vector<CaptureDevice> microphones;
};

// Reads the list AVFoundation's input writes to the log when asked for its
// devices: "AVFoundation video devices:", then "[0] FaceTime HD Camera" and
// so on, then the audio devices. Log prefixes ("[AVFoundation indev @ 0x..]")
// are skipped.
CaptureDevices parse_device_list(const std::string& log);

// The cameras and microphones AVFoundation can see. Listing needs no
// permission; recording does. Empty where there's no AVFoundation input.
CaptureDevices list_capture_devices();

// What to record from: a libavdevice input format, its URL and options.
struct CaptureInput {
  std::string format;  // "avfoundation" or "lavfi"
  std::string url;
  std::vector<std::pair<std::string, std::string>> options;
};

// A camera and a microphone by AVFoundation index; -1 leaves one out.
CaptureInput device_input(int camera, int microphone);

// libavfilter's moving test pattern and a 440 Hz tone (if `sound`), at the
// project's frame size and rate and paced like a real camera: it stands in
// for one.
CaptureInput test_pattern_input(const ProjectSettings& settings, bool sound);

// Opens an input on a background thread and keeps reading it. The newest
// picture, fitted into the project's frame, is there to show; record()
// writes what follows into a movie at the project's frame size, rate and
// sound format. Each movie frame gets the newest picture at its time, so a
// camera at another rate drops or repeats pictures. Sound is kept for a
// moment even when not recording, so a recording's sound starts exactly
// with its first picture; it ends padded or cut to the pictures' length.
class CaptureSession {
 public:
  CaptureSession(CaptureInput input, ProjectSettings settings);
  ~CaptureSession();  // stops; a recording in progress is deleted

  CaptureSession(const CaptureSession&) = delete;
  CaptureSession& operator=(const CaptureSession&) = delete;

  // Waits until the input is open or has failed; then the error, empty if
  // it's open.
  std::string wait_open();
  bool open() const { return open_.load(); }
  bool has_sound() const { return has_sound_.load(); }
  // Why the input couldn't be opened or stopped working; empty if fine.
  std::string error() const;

  // The newest picture, and how many pictures have come in so far.
  Image picture() const;
  std::uint64_t pictures() const { return pictures_.load(); }

  // Starts a movie at `path` with the pictures and sound from the next
  // picture on; `frames` > 0 ends it by itself after that many. Returns the
  // error, empty if recording has started.
  std::string record(const std::string& path, MovieFormat format, std::int64_t frames = 0);
  // True from record() until the movie is finished: by stop(), by reaching
  // its frame count, or because the input ended.
  bool recording() const { return recording_.load(); }
  std::int64_t recorded_frames() const { return recorded_.load(); }
  // Ends the recording and waits for the movie to be finished. Returns its
  // error, empty if the movie was written.
  std::string stop();

 private:
  struct Reader;
  void run();

  const CaptureInput input_;
  const ProjectSettings settings_;
  std::atomic<bool> quit_{false};
  std::atomic<bool> open_{false};
  std::atomic<bool> has_sound_{false};
  std::atomic<std::uint64_t> pictures_{0};
  std::atomic<bool> recording_{false};
  std::atomic<bool> stop_requested_{false};
  std::atomic<std::int64_t> recorded_{0};

  mutable std::mutex mutex_;
  std::condition_variable changed_;
  bool settled_ = false;  // the input opened or failed
  std::string error_;
  Image picture_;
  // A movie record() started, waiting for the reading thread to take it.
  std::unique_ptr<MovieWriter> next_writer_;
  std::int64_t next_frames_ = 0;
  std::string result_;  // the last recording's error

  std::thread thread_;
};

}  // namespace mpp
