#pragma once

#include <string>

#include "mpp/project.h"

namespace mpp {

// One edit to a project. Every change goes through a Command so that it can
// be named and undone.
//
// A subclass holds its parameters as plain values and implements apply().
// Undo comes from the base class: execute() keeps a copy of the project
// from before the edit. Projects hold no pixels, so the copy is small.
class Command {
 public:
  virtual ~Command() = default;

  // Short label for the Edit menu, as in "Undo Razor".
  virtual std::string name() const = 0;

  // Applies the edit. If it throws, the project is left as it was.
  // Throws std::logic_error if called twice.
  void execute(Project& project);

  // undo() returns the project to its state before execute(); redo()
  // brings the edit back. They alternate, starting with undo(), and throw
  // std::logic_error when called out of turn.
  void undo(Project& project);
  void redo(Project& project);

 protected:
  virtual void apply(Project& project) = 0;

 private:
  enum class Stage { New, Done, Undone };

  Stage stage_ = Stage::New;
  Project saved_;  // the project on the other side of the edit
};

}  // namespace mpp
