#pragma once

// Animatable properties.
//
// Every property that can change over time is an Animated<T>. Without keyframes
// it simply holds a static value; once it has keyframes, its value is
// interpolated between them. AnimatedBase exposes a type-erased view so that
// generic UI (timeline, keyframe editing) does not need to know about T.

#include "core/Easing.h"
#include "core/Math.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace bemotion {

// Two keyframe times closer than this are considered the same instant.
constexpr double kTimeEpsilon = 1e-4;

uint32_t nextKeyframeUid();

// How a spatial (Vec2) motion path bends through a keyframe.
enum class PathMode {
    Auto,   // smooth curve through the neighbors (auto bezier)
    Linear, // straight segments, a corner at the key
    Custom, // user-dragged handles (inTangent / outTangent)
};

template <typename T>
struct Keyframe {
    double time = 0.0;
    T value{};
    Easing easing = Easing::EaseInOut; // curve of the segment that starts here
    BezierEase curve;                  // timing curve when easing == Custom
    // Spatial path (Vec2 properties only); tangents are relative to `value`.
    PathMode path = PathMode::Auto;
    Vec2 inTangent{}, outTangent{};
    uint32_t uid = nextKeyframeUid(); // runtime identity, not persisted
};

inline float interpolate(float a, float b, float t) { return lerp(a, b, t); }
inline Vec2 interpolate(Vec2 a, Vec2 b, float t) { return lerp(a, b, t); }
inline Color interpolate(Color a, Color b, float t) { return lerp(a, b, t); }

// Value of the segment from keys[i] to keys[i + 1] at eased progress u.
template <typename T>
T segmentValue(const std::vector<Keyframe<T>>& keys, size_t i, float u) {
    return interpolate(keys[i].value, keys[i + 1].value, u);
}
// Positions follow a bezier motion path, at constant speed along the curve.
Vec2 segmentValue(const std::vector<Keyframe<Vec2>>& keys, size_t i, float u);

// Absolute bezier control points of the motion path segment keys[i] -> keys[i + 1].
struct PathSegment {
    Vec2 p0, p1, p2, p3;
    Vec2 at(float s) const;
};
PathSegment pathSegment(const std::vector<Keyframe<Vec2>>& keys, size_t i);
// Effective tangents of keys[i] (what Auto / Linear resolve to), relative to its value.
void pathTangents(const std::vector<Keyframe<Vec2>>& keys, size_t i, Vec2& in, Vec2& out);

class AnimatedBase;
// The property's expression at `t` (core/Expressions.h), as 1, 2 or 4 floats in `out`; false
// when it has none or it fails (then the keyframed value is used).
bool evaluateExpression(const AnimatedBase& property, double t, float out[4]);

class AnimatedBase {
public:
    virtual ~AnimatedBase() = default;

    // A formula computing the value every frame (core/Expressions.h); empty: none.
    std::string expression;
    bool hasExpression() const { return !expression.empty(); }
    virtual int components() const = 0;                         // 1, 2 or 4
    virtual void keyedValue(double t, float out[4]) const = 0; // what the keyframes say (no expression)

    virtual int keyCount() const = 0;
    virtual double keyTime(int i) const = 0;
    virtual void setKeyTime(int i, double t) = 0;
    virtual Easing keyEasing(int i) const = 0;
    virtual void setKeyEasing(int i, Easing e) = 0;
    virtual BezierEase keyCurve(int i) const = 0;
    virtual void setKeyCurve(int i, const BezierEase& c) = 0;
    virtual uint32_t keyUid(int i) const = 0;
    virtual void removeKey(int i) = 0;
    virtual void sortKeys() = 0;           // safe while dragging keys around
    virtual void removeDuplicateKeys() = 0; // call once an edit is finished

    // Adds a key at `t` holding the property's current value at that time.
    virtual void addKeyAt(double t) = 0;
    // Drops all keys, keeping the value at `t` as the new static value.
    virtual void clearKeys(double t) = 0;

    bool isAnimated() const { return keyCount() > 0; }

    int findKey(double t) const {
        for (int i = 0; i < keyCount(); ++i)
            if (std::fabs(keyTime(i) - t) < kTimeEpsilon) return i;
        return -1;
    }
    int findKeyByUid(uint32_t uid) const {
        for (int i = 0; i < keyCount(); ++i)
            if (keyUid(i) == uid) return i;
        return -1;
    }
};

