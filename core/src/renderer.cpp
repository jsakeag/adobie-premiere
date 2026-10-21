#include "mpp/renderer.h"

#include <algorithm>
#include <exception>

#include "mpp/compositing.h"
#include "mpp/resample.h"
#include "mpp/title.h"

namespace mpp {

double clip_source_position(const Clip& clip, std::int64_t frame) {
  return static_cast<double>(clip.in + (frame - clip.start));
}

Image item_picture(const Project& project, MediaCache& cache, const MediaItem& item,
                   double position) {
  const ProjectSettings& s = project.settings;
  if (item.title) return render_title(*item.title, s.width, s.height);
  return cache.frame(item.path, source_frame(item.info, s.rate, position));
}

double clip_progress(const Clip& clip, std::int64_t frame) {
  if (clip.length() <= 1) return 0.0;
  return std::clamp(static_cast<double>(frame - clip.start) / static_cast<double>(clip.length() - 1),
                    0.0, 1.0);
}

Image clip_source(const Project& project, MediaCache& cache, const Clip& clip,
                  std::int64_t frame, bool keep_alpha) {
  const ProjectSettings& s = project.settings;
  const MediaItem* item = project.item(clip.item);
  if (!item || !item->info.has_video) return {};
  const Image picture = item_picture(project, cache, *item, clip_source_position(clip, frame));
  const Image source = keep_alpha ? picture : flattened(picture);
  const Point size = fitted_size(source.width(), source.height(), s.width, s.height);
  return resized(source, size.x, size.y);
}

Image clip_picture(const Project& project, MediaCache& cache, const Clip& clip,
                   std::int64_t frame, bool keep_alpha) {
  const ProjectSettings& s = project.settings;
  Image picture = clip_source(project, cache, clip, frame, keep_alpha);
  Image out(s.width, s.height, kTransparent);
  if (picture.empty()) return out;
  if (!clip.filters.empty()) {
    picture = apply_video_filters(picture, clip.filters, clip_progress(clip, frame), frame);
  }
  out.blit(picture, {(s.width - picture.width()) / 2, (s.height - picture.height()) / 2});
  return out;
}

Image track_picture(const Project& project, MediaCache& cache, Track track, std::int64_t frame) {
  const ProjectSettings& s = project.settings;
  Image out(s.width, s.height, kBlack);
  if (const Clip* clip = project.clip_at(track, frame)) {
    try {
      composite_over(out, clip_picture(project, cache, *clip, frame));
    } catch (const std::exception&) {
      // Missing media renders black, as an offline clip would.
    }
  }
  return out;
}

Image render_beneath_super(const Project& project, MediaCache& cache, std::int64_t frame) {
  const Transition* t = project.transition_at(frame);
  const TransitionDef* def = t ? find_transition(t->kind) : nullptr;
  if (def) {
    const Image a = track_picture(project, cache, Track::VideoA, frame);
    const Image b = track_picture(project, cache, Track::VideoB, frame);
    const double progress = transition_progress(t->settings, frame - t->start, t->length);
    return t->settings.b_to_a ? apply_transition(*def, b, a, progress, t->settings)
                              : apply_transition(*def, a, b, progress, t->settings);
  }
  const bool a_has_clip = project.clip_at(Track::VideoA, frame) != nullptr;
  return track_picture(project, cache, a_has_clip ? Track::VideoA : Track::VideoB, frame);
}

Image render_frame(const Project& project, MediaCache& cache, std::int64_t frame) {
  Image out = render_beneath_super(project, cache, frame);
  if (const Clip* super = project.clip_at(Track::Superimpose, frame)) {
    try {
      const Image picture =
          clip_picture(project, cache, *super, frame, super->key.type != KeyType::None);
      const double opacity = fade_level(super->fade, clip_source_position(*super, frame));
      composite_superimpose(out, picture, super->key, opacity);
    } catch (const std::exception&) {
      // Missing media adds nothing.
    }
  }
  return out;
}

}  // namespace mpp
