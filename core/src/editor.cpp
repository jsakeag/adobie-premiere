#include "mpp/editor.h"

#include <stdexcept>
#include <utility>

namespace mpp {

void Editor::execute(std::unique_ptr<Command> command) {
  if (!command) throw std::invalid_argument("Editor::execute needs a command");
  command->execute(project_);
  last_ = std::move(command);
  undone_ = false;
}

bool Editor::undo() {
  if (!can_undo()) return false;
  last_->undo(project_);
  undone_ = true;
  return true;
}

bool Editor::redo() {
  if (!can_redo()) return false;
  last_->redo(project_);
  undone_ = false;
  return true;
}

}  // namespace mpp
