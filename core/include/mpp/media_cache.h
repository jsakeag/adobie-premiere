#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "mpp/media.h"

namespace mpp {

// Opens media files on demand and keeps recently decoded frames and whole
// decoded sound tracks. Safe to use from several threads: each source
// decodes one request at a time.
class MediaCache {
 public:
  // Opens a file as a MediaSource. Replaceable so tests can count opens or
  // supply synthetic sources.
  using Opener = std::function<std::unique_ptr<MediaSource>(const std::string&)>;

  explicit MediaCache(std::size_t frame_budget_bytes = std::size_t(256) << 20,
                      Opener opener = open_media);

  // All three throw std::runtime_error if the file can't be opened.
  MediaInfo info(const std::string& path);
  // A frame at the source's own size, clamped to the source's length.
  Image frame(const std::string& path, std::int64_t index);
  std::shared_ptr<const AudioBuffer> audio(const std::string& path, int sample_rate);

  // Forgets everything about a file, so the next use reopens it.
  void forget(const std::string& path);

  std::size_t cached_frames() const;

 private:
  struct Entry {
    std::mutex mutex;  // held while the source decodes
    std::unique_ptr<MediaSource> source;
    std::map<int, std::shared_ptr<const AudioBuffer>> audio;  // by sample rate
  };
  struct FrameKey {
    std::string path;
    std::int64_t index;
    bool operator==(const FrameKey& o) const { return index == o.index && path == o.path; }
  };
  struct FrameKeyHash {
    std::size_t operator()(const FrameKey& k) const {
      return std::hash<std::string>()(k.path) ^ (std::hash<std::int64_t>()(k.index) * 31);
    }
  };
  struct CachedFrame {
    Image image;
    std::list<FrameKey>::iterator age;
  };

  std::shared_ptr<Entry> entry(const std::string& path);
  void remember(const FrameKey& key, const Image& image);

  std::size_t budget_;
  Opener opener_;
  mutable std::mutex mutex_;  // guards the maps below, not the sources
  std::unordered_map<std::string, std::shared_ptr<Entry>> entries_;
  std::unordered_map<FrameKey, CachedFrame, FrameKeyHash> frames_;
  std::list<FrameKey> ages_;  // most recently used first
  std::size_t frame_bytes_ = 0;
};

}  // namespace mpp
