#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "mpp/media_cache.h"
#include "mpp/movie_writer.h"
#include "mpp/project.h"

namespace mpp {

// Writes frames [first, last) of the program to a movie at the project's
// frame size, rate and sound format. `keep_going` gets the fraction done
// before each frame; returning false stops, deleting the partial file, and
// throws std::runtime_error("cancelled"). Other failures throw too.
void export_movie(const Project& project, MediaCache& cache, const std::string& path,
                  MovieFormat format, std::int64_t first, std::int64_t last,
                  const std::function<bool(double)>& keep_going = nullptr);

// Writes frames [first, last) of the program as a Filmstrip PNG. Throws
// std::runtime_error.
void export_filmstrip(const Project& project, MediaCache& cache, const std::string& path,
                      std::int64_t first, std::int64_t last);

// export_movie on a background thread, with its own media readers.
class ExportJob {
 public:
  ExportJob(Project project, std::string path, MovieFormat format, std::int64_t first,
            std::int64_t last);
  ~ExportJob();  // cancels and waits

  ExportJob(const ExportJob&) = delete;
  ExportJob& operator=(const ExportJob&) = delete;

  const std::string& path() const { return path_; }
  double progress() const { return progress_.load(); }
  bool finished() const { return finished_.load(); }
  void cancel() { cancelled_ = true; }
  // Waits for the thread; then the error, empty if the movie was written.
  std::string wait();

 private:
  const Project project_;
  const std::string path_;
  std::atomic<double> progress_{0.0};
  std::atomic<bool> finished_{false};
  std::atomic<bool> cancelled_{false};
  std::mutex mutex_;
  std::string error_;
  std::thread thread_;
};

}  // namespace mpp
