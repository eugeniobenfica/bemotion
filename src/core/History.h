#pragma once

// Snapshot based undo/redo.
//
// After every completed edit the whole project is serialized and pushed on the
// stack. This keeps every editing tool free of undo bookkeeping: tools just
// mutate the document and call commit(). Projects with paint strokes, puppet
// pins or long subtitles grow, so the stack is bounded twice: by steps and by
// the bytes it keeps (the oldest steps go first; the last one always stays).

#include "core/Project.h"

#include <cstdint>
#include <deque>
#include <string>

namespace bemotion {

class History {
public:
    static constexpr size_t kDefaultSteps = 200;
    static constexpr size_t kDefaultBytes = size_t(256) << 20; // 256 MB of snapshots

    explicit History(size_t limit = kDefaultSteps, size_t byteLimit = kDefaultBytes) : limit_(limit), byteLimit_(byteLimit) {}

    // Starts a fresh history whose baseline is `project`.
    void reset(const Project& project);

    // Records the current state of `project` as a new undo step.
    // Does nothing if the state did not change since the last commit.
    void commit(const Project& project);

    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    bool undo(Project& project);
    bool redo(Project& project);

    // True when the current state differs from the last saved one.
    bool isDirty() const { return current_.id != saved_; }
    void markSaved() { saved_ = current_.id; }

    size_t undoSteps() const { return undo_.size(); }
    size_t bytes() const { return bytes_; } // what the undo and redo stacks keep

private:
    // A serialized state and a number that names it (so "is it the saved one?" compares numbers,
    // not whole projects).
    struct State {
        std::string text;
        uint64_t id = 0;
    };
    void trim();

    size_t limit_;
    size_t byteLimit_;
    size_t bytes_ = 0;
    uint64_t nextId_ = 1;
    std::deque<State> undo_;
    std::deque<State> redo_;
    State current_;
    uint64_t saved_ = 0;
};

} // namespace bemotion
