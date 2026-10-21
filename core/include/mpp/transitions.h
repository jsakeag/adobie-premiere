#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "mpp/image.h"

namespace mpp {

// How one transition on the T track plays. Saved with the project.
struct TransitionSettings {
  double start = 0.0;    // how far along the first frame is (0..1)
  double end = 1.0;      // how far along the last frame is
  bool b_to_a = false;   // the track arrow: from Video B to Video A
  bool reverse = false;  // run the motion backwards (an iris closes)
  int border = 0;        // width of the band at a moving edge, in pixels
  Color border_color = kWhite;
  int direction = 0;     // an index into the transition's direction names

  bool operator==(const TransitionSettings& o) const {
    return start == o.start && end == o.end && b_to_a == o.b_to_a && reverse == o.reverse &&
           border == o.border && border_color == o.border_color && direction == o.direction;
  }
  bool operator!=(const TransitionSettings& o) const { return !(*this == o); }
};

// One kind of transition. `apply` gets two pictures of the same size and
// how far along (0 shows `from`, 1 shows `to`).
struct TransitionDef {
  std::string id;     // stable, saved in project files
  std::string name;   // shown in the Transitions window
  std::string group;  // Dissolve, Wipe, Iris, Slide, Zoom, Stretch
  std::vector<std::string> directions;  // empty if direction doesn't apply
  bool border = false;                  // whether a border applies
  std::function<Image(const Image& from, const Image& to, double progress,
                      const TransitionSettings& settings)>
      apply;
};

// All 24, in the Transitions window's order.
const std::vector<TransitionDef>& transitions();
// nullptr for an unknown id.
const TransitionDef* find_transition(const std::string& id);

// Runs a transition: `from` at progress 0 and `to` at 1 exactly, the
// definition in between, and `reverse` running it backwards with the roles
// swapped. Throws std::invalid_argument if the sizes differ.
Image apply_transition(const TransitionDef& def, const Image& from, const Image& to,
                       double progress, const TransitionSettings& settings);

// How far along a transition is at frame `index` of `length` frames: from
// settings.start to settings.end, sampled at the middle of each frame.
double transition_progress(const TransitionSettings& settings, std::int64_t index,
                           std::int64_t length);

}  // namespace mpp
