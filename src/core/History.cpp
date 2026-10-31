#include "core/History.h"

#include "core/Serialization.h"

namespace bemotion {

void History::reset(const Project& project) {
    undo_.clear();
    redo_.clear();
    bytes_ = 0;
    current_ = serializeProject(project);
    saved_ = current_;
}

void History::commit(const Project& project) {
    std::string state = serializeProject(project);
    if (state == current_) return;
    for (const std::string& s : redo_) bytes_ -= s.size();
    redo_.clear();
    bytes_ += current_.size();
    undo_.push_back(std::move(current_));
    current_ = std::move(state);
    while (undo_.size() > limit_ || (bytes_ > byteLimit_ && undo_.size() > 1)) {
        bytes_ -= undo_.front().size();
        undo_.pop_front();
    }
}

bool History::undo(Project& project) {
    if (undo_.empty()) return false;
    bytes_ += current_.size() - undo_.back().size();
    redo_.push_back(std::move(current_));
    current_ = std::move(undo_.back());
    undo_.pop_back();
    return deserializeProject(current_, project);
}

bool History::redo(Project& project) {
    if (redo_.empty()) return false;
    bytes_ += current_.size() - redo_.back().size();
    undo_.push_back(std::move(current_));
    current_ = std::move(redo_.back());
    redo_.pop_back();
    return deserializeProject(current_, project);
}

} // namespace bemotion
