#pragma once

#include <string>

#include "mpp/project.h"

namespace mpp {

inline constexpr int kProjectFormatVersion = 1;
inline constexpr const char* kProjectExtension = ".mpp";

// Project files are JSON: the settings, the bin (with each file's path and
// what was learned about it on import), the clips and the work area.
// Media stays where it is; the file records both its absolute path and its
// path relative to the project, and loading uses the relative one if the
// absolute path no longer exists (the folder was moved).

// In memory. `project_dir` is the folder the file lives in, for relative
// paths; empty leaves paths absolute. decode throws std::runtime_error on
// malformed text, a different format, or a newer version.
std::string encode_project(const Project& project, const std::string& project_dir = "");
Project decode_project(const std::string& text, const std::string& project_dir = "");

// On disk. save writes `<path>.saving` and renames it over `path`, so a
// failed save leaves the old file intact. Both throw std::runtime_error.
void save_project(const Project& project, const std::string& path);
Project load_project(const std::string& path);

// Whether a path ends in ".mpp" (any case).
bool is_project_path(const std::string& path);

}  // namespace mpp
