#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "mpp/image.h"

namespace mpp {

// One setting of a filter, such as a blur's radius.
struct FilterParam {
  std::string name;
  double min = 0;
  double max = 1;
  double value = 0;  // the default
  std::string unit;  // shown after the value: "px", "%", "°"
  bool whole = false;  // a count or level that only takes whole numbers
};

// A video filter. `apply` gets a picture (straight alpha), the parameter
// values, and the clip frame number for filters that change every frame
// (noise).
struct FilterDef {
  std::string id;    // stable, saved in project files
  std::string name;  // shown in menus
  std::vector<FilterParam> params;
  std::function<Image(const Image& picture, const std::vector<double>& values,
                      std::int64_t frame)>
      apply;
};

// An audio filter: changes a clip's stereo samples in place, with the
// parameter values at the clip's first and last sample (filters that can
// sweep, like Pan, move between them; the others use the start values).
struct AudioFilterDef {
  std::string id;
  std::string name;
  std::vector<FilterParam> params;
  std::function<void(std::vector<float>& stereo, int sample_rate,
                     const std::vector<double>& start, const std::vector<double>& end)>
      apply;
};

// A filter on a clip, with every parameter's value at the clip's first
// frame and its last. Values in between are a straight blend.
struct FilterInstance {
  std::string id;
  std::vector<double> start;
  std::vector<double> end;

  bool operator==(const FilterInstance& o) const {
    return id == o.id && start == o.start && end == o.end;
  }
  bool operator!=(const FilterInstance& o) const { return !(*this == o); }
};

const std::vector<FilterDef>& video_filters();
const std::vector<AudioFilterDef>& audio_filters();
const FilterDef* find_video_filter(const std::string& id);
const AudioFilterDef* find_audio_filter(const std::string& id);

// A new instance with every parameter at its default, start and end.
FilterInstance default_instance(const std::string& id);

// The values `t` of the way from start (0) to end (1). Whole-number
// parameters are rounded.
std::vector<double> filter_values(const FilterInstance& f, const std::vector<FilterParam>& params,
                                  double t);

// Runs a clip's video filters in order at `t` through the clip. Unknown
// filters are skipped.
Image apply_video_filters(const Image& picture, const std::vector<FilterInstance>& filters,
                          double t, std::int64_t frame);

// Runs a clip's audio filters in order over its samples.
void apply_audio_filters(std::vector<float>& stereo, int sample_rate,
                         const std::vector<FilterInstance>& filters);

}  // namespace mpp
