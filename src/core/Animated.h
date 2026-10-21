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
#include <vector>

namespace mirage {

// Two keyframe times closer than this are considered the same instant.
constexpr double kTimeEpsilon = 1e-4;

uint32_t nextKeyframeUid();

template <typename T>
struct Keyframe {
    double time = 0.0;
    T value{};
    Easing easing = Easing::EaseInOut; // curve of the segment that starts here
    uint32_t uid = nextKeyframeUid();  // runtime identity, not persisted
};

inline float interpolate(float a, float b, float t) { return lerp(a, b, t); }
inline Vec2 interpolate(Vec2 a, Vec2 b, float t) { return lerp(a, b, t); }
inline Color interpolate(Color a, Color b, float t) { return lerp(a, b, t); }

class AnimatedBase {
public:
    virtual ~AnimatedBase() = default;

    virtual int keyCount() const = 0;
    virtual double keyTime(int i) const = 0;
    virtual void setKeyTime(int i, double t) = 0;
    virtual Easing keyEasing(int i) const = 0;
    virtual void setKeyEasing(int i, Easing e) = 0;
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

    T valueAt(double t) const {
        if (keys.empty()) return base;
        if (t <= keys.front().time) return keys.front().value;
        if (t >= keys.back().time) return keys.back().value;
        auto next = std::upper_bound(keys.begin(), keys.end(), t,
                                     [](double time, const Keyframe<T>& k) { return time < k.time; });
        auto prev = next - 1;
        const double span = next->time - prev->time;
        const float u = span > 0.0 ? static_cast<float>((t - prev->time) / span) : 1.0f;
        return interpolate(prev->value, next->value, applyEasing(prev->easing, u));
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

    void addKeyAt(double t) override { setKey(t, valueAt(t)); }

    void clearKeys(double t) override {
        base = valueAt(t);
        keys.clear();
    }
};

using AnimFloat = Animated<float>;
using AnimVec2 = Animated<Vec2>;
using AnimColor = Animated<Color>;

} // namespace mirage
