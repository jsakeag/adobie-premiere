#pragma once

#include <cstdint>

#include "mpp/image.h"
#include "mpp/media_cache.h"
#include "mpp/project.h"

namespace mpp {

// The source position (project frames into the source) a clip shows at a
// timeline frame.
double clip_source_position(const Clip& clip, std::int64_t frame);

// A clip's picture at a timeline frame, fitted into the project's frame
// size on a transparent background. Throws if the media can't be read.
Image clip_picture(const Project& project, MediaCache& cache, const Clip& clip,
                   std::int64_t frame);

// The program at a timeline frame: an opaque picture at the project's frame
// size. Video A shows where it has a clip, Video B elsewhere, black where
// neither does. Media that can't be read shows as black.
Image render_frame(const Project& project, MediaCache& cache, std::int64_t frame);

}  // namespace mpp
