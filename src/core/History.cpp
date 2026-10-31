#include "core/History.h"

#include "core/Serialization.h"

#include <string_view>
#include <unordered_map>

namespace bemotion {

namespace {

// The text's lines, each with its '\n'.
std::vector<std::string_view> linesOf(const std::string& text) {
    std::vector<std::string_view> lines;
    size_t start = 0;
    while (start < text.size()) {
        const size_t end = text.find('\n', start);
        const size_t next = end == std::string::npos ? text.size() : end + 1;
        lines.emplace_back(text.data() + start, next - start);
        start = next;
    }
    return lines;
}

} // namespace

// Walks `from` line by line: a line `to` also has is a reference to it (preferring the line
// after the last one taken, so unchanged stretches become one piece), anything else is text.
History::Step History::stepBetween(const State& from, const std::string& to) {
    const std::vector<std::string_view> a = linesOf(from.text), b = linesOf(to);
    std::unordered_map<std::string_view, std::vector<size_t>> where;
    for (size_t j = 0; j < b.size(); ++j)
        where[b[j]].push_back(j);
    Step step;
    step.id = from.id;
    auto add = [&](bool copy, size_t offset, size_t size) {
        if (!step.pieces.empty() && step.pieces.back().copy == copy &&
            step.pieces.back().offset + step.pieces.back().size == offset) {
            step.pieces.back().size += size;
            return;
        }
        step.pieces.push_back({offset, size, copy});
    };
    constexpr size_t kNone = size_t(-1);
    size_t next = kNone; // the line of `to` after the last one taken
    for (size_t i = 0; i < a.size(); ++i) {
        size_t j = kNone;
        if (next < b.size() && b[next] == a[i]) {
            j = next;
        } else if (auto it = where.find(a[i]); it != where.end()) {
            // Several places have this line ("},"): the one whose next lines match too.
            int best = -1;
            size_t looked = 0;
            for (size_t c : it->second) {
                int same = 0;
                while (same < 4 && i + 1 + same < a.size() && c + 1 + same < b.size() &&
                       a[i + 1 + same] == b[c + 1 + same])
                    ++same;
                if (same > best) best = same, j = c;
                if (same == 4 || ++looked == 64) break;
            }
            if (best == 0 && a[i].size() < 16) j = kNone; // a short line on its own is cheaper as text
        }
        if (j == kNone) {
            add(false, step.literal.size(), a[i].size());
            step.literal.append(a[i]);
            next = kNone;
        } else {
            add(true, static_cast<size_t>(b[j].data() - to.data()), b[j].size());
            next = j + 1;
        }
    }
    return step;
}

History::State History::apply(const Step& step, const std::string& to) {
    std::string text;
    size_t size = 0;
    for (const Piece& p : step.pieces)
        size += p.size;
    text.reserve(size);
    for (const Piece& p : step.pieces)
        text.append(p.copy ? to : step.literal, p.offset, p.size);
    return {std::move(text), step.id};
}

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
    for (const Step& s : redo_)
        bytes_ -= s.bytes();
    redo_.clear();
    undo_.push_back(stepBetween(current_, state));
    bytes_ += undo_.back().bytes();
    current_ = {std::move(state), nextId_++};
    trim();
}

void History::trim() {
    // The oldest step is relative to the one after it, which stays whole: dropping it is safe.
    while (undo_.size() > 1 && (undo_.size() > limit_ || bytes_ > byteLimit_)) {
        bytes_ -= undo_.front().bytes();
        undo_.pop_front();
    }
}

bool History::undo(Project& project) {
    if (undo_.empty()) return false;
    State older = apply(undo_.back(), current_.text);
    bytes_ -= undo_.back().bytes();
    undo_.pop_back();
    redo_.push_back(stepBetween(current_, older.text));
    bytes_ += redo_.back().bytes();
    current_ = std::move(older);
    return deserializeProject(current_.text, project);
}

bool History::redo(Project& project) {
    if (redo_.empty()) return false;
    State newer = apply(redo_.back(), current_.text);
    bytes_ -= redo_.back().bytes();
    redo_.pop_back();
    undo_.push_back(stepBetween(current_, newer.text));
    bytes_ += undo_.back().bytes();
    current_ = std::move(newer);
    return deserializeProject(current_.text, project);
}

} // namespace bemotion
