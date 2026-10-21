#pragma once

#include <initializer_list>
#include <string>

#include "golden.h"
#include "mpp/media_cache.h"
#include "mpp/project.h"

// A project whose bin holds the named fixture files, in order, with ids
// 1, 2, 3... and marks around the whole of each.
inline mpp::Project project_with(std::initializer_list<const char*> files,
                                 mpp::ProjectSettings settings = {}) {
  static mpp::MediaCache cache;
  mpp::Project p;
  p.settings = settings;
  for (const char* name : files) {
    const std::string path = fixture(name);
    mpp::MediaItem item = mpp::make_item(path, cache.info(path), settings);
    item.id = p.next_id++;
    p.bin.push_back(item);
  }
  return p;
}
