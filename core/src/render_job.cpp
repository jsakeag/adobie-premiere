#include "mpp/render_job.h"

#include <algorithm>
#include <exception>
#include <stdexcept>
#include <utility>

#include "mpp/audio_mixer.h"
#include "mpp/media_cache.h"
#include "mpp/renderer.h"

namespace mpp {

namespace {

// Enough cache for a little decoding slack per thread.
constexpr std::size_t kThreadCacheBytes = std::size_t(64) << 20;

}  // namespace

RenderJob::RenderJob(Project project, std::int64_t first, std::int64_t last, int threads)
    : project_(std::move(project)), first_(first), last_(std::max(first, last)) {
  const std::int64_t count = last_ - first_;
  result_.first = first_;
  result_.frames.resize(static_cast<std::size_t>(count));
  if (threads <= 0) {
    const int hardware = static_cast<int>(std::thread::hardware_concurrency());
    threads = std::clamp(hardware - 1, 1, 8);
  }
  threads = static_cast<int>(std::clamp<std::int64_t>(threads, 1, std::max<std::int64_t>(1, count)));
  running_ = threads + 1;
  threads_.emplace_back([this] { mix(); });
  for (int t = 0; t < threads; ++t) {
    const std::int64_t from = first_ + count * t / threads;
    const std::int64_t to = first_ + count * (t + 1) / threads;
    threads_.emplace_back([this, from, to] { render_run(from, to); });
  }
}

RenderJob::~RenderJob() {
  cancel();
  for (std::thread& t : threads_) {
    if (t.joinable()) t.join();
  }
}

double RenderJob::progress() const {
  const std::int64_t count = last_ - first_;
  return count > 0 ? static_cast<double>(rendered_.load()) / count : 1.0;
}

bool RenderJob::finished() const { return running_.load() == 0; }

void RenderJob::cancel() { cancelled_ = true; }

void RenderJob::fail(const std::string& why) {
  std::lock_guard<std::mutex> lock(error_mutex_);
  if (error_.empty()) error_ = why;
  cancelled_ = true;
}

void RenderJob::render_run(std::int64_t from, std::int64_t to) {
  try {
    MediaCache cache(kThreadCacheBytes);
    for (std::int64_t f = from; f < to && !cancelled_; ++f) {
      result_.frames[static_cast<std::size_t>(f - first_)] = render_frame(project_, cache, f);
      ++rendered_;
    }
  } catch (const std::exception& e) {
    fail(e.what());
  }
  --running_;
}

void RenderJob::mix() {
  try {
    MediaCache cache(kThreadCacheBytes);
    result_.audio = mix_audio(project_, cache, first_, last_);
  } catch (const std::exception& e) {
    fail(e.what());
  }
  --running_;
}

RenderedProgram RenderJob::take() {
  for (std::thread& t : threads_) {
    if (t.joinable()) t.join();
  }
  {
    std::lock_guard<std::mutex> lock(error_mutex_);
    if (!error_.empty()) throw std::runtime_error("Rendering failed: " + error_);
  }
  if (cancelled_) throw std::runtime_error("Rendering was cancelled");
  if (taken_) throw std::logic_error("RenderJob::take called twice");
  taken_ = true;
  return std::move(result_);
}

}  // namespace mpp
