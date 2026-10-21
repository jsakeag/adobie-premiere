#include "mpp/media_cache.h"

#include <utility>

namespace mpp {

MediaCache::MediaCache(std::size_t frame_budget_bytes, Opener opener)
    : budget_(frame_budget_bytes), opener_(std::move(opener)) {}

std::shared_ptr<MediaCache::Entry> MediaCache::entry(const std::string& path) {
  std::shared_ptr<Entry> e;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& slot = entries_[path];
    if (!slot) slot = std::make_shared<Entry>();
    e = slot;
  }
  std::lock_guard<std::mutex> lock(e->mutex);
  if (!e->source) e->source = opener_(path);  // throws; the next call retries
  return e;
}

MediaInfo MediaCache::info(const std::string& path) {
  std::shared_ptr<Entry> e = entry(path);
  std::lock_guard<std::mutex> lock(e->mutex);
  return e->source->info();
}

Image MediaCache::frame(const std::string& path, std::int64_t index) {
  std::shared_ptr<Entry> e = entry(path);
  FrameKey key{path, index};
  {
    std::lock_guard<std::mutex> lock(e->mutex);
    const MediaInfo& info = e->source->info();
    if (info.is_still()) key.index = 0;
    else if (key.index < 0) key.index = 0;
    else if (key.index >= info.frames) key.index = info.frames - 1;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = frames_.find(key);
    if (it != frames_.end()) {
      ages_.splice(ages_.begin(), ages_, it->second.age);
      return it->second.image;
    }
  }
  Image image;
  {
    std::lock_guard<std::mutex> lock(e->mutex);
    image = e->source->frame(key.index);
  }
  remember(key, image);
  return image;
}

void MediaCache::remember(const FrameKey& key, const Image& image) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (frames_.count(key) || image.size_bytes() > budget_) return;
  ages_.push_front(key);
  frames_.emplace(key, CachedFrame{image, ages_.begin()});
  frame_bytes_ += image.size_bytes();
  while (frame_bytes_ > budget_ && !ages_.empty()) {
    auto old = frames_.find(ages_.back());
    frame_bytes_ -= old->second.image.size_bytes();
    frames_.erase(old);
    ages_.pop_back();
  }
}

std::shared_ptr<const AudioBuffer> MediaCache::audio(const std::string& path,
                                                     int sample_rate) {
  std::shared_ptr<Entry> e = entry(path);
  std::lock_guard<std::mutex> lock(e->mutex);
  auto& slot = e->audio[sample_rate];
  if (!slot) slot = std::make_shared<const AudioBuffer>(e->source->audio(sample_rate));
  return slot;
}

void MediaCache::forget(const std::string& path) {
  std::lock_guard<std::mutex> lock(mutex_);
  entries_.erase(path);
  for (auto it = ages_.begin(); it != ages_.end();) {
    if (it->path == path) {
      auto f = frames_.find(*it);
      frame_bytes_ -= f->second.image.size_bytes();
      frames_.erase(f);
      it = ages_.erase(it);
    } else {
      ++it;
    }
  }
}

std::size_t MediaCache::cached_frames() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return frames_.size();
}

}  // namespace mpp
