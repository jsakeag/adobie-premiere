#include "mpp/renderer.h"

#include <exception>

#include "mpp/compositing.h"
#include "mpp/resample.h"

namespace mpp {

double clip_source_position(const Clip& clip, std::int64_t frame) {
  return static_cast<double>(clip.in + (frame - clip.start));
}

Image clip_picture(const Project& project, MediaCache& cache, const Clip& clip,
                   std::int64_t frame, bool keep_alpha) {
  const ProjectSettings& s = project.settings;
  const MediaItem* item = project.item(clip.item);
  if (!item || !item->info.has_video) return Image(s.width, s.height);
  const std::int64_t index =
      source_frame(item->info, s.rate, clip_source_position(clip, frame));
  const Image source = cache.frame(item->path, index);
  return fit_into(keep_alpha ? source : flattened(source), s.width, s.height, kTransparent);
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
