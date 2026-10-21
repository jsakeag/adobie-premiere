#pragma once

#include <cstdint>

#include "mpp/image.h"
#include "mpp/media_cache.h"
#include "mpp/project.h"

namespace mpp {

// The source position (project frames into the source) a clip shows at a
// timeline frame.
double clip_source_position(const Clip& clip, std::int64_t frame);

// An item's picture at a source position (project frames), at its own
// size: a frame of its file, or a title drawn at the project's frame size.
// Throws if the media can't be read.
Image item_picture(const Project& project, MediaCache& cache, const MediaItem& item,
                   double position);

// How far through a clip a timeline frame is: 0 at its first frame, 1 at
// its last. Filters blend their start and end values by this.
double clip_progress(const Clip& clip, std::int64_t frame);

// A clip's picture at a timeline frame, scaled to fit the project's frame
// (keeping its shape, so it may be narrower or shorter than the frame),
// before filters. With `keep_alpha` false it is made opaque on black first.
// Throws if the media can't be read; a clip without a picture gives an
// empty image.
Image clip_source(const Project& project, MediaCache& cache, const Clip& clip,
                  std::int64_t frame, bool keep_alpha = true);

// A clip's picture at a timeline frame, fitted into the project's frame
// size on a transparent background, with its filters applied at the fitted
// size (so a radius in pixels means frame pixels). With `keep_alpha` false
// the picture is made opaque (on black) first. Throws if the media can't be
// read.
Image clip_picture(const Project& project, MediaCache& cache, const Clip& clip,
                   std::int64_t frame, bool keep_alpha = true);

// What Video A or Video B shows at a frame: its clip's picture over black,
// or black. Media that can't be read shows as black.
Image track_picture(const Project& project, MediaCache& cache, Track track, std::int64_t frame);

// What Video A, Video B and the transition between them show at a frame,
// before the Super track goes on top.
Image render_beneath_super(const Project& project, MediaCache& cache, std::int64_t frame);

// The program at a timeline frame: an opaque picture at the project's frame
// size. Under a transition, the transition from one of Video A and B to the
// other; elsewhere Video A where it has a clip, Video B where it doesn't.
// Then the Super clip, if any, keyed and faded on top.
Image render_frame(const Project& project, MediaCache& cache, std::int64_t frame);

}  // namespace mpp
