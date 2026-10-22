#include "core/History.h"

#include "core/Serialization.h"

namespace bemotion {

void History::reset(const Project& project) {
    undo_.clear();
    redo_.clear();
    current_ = serializeProject(project);
    saved_ = current_;
}

void History::commit(const Project& project) {
    std::string state = serializeProject(project);
    if (state == current_) return;
    undo_.push_back(std::move(current_));
    if (undo_.size() > limit_) undo_.pop_front();
    current_ = std::move(state);
    redo_.clear();
}

bool History::undo(Project& project) {
    if (undo_.empty()) return false;
    redo_.push_back(std::move(current_));
    current_ = std::move(undo_.back());
    undo_.pop_back();
    return deserializeProject(current_, project);
}

bool History::redo(Project& project) {
    if (redo_.empty()) return false;
    undo_.push_back(std::move(current_));
    current_ = std::move(redo_.back());
    redo_.pop_back();
    return deserializeProject(current_, project);
}

} // namespace bemotion