template <typename T>
class Animated final : public AnimatedBase {
public:
    T base{};
    std::vector<Keyframe<T>> keys; // always sorted by time

    Animated() = default;
    Animated(T value) : base(value) {} // NOLINT(google-explicit-constructor)

    // The value at `t`: the expression's when it has one, else the keyframes'.
    T valueAt(double t) const {
        if (!expression.empty())
            if (float v[4]; evaluateExpression(*this, t, v)) return fromFloats(v);
        return keyedAt(t);
    }

    // What the keyframes (or the static value) say at `t`, whatever the expression.
    T keyedAt(double t) const {
        if (keys.empty()) return base;
        if (t <= keys.front().time) return keys.front().value;
        if (t >= keys.back().time) return keys.back().value;
        auto next = std::upper_bound(keys.begin(), keys.end(), t,
                                     [](double time, const Keyframe<T>& k) { return time < k.time; });
        auto prev = next - 1;
        const double span = next->time - prev->time;
        const float u = span > 0.0 ? static_cast<float>((t - prev->time) / span) : 1.0f;
        return segmentValue(keys, static_cast<size_t>(prev - keys.begin()), applyEasing(prev->easing, prev->curve, u));
    }

    // Edits the property at time `t`: updates/creates a key if animated,
    // otherwise changes the static value.
    void set(double t, const T& v) {
        if (keys.empty()) base = v;
        else setKey(t, v);
    }

    void setKey(double t, const T& v, Easing easing = Easing::EaseInOut) {
        if (const int i = findKey(t); i >= 0) {
            keys[i].value = v;
            return;
        }
        Keyframe<T> k;
        k.time = t;
        k.value = v;
        k.easing = easing;
        keys.insert(std::upper_bound(keys.begin(), keys.end(), t,
                                     [](double time, const Keyframe<T>& kf) { return time < kf.time; }),
                    k);
    }

    int keyCount() const override { return static_cast<int>(keys.size()); }
    double keyTime(int i) const override { return keys[i].time; }
    void setKeyTime(int i, double t) override { keys[i].time = std::max(0.0, t); }
    Easing keyEasing(int i) const override { return keys[i].easing; }
    void setKeyEasing(int i, Easing e) override { keys[i].easing = e; }
    BezierEase keyCurve(int i) const override { return keys[i].curve; }
    void setKeyCurve(int i, const BezierEase& c) override { keys[i].curve = c; }
    uint32_t keyUid(int i) const override { return keys[i].uid; }

    void removeKey(int i) override {
        if (keys.size() == 1) base = keys[0].value;
        keys.erase(keys.begin() + i);
    }

    void sortKeys() override {
        std::stable_sort(keys.begin(), keys.end(),
                         [](const Keyframe<T>& a, const Keyframe<T>& b) { return a.time < b.time; });
    }

    // Collapses keys that ended up on the same instant (the later one wins).
    void removeDuplicateKeys() override {
        sortKeys();
        for (size_t i = 1; i < keys.size();) {
            if (std::fabs(keys[i].time - keys[i - 1].time) < kTimeEpsilon) keys.erase(keys.begin() + (i - 1));
            else ++i;
        }
    }

    void addKeyAt(double t) override { setKey(t, keyedAt(t)); }

    void clearKeys(double t) override {
        base = keyedAt(t);
        keys.clear();
    }

    int components() const override {
        if constexpr (std::is_same_v<T, float>) return 1;
        else if constexpr (std::is_same_v<T, Vec2>) return 2;
        else return 4;
    }
    void keyedValue(double t, float out[4]) const override { toFloats(keyedAt(t), out); }

private:
    static void toFloats(float v, float* out) { out[0] = v; }
    static void toFloats(Vec2 v, float* out) { out[0] = v.x, out[1] = v.y; }
    static void toFloats(Color v, float* out) { out[0] = v.r, out[1] = v.g, out[2] = v.b, out[3] = v.a; }
    static T fromFloats(const float* v) {
        if constexpr (std::is_same_v<T, float>) return v[0];
        else if constexpr (std::is_same_v<T, Vec2>) return Vec2{v[0], v[1]};
        else return Color{v[0], v[1], v[2], v[3]};
    }
};

using AnimFloat = Animated<float>;
using AnimVec2 = Animated<Vec2>;
using AnimColor = Animated<Color>;

} // namespace bemotion
