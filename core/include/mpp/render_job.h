#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "mpp/image.h"
#include "mpp/media.h"
#include "mpp/project.h"

namespace mpp {

// A rendered stretch of the program: the Preview that plays back.
struct RenderedProgram {
  std::int64_t first = 0;     // the timeline frame frames[0] shows
  std::vector<Image> frames;  // one per frame, at the project's size
  AudioBuffer audio;          // the mixed sound, from the start of `first`
};

// Renders frames [first, last) of a copy of a project in the background,
// the way Premiere built a preview before playing it. Frames are split into
// contiguous runs, one per thread, each with its own media readers so they
// decode forward without fighting over a file; the sound is mixed on one
// more thread.
class RenderJob {
 public:
  // `threads` 0 picks a number from the hardware.
  RenderJob(Project project, std::int64_t first, std::int64_t last, int threads = 0);
  ~RenderJob();  // cancels and waits

  RenderJob(const RenderJob&) = delete;
  RenderJob& operator=(const RenderJob&) = delete;

  std::int64_t first() const { return first_; }
  std::int64_t last() const { return last_; }
  // Fraction of the frames rendered so far.
  double progress() const;
  // True once every thread has stopped: done, cancelled or failed.
  bool finished() const;
  void cancel();
  // Waits for the threads, then hands over the result. Throws
  // std::runtime_error if the job was cancelled or a frame failed.
  RenderedProgram take();

 private:
  void render_run(std::int64_t from, std::int64_t to);
  void mix();
  void fail(const std::string& why);

  const Project project_;
  const std::int64_t first_;
  const std::int64_t last_;
  RenderedProgram result_;
  std::vector<std::thread> threads_;
  std::atomic<std::int64_t> rendered_{0};
  std::atomic<int> running_{0};
  std::atomic<bool> cancelled_{false};
  std::mutex error_mutex_;
  std::string error_;
  bool taken_ = false;
};

}  // namespace mpp
