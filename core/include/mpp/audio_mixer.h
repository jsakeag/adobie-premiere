#pragma once

#include <cstdint>

#include "mpp/media.h"
#include "mpp/media_cache.h"
#include "mpp/project.h"

namespace mpp {

// The first sound sample of a project frame, at the project's sample rate.
std::int64_t frame_to_sample(const ProjectSettings& settings, std::int64_t frame);

// One sound clip as it plays: its stretch of the source (silence past the
// source's end), its audio filters run over the whole clip, and its volume
// rubber band applied. Stereo at the project's sample rate, one sample per
// sample of the clip. Throws if the media can't be read.
AudioBuffer clip_audio(const Project& project, MediaCache& cache, const Clip& clip);

// The program's sound for frames [first, last): every clip on Audio A, B
// and C summed, clipped to -1..1 and quantized to the project's bit depth.
// Clips whose media can't be read are silent.
AudioBuffer mix_audio(const Project& project, MediaCache& cache, std::int64_t first,
                      std::int64_t last);

// Clips to -1..1 and rounds to the levels of 8- or 16-bit sound: steps of
// 1/128 or 1/32768, so the values convert to and from integer samples
// exactly.
void quantize(AudioBuffer& audio, int bits);

}  // namespace mpp
