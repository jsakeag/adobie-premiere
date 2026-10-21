#include "mpp/command.h"

#include <stdexcept>
#include <utility>

namespace mpp {

void Command::execute(Project& project) {
  if (stage_ != Stage::New) {
    throw std::logic_error("Command::execute called twice");
  }
  saved_ = project;
  try {
    apply(project);
  } catch (...) {
    project = std::move(saved_);
    saved_ = {};
    throw;
  }
  stage_ = Stage::Done;
}

void Command::undo(Project& project) {
  if (stage_ != Stage::Done) {
    throw std::logic_error("Command::undo needs an executed command");
  }
  std::swap(project, saved_);
  stage_ = Stage::Undone;
}

void Command::redo(Project& project) {
  if (stage_ != Stage::Undone) {
    throw std::logic_error("Command::redo needs an undone command");
  }
  std::swap(project, saved_);
  stage_ = Stage::Done;
}

}  // namespace mpp
