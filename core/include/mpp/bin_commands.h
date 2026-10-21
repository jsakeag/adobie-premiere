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

// Removes a file from the bin, and every clip that uses it.
class RemoveItemCommand : public Command {
 public:
  explicit RemoveItemCommand(int id) : id(id) {}
  std::string name() const override { return "Clear"; }
  const int id;

 protected:
  void apply(Project& p) override;
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

// The Title window's OK on an existing title: its contents and name.
class SetTitleCommand : public Command {
 public:
  SetTitleCommand(int id, TitleSpec title, std::string title_name)
      : id(id), title(std::move(title)), title_name(std::move(title_name)) {}
  std::string name() const override { return "Edit Title"; }
  const int id;
  const TitleSpec title;
  const std::string title_name;

 protected:
  void apply(Project& p) override;
};

// Changes the output format. Marks, clips and the work area are in project
// frames, so a new frame rate rescales them to keep their times.
class SetSettingsCommand : public Command {
 public:
  explicit SetSettingsCommand(ProjectSettings settings) : settings(settings) {}
  std::string name() const override { return "Project Settings"; }
  const ProjectSettings settings;

 protected:
  void apply(Project& p) override;
};

}  // namespace mpp
