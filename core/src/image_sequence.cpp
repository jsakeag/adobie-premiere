#include "mpp/image_sequence.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <set>

namespace mpp {

namespace fs = std::filesystem;

std::string ImageSequence::path(int index) const {
  char buffer[4096];
  std::snprintf(buffer, sizeof(buffer), pattern.c_str(), first + index);
  return buffer;
}

std::optional<ImageSequence> find_image_sequence(const std::string& picked) {
  const fs::path file(picked);
  const std::string stem = file.stem().string();
  const std::string ext = file.extension().string();
  std::size_t digits_at = stem.size();
  while (digits_at > 0 && std::isdigit(static_cast<unsigned char>(stem[digits_at - 1]))) --digits_at;
  const std::size_t width = stem.size() - digits_at;
  if (width == 0 || width > 9) return std::nullopt;
  const std::string prefix = stem.substr(0, digits_at);
  const int picked_number = std::stoi(stem.substr(digits_at));

  std::set<int> numbers;
  std::error_code ec;
  const fs::path dir = file.has_parent_path() ? file.parent_path() : fs::path(".");
  for (const auto& entry : fs::directory_iterator(dir, ec)) {
    if (!entry.is_regular_file(ec)) continue;
    const std::string s = entry.path().stem().string();
    if (entry.path().extension().string() != ext || s.size() != prefix.size() + width ||
        s.compare(0, prefix.size(), prefix) != 0) {
      continue;
    }
    const std::string digits = s.substr(prefix.size());
    if (!std::all_of(digits.begin(), digits.end(), [](unsigned char c) { return std::isdigit(c); })) continue;
    numbers.insert(std::stoi(digits));
  }
  if (!numbers.count(picked_number)) return std::nullopt;
  // The unbroken run through the picked number.
  int first = picked_number, last = picked_number;
  while (numbers.count(first - 1)) --first;
  while (numbers.count(last + 1)) ++last;
  if (last == first) return std::nullopt;

  ImageSequence seq;
  std::string escaped;
  for (char c : prefix) {
    escaped += c;
    if (c == '%') escaped += '%';
  }
  const std::string name = escaped + "%0" + std::to_string(width) + "d" + ext;
  seq.pattern = (file.has_parent_path() ? (file.parent_path() / name) : fs::path(name)).string();
  seq.first = first;
  seq.count = last - first + 1;
  return seq;
}

}  // namespace mpp
