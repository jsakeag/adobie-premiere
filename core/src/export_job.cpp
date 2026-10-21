#include "mpp/export_job.h"

#include <algorithm>
#include <exception>
#include <stdexcept>
#include <utility>

#include "mpp/audio_mixer.h"
#include "mpp/renderer.h"

namespace mpp {

void export_movie(const Project& project, MediaCache& cache, const std::string& path,
                  MovieFormat format, std::int64_t first, std::int64_t last,
                  const std::function<bool(double)>& keep_going) {
  if (last <= first) throw std::runtime_error("There's nothing to export");
  const ProjectSettings& s = project.settings;
  const AudioBuffer sound = mix_audio(project, cache, first, last);

  MovieSettings movie;
  movie.format = format;
  movie.width = s.width;
  movie.height = s.height;
  movie.rate = s.rate;
  movie.sample_rate = s.audio_rate;
  movie.sample_bits = s.audio_bits;
  MovieWriter writer(path, movie);

  const std::int64_t base = frame_to_sample(s, first);
  std::size_t written = 0;  // sound frames handed to the writer
  for (std::int64_t f = first; f < last; ++f) {
    if (keep_going && !keep_going(static_cast<double>(f - first) / (last - first))) {
      throw std::runtime_error("cancelled");
    }
    writer.write_picture(render_frame(project, cache, f));
    // The sound up to the end of this frame, so the two stay interleaved.
    const std::size_t until = std::min(sound.frames(), static_cast<std::size_t>(frame_to_sample(s, f + 1) - base));
    if (until > written) {
      writer.write_sound(sound.samples.data() + written * 2, until - written);
      written = until;
    }
  }
  writer.finish();
}

ExportJob::ExportJob(Project project, std::string path, MovieFormat format, std::int64_t first,
                     std::int64_t last)
    : project_(std::move(project)), path_(std::move(path)) {
  thread_ = std::thread([this, format, first, last] {
    try {
      MediaCache cache(std::size_t(64) << 20);
      export_movie(project_, cache, path_, format, first, last, [this](double done) {
        progress_ = done;
        return !cancelled_.load();
      });
      progress_ = 1.0;
    } catch (const std::exception& e) {
      std::lock_guard<std::mutex> lock(mutex_);
      error_ = e.what();
    }
    finished_ = true;
  });
}

ExportJob::~ExportJob() {
  cancel();
  if (thread_.joinable()) thread_.join();
}

std::string ExportJob::wait() {
  if (thread_.joinable()) thread_.join();
  std::lock_guard<std::mutex> lock(mutex_);
  return error_;
}

}  // namespace mpp
