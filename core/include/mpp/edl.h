#pragma once

#include <string>

#include "mpp/project.h"

namespace mpp {

// The program as a CMX 3600 edit decision list (Premiere 2.0 exported EDLs
// for finishing on tape; today's editors still read them).
//
// - The A/B video becomes one V channel: a cut event wherever the picture
//   comes from one clip, black (BL) for gaps, and for each transition the
//   outgoing clip cut in for no time followed by a dissolve (D) or wipe
//   (W001) of the transition's length into the incoming clip.
// - Audio A, B and C become channels A, A2 and A3, one event per clip.
// - Clips at another speed get an M2 line.
// - Super clips can't be expressed in CMX 3600 and are listed as comments.
// - Reels are AX (a file named in a FROM CLIP NAME comment) or BL. Record
//   time starts at one hour, in the project's timecode (drop-frame if set).
std::string make_edl(const Project& project, const std::string& title);

// Writes make_edl to a file; throws std::runtime_error.
void save_edl(const Project& project, const std::string& title, const std::string& path);

}  // namespace mpp
