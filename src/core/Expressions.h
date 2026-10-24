#pragma once

// Expressions: a property computed every frame from a short formula, like After
// Effects' expressions, in a small language of bemotion's own: safe (it can only
// read the project), fast, and the same in the editor, exports and scripts.
//
//   time * 90                          turns 90° a second
//   wiggle(2, 30)                      its keyframed value, shaken twice a second by up to 30
//   [960 + sin(time * 2) * 300, value.y]
//   layer("Ball").position + [0, -120] follows another layer
//   layer("Ball").position(time - 0.2) ... a moment later
//   loop()                             its keyframes over and over
//   progress < 0.5 ? 0 : 100           a choice
//
// Numbers and vectors ([x, y], [r, g, b, a]) mix freely: math works per component,
// a number meets a vector on every component. docs/EXPRESSIONS.md has the reference.

#include "core/Project.h"

#include <string>

namespace bemotion {

// The composition expressions read (layers, other properties) on this thread, for its
// lifetime; scopes nest. Rendering opens one; the editor keeps one for its composition.
class ExpressionScope {
public:
    explicit ExpressionScope(const Composition& comp);
    ~ExpressionScope();
    ExpressionScope(const ExpressionScope&) = delete;
    ExpressionScope& operator=(const ExpressionScope&) = delete;

private:
    const Composition* previous_;
};

// The editor's composition, as a scope that lasts (null: none).
void setExpressionComposition(const Composition* comp);

// Checks an expression without running it: empty when it reads, else what is wrong.
std::string checkExpression(const std::string& expression);
// The last problem `property`'s expression had when it ran (empty: none).
std::string expressionError(const AnimatedBase& property);

} // namespace bemotion
