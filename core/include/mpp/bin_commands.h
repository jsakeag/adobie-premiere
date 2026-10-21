#pragma once

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

#include "mpp/command.h"

namespace mpp {

// Commands for the Project window's bin and the project settings.

// Adds a file to the bin. The item gets the project's next id; its own id
// is ignored.
class AddItemCommand : public Command {
 public:
  explicit AddItemCommand(MediaItem item) : item(std::move(item)) {}
  std::string name() const override { return "Import"; }
  const MediaItem item;

 protected:
  void apply(Project& p) override {
    MediaItem added = item;
    added.id = p.next_id++;
    p.bin.push_back(std::move(added));
  }
};

class RemoveItemCommand : public Command {
 public:
  explicit RemoveItemCommand(int id) : id(id) {}
  std::string name() const override { return "Clear"; }
  const int id;

 protected:
  void apply(Project& p) override {
    const int i = p.item_index(id);
    if (i < 0) throw std::invalid_argument("No such item in the Project window");
    p.bin.erase(p.bin.begin() + i);
  }
};

// The Clip window's in and out marks.
class SetItemMarksCommand : public Command {
 public:
  SetItemMarksCommand(int id, std::int64_t in, std::int64_t out)
      : id(id), in(in), out(out) {}
  std::string name() const override { return "Set Marks"; }
  const int id;
  const std::int64_t in;
  const std::int64_t out;

 protected:
  void apply(Project& p) override {
    MediaItem* item = p.item(id);
    if (!item) throw std::invalid_argument("No such item in the Project window");
    if (in < 0 || out <= in) throw std::invalid_argument("The out mark must come after the in mark");
    const auto length = source_length(item->info, p.settings.rate);
    if (length && out > *length) throw std::invalid_argument("The out mark is past the end of the clip");
    item->in = in;
    item->out = out;
  }
};

class SetSettingsCommand : public Command {
 public:
  explicit SetSettingsCommand(ProjectSettings settings) : settings(settings) {}
  std::string name() const override { return "Project Settings"; }
  const ProjectSettings settings;

 protected:
  void apply(Project& p) override {
    if (settings.width < 16 || settings.height < 16 || settings.width > 4096 ||
        settings.height > 4096) {
      throw std::invalid_argument("Frame size must be between 16 and 4096 pixels");
    }
    if (settings.rate.num <= 0 || settings.rate.den <= 0) {
      throw std::invalid_argument("Frame rate must be positive");
    }
    if (settings.audio_bits != 8 && settings.audio_bits != 16) {
      throw std::invalid_argument("Audio must be 8 or 16 bit");
    }
    if (settings.audio_rate < 8000 || settings.audio_rate > 96000) {
      throw std::invalid_argument("Audio rate must be between 8 and 96 kHz");
    }
    if (settings.still_frames < 1) throw std::invalid_argument("Still length must be at least one frame");
    // Marks are in project frames, so a new rate rescales them.
    if (settings.rate != p.settings.rate) {
      const double scale = settings.rate.fps() / p.settings.rate.fps();
      for (MediaItem& item : p.bin) {
        item.in = static_cast<std::int64_t>(item.in * scale);
        item.out = std::max(item.in + 1, static_cast<std::int64_t>(item.out * scale));
        if (const auto length = source_length(item.info, settings.rate)) {
          item.out = std::min(item.out, *length);
          item.in = std::min(item.in, item.out - 1);
        }
      }
    }
    p.settings = settings;
  }
};

}  // namespace mpp
