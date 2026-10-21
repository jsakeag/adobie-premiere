#include "mpp/renderer.h"

#include <exception>

#include "mpp/compositing.h"
#include "mpp/resample.h"

namespace mpp {

double clip_source_position(const Clip& clip, std::int64_t frame) {
  return static_cast<double>(clip.in + (frame - clip.start));
}

Image clip_picture(const Project& project, MediaCache& cache, const Clip& clip,
                   std::int64_t frame) {
  const ProjectSettings& s = project.settings;
  const MediaItem* item = project.item(clip.item);
  if (!item || !item->info.has_video) return Image(s.width, s.height);
  const std::int64_t index =
      source_frame(item->info, s.rate, clip_source_position(clip, frame));
  return fit_into(cache.frame(item->path, index), s.width, s.height, kTransparent);
}

Image render_frame(const Project& project, MediaCache& cache, std::int64_t frame) {
  const ProjectSettings& s = project.settings;
  Image out(s.width, s.height, kBlack);
  const Clip* shown = project.clip_at(Track::VideoA, frame);
  if (!shown) shown = project.clip_at(Track::VideoB, frame);
  if (shown) {
    try {
      composite_over(out, clip_picture(project, cache, *shown, frame));
    } catch (const std::exception&) {
      // Missing media renders black, as an offline clip would.
    }
  }
  return out;
}

}  // namespace mpp
