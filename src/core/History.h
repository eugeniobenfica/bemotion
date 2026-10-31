#pragma once

// Snapshot based undo/redo.
//
// After every completed edit the whole project is serialized and compared with the state
// before. This keeps every editing tool free of undo bookkeeping: tools just mutate the
// document and call commit(). Only the current state is kept whole: each step on the stacks is
// its neighbor's lines it shares (by reference) plus the lines that differ (as text), so a long
// history of a big project costs little. The stacks are still bounded twice: by steps and by
// the bytes they keep (the oldest steps go first; the last one always stays).

#include "core/Project.h"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace bemotion {

class History {
  public:
    static constexpr size_t kDefaultSteps = 200;
    static constexpr size_t kDefaultBytes = size_t(256) << 20; // 256 MB of steps

    explicit History(size_t limit = kDefaultSteps, size_t byteLimit = kDefaultBytes)
        : limit_(limit), byteLimit_(byteLimit) {}

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
    // A state next to another one on a stack, as pieces put end to end: bytes of the other's
    // text (`copy`) or of `literal`. And a number that names it (so "is it the saved one?"
    // compares numbers, not whole projects).
    struct Piece {
        size_t offset = 0;
        size_t size = 0;
        bool copy = false;
    };
    struct Step {
        std::vector<Piece> pieces;
        std::string literal;
        uint64_t id = 0;
        size_t bytes() const { return literal.size() + pieces.size() * sizeof(Piece) + sizeof(Step); }
    };
    struct State {
        std::string text;
        uint64_t id = 0;
    };
    // `from` as a step next to `to`, and back.
    static Step stepBetween(const State& from, const std::string& to);
    static State apply(const Step& step, const std::string& to);
    void trim();

    size_t limit_;
    size_t byteLimit_;
    size_t bytes_ = 0;
    uint64_t nextId_ = 1;
    std::deque<Step> undo_; // each relative to the state above it (the next newer one)
    std::deque<Step> redo_; // each relative to the state below it (the next older one)
    State current_;
    uint64_t saved_ = 0;
};

} // namespace bemotion
