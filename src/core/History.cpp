#include "core/History.h"

#include "core/Serialization.h"

namespace bemotion {

void History::reset(const Project& project) {
    undo_.clear();
    redo_.clear();
    bytes_ = 0;
    current_ = {serializeProject(project), nextId_++};
    saved_ = current_.id;
}

void History::commit(const Project& project) {
    std::string state = serializeProject(project);
    if (state == current_.text) return;
    for (const State& s : redo_) bytes_ -= s.text.size();
    redo_.clear();
    bytes_ += current_.text.size();
    undo_.push_back(std::move(current_));
    current_ = {std::move(state), nextId_++};
    trim();
}

void History::trim() {
    while (undo_.size() > 1 && (undo_.size() > limit_ || bytes_ > byteLimit_)) {
        bytes_ -= undo_.front().text.size();
        undo_.pop_front();
    }
}

bool History::undo(Project& project) {
    if (undo_.empty()) return false;
    redo_.push_back(std::move(current_));
    current_ = std::move(undo_.back());
    undo_.pop_back();
    bytes_ += redo_.back().text.size();
    bytes_ -= current_.text.size();
    return deserializeProject(current_.text, project);
}

bool History::redo(Project& project) {
    if (redo_.empty()) return false;
    undo_.push_back(std::move(current_));
    current_ = std::move(redo_.back());
    redo_.pop_back();
    bytes_ += undo_.back().text.size();
    bytes_ -= current_.text.size();
    return deserializeProject(current_.text, project);
}

} // namespace bemotion
