#pragma once

#include <string>

#include "mpp/image.h"

// Compares `actual` against tests/golden/<name>.png and returns an empty
// string on a match, otherwise a description of the mismatch. On a mismatch
// the actual image is written to the test output directory.
// Run with MPP_UPDATE_GOLDEN=1 to (re)write the golden files instead.
std::string golden_mismatch(const mpp::Image& actual, const std::string& name);

// Deterministic test pattern: colour gradient with an alpha ramp and markers
// in the corners, so flips, rotations and offsets are all distinguishable.
mpp::Image make_test_pattern(int width, int height);

// Path of a file in tests/fixtures.
inline std::string fixture(const std::string& name) {
  return std::string(MPP_FIXTURE_DIR) + "/" + name;
}
