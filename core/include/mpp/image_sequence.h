#pragma once

#include <optional>
#include <string>

namespace mpp {

// Numbered stills played as one clip (Premiere 2.0 read numbered PICTs):
// shot_0001.png, shot_0002.png, ... `pattern` is a printf pattern for the
// path, such as "/renders/shot_%04d.png".
struct ImageSequence {
  std::string pattern;
  int first = 0;
  int count = 0;

  std::string path(int index) const;  // the file for the index-th frame

  bool operator==(const ImageSequence& o) const {
    return pattern == o.pattern && first == o.first && count == o.count;
  }
};

// The run of numbered images a picked file belongs to: files in its folder
// with the same name before and after the number and the same number of
// digits, counting up from the first without gaps through the picked one.
// nullopt if the name has no number or there's only the one file.
std::optional<ImageSequence> find_image_sequence(const std::string& picked);

}  // namespace mpp
