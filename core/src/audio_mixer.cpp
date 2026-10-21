#include "mpp/audio_mixer.h"

#include <algorithm>
#include <cmath>
#include <exception>

#include "mpp/filters.h"

namespace mpp {

std::int64_t frame_to_sample(const ProjectSettings& settings, std::int64_t frame) {
  return static_cast<std::int64_t>(
      std::floor(settings.rate.seconds(frame) * settings.audio_rate + 0.5));
}

AudioBuffer clip_audio(const Project& project, MediaCache& cache, const Clip& clip) {
  const ProjectSettings& s = project.settings;
  AudioBuffer out;
  out.sample_rate = s.audio_rate;
  const std::int64_t count =
      std::max<std::int64_t>(0, frame_to_sample(s, clip.end()) - frame_to_sample(s, clip.start));
  out.samples.assign(static_cast<std::size_t>(count) * 2, 0.0f);
  const MediaItem* item = project.item(clip.item);
  if (!item || !item->info.has_audio || count == 0) return out;

  const auto source = cache.audio(item->path, s.audio_rate);
  const std::int64_t offset = frame_to_sample(s, clip.in);
  const std::int64_t available =
      std::clamp<std::int64_t>(static_cast<std::int64_t>(source->frames()) - offset, 0, count);
  if (available > 0) {
    std::copy(source->samples.begin() + offset * 2, source->samples.begin() + (offset + available) * 2,
              out.samples.begin());
  }
  apply_audio_filters(out.samples, s.audio_rate, clip.filters);
  if (!clip.fade.empty()) {
    const double frames_per_sample = s.rate.fps() / s.audio_rate;
    for (std::int64_t k = 0; k < count; ++k) {
      const float level = static_cast<float>(fade_level(clip.fade, clip.in + k * frames_per_sample));
      out.samples[static_cast<std::size_t>(k) * 2] *= level;
      out.samples[static_cast<std::size_t>(k) * 2 + 1] *= level;
    }
  }
  return out;
}

AudioBuffer mix_audio(const Project& project, MediaCache& cache, std::int64_t first,
                      std::int64_t last) {
  const ProjectSettings& s = project.settings;
  AudioBuffer out;
  out.sample_rate = s.audio_rate;
  const std::int64_t begin = frame_to_sample(s, first);
  const std::int64_t end = frame_to_sample(s, std::max(first, last));
  out.samples.assign(static_cast<std::size_t>(end - begin) * 2, 0.0f);
  for (const Clip& clip : project.clips) {
    if (!is_audio_track(clip.track) || clip.end() <= first || clip.start >= last) continue;
    AudioBuffer sound;
    try {
      sound = clip_audio(project, cache, clip);
    } catch (const std::exception&) {
      continue;  // missing media is silent
    }
    const std::int64_t at = frame_to_sample(s, clip.start);
    const std::int64_t from = std::max(begin, at);
    const std::int64_t to = std::min(end, at + static_cast<std::int64_t>(sound.frames()));
    for (std::int64_t i = from; i < to; ++i) {
      const std::size_t o = static_cast<std::size_t>(i - begin) * 2;
      const std::size_t c = static_cast<std::size_t>(i - at) * 2;
      out.samples[o] += sound.samples[c];
      out.samples[o + 1] += sound.samples[c + 1];
    }
  }
  quantize(out, s.audio_bits);
  return out;
}

void quantize(AudioBuffer& audio, int bits) {
  const float steps = bits <= 8 ? 128.0f : 32768.0f;
  for (float& v : audio.samples) {
    const float level = std::round(std::clamp(v, -1.0f, 1.0f) * steps);
    v = std::min(level, steps - 1) / steps;
  }
}

}  // namespace mpp
