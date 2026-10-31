#pragma once

// Snapshot based undo/redo.
//
// After every completed edit the whole project is serialized and pushed on the
// stack. Projects are small, so this is cheap and keeps every editing tool free
// of undo bookkeeping: tools just mutate the document and call commit(). The
// oldest steps go when there are more than `limit` of them or they take more
// than `byteLimit` bytes (a big project keeps fewer steps); the last one stays.

#include "core/Project.h"

#include <deque>
#include <string>

namespace bemotion {

class History {
public:
    explicit History(size_t limit = 200, size_t byteLimit = size_t(256) << 20) : limit_(limit), byteLimit_(byteLimit) {}

    // Starts a fresh history whose baseline is `project`.
    void reset(const Project& project);

    // Records the current state of `project` as a new undo step.
    // Does nothing if the state did not change since the last commit.
    void commit(const Project& project);

    bool canUndo() const { return !undo_.empty(); }
    size_t undoSteps() const { return undo_.size(); }
    size_t bytes() const { return bytes_; } // the snapshots kept for undo and redo
    bool canRedo() const { return !redo_.empty(); }
    bool undo(Project& project);
    bool redo(Project& project);

    // True when the current state differs from the last saved one.
    bool isDirty() const { return current_ != saved_; }
    void markSaved() { saved_ = current_; }

private:
    size_t limit_;
    size_t byteLimit_;
    size_t bytes_ = 0;
    std::deque<std::string> undo_;
    std::deque<std::string> redo_;
    std::string current_;
    std::string saved_;
};

} // namespace bemotion
