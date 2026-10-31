#pragma once

// What the operations share inside the core (not a public header): the field readers, the
// errors and the Applier, whose handlers are split by area over Ops.cpp (composition, layers,
// queries), OpsTimeline.cpp (clip edits, markers, compounds), OpsAnimation.cpp (masks, paint,
// keys, expressions, effects, audio, transitions) and OpsMedia.cpp (plugin values, data, media).

#include "core/Editing.h"
#include "core/Ops.h"
#include "core/Project.h"
#include "core/Suggest.h"

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace bemotion::opsimpl {

using json = nlohmann::json;

// Thrown by the operations; applyOps turns it into OpsResult::error.
struct OpError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

extern const char* kShapeNames[];
extern const char* kAlignNames[];
[[noreturn]] void fail(const std::string& message);
const json& field(const json& op, const char* name);
bool has(const json& op, const char* name);
double number(const json& v, const char* name);
double number(const json& op, const char* name, double fallback);
std::string text(const json& v, const char* name);
bool boolean(const json& v, const char* name);
std::string typeName(const Layer& l);
int shapeKindOf(const std::string& name);
Easing easingOf(const json& v);
bool parseHex(std::string s, Color& c);
Color colorOf(const json& v, const char* name);
Vec2 vec2Of(const json& v, const char* name);
std::vector<float> valueOf(const PropertyRef& p, const json& v);
std::string hexOf(Color c);
double round3(float v);
json valueJson(PropertyKind kind, const std::vector<float>& v);

class Applier {
  public:
    Applier(Project& p, const OpEnvironment& env) : p_(p), c_(p.comp), env_(env) {}
    bool changed = false;

    using Handler = json (Applier::*)(const json&);

    json apply(const json& op);

    static const std::vector<std::pair<std::string, Handler>>& ops();

  private:
    // --- lookups ---
    Layer& layer(const json& op, const char* key = "layer");

    PropertyRef property(Layer& l, const json& op);

    int keyAt(const PropertyRef& p, double t);

    Effect& effect(Layer& l, const json& op);

    double time(const json& op, const char* name = "time", double fallback = 0.0);

    // --- operations ---
    json compositionSet(const json& op);

    json layerAdd(const json& op);

    void joinTrack(uint64_t id, const json& op);

    void setCounter(Layer& l, const json& op);

    // The index of op[key] among `ids`, or a helpful failure.
    template <size_t N> int idOf(const char* const (&ids)[N], const json& op, const char* key) {
        const std::string v = text(op[key], key);
        const int i = indexOfId(ids, v);
        if (i < 0)
            fail("unknown " + std::string(key) + " \"" + v + "\"" +
                 suggest(v, std::vector<std::string>(std::begin(ids), std::end(ids))));
        return i;
    }

    void setLetters(Layer& l, const json& op);

    void setCaptions(Layer& l, const json& op);

    void setBackground(Layer& l, const json& op);

    json layerRemove(const json& op);

    json layerSet(const json& op);

    BezierPath pathOf(const json& v);

    void setMatte(Layer& l, const json& op);

    json layerMove(const json& op);

    json layerSplit(const json& op);

    json layerDuplicate(const json& op);

    // --- timeline edits (core/Editing.h) ---
    ClipEdge edgeOf(const json& op);

    json layerTrim(const json& op);

    json layerRoll(const json& op);

    json layerSlip(const json& op);

    json layerRippleDelete(const json& op);

    json trackCloseGap(const json& op);

    Layer& footage(const json& op);

    json layerSpeed(const json& op);

    json layerTimeRemap(const json& op);

    json layerFreeze(const json& op);

    json layerSeparateAudio(const json& op);

    json svgAdd(const json& op);

    json layerLinkAudio(const json& op);

    // --- masks ---
    Mask& mask(Layer& l, const json& op);

    void setMask(Mask& m, const json& op);

    json maskAdd(const json& op);

    json maskSet(const json& op);

    Layer& pictureLayer(const json& op, const char* what);
    Vec2 pointOf(const json& v, const char* name);
    json paintStroke(const json& op);
    json paintClear(const json& op);
    json pinAdd(const json& op);
    json pinRemove(const json& op);

    json maskDuplicate(const json& op);

    json maskRemove(const json& op);

    // --- markers ---
    int markerIndex(const json& op);

    int markerColorOf(const json& op);

    json markerAdd(const json& op);

    json markerSet(const json& op);

    json markerRemove(const json& op);

    json group(const json& op);

    json ungroup(const json& op);

    json propertySet(const json& op);

    template <typename T> static void addKey(const PropertyRef& p, double t, const std::vector<float>& v, Easing e) {
        auto& a = *static_cast<Animated<T>*>(p.anim);
        T value;
        if constexpr (std::is_same_v<T, float>)
            value = v[0];
        else if constexpr (std::is_same_v<T, Vec2>)
            value = {v[0], v[1]};
        else
            value = {v[0], v[1], v[2], v[3]};
        a.setKey(t, value, e);
    }

    json keyAdd(const json& op);

    json expressionSet(const json& op);

    json expressionBake(const json& op);

    json keyRemove(const json& op);

    json keyClear(const json& op);

    json keyEasing(const json& op);

    json effectAdd(const json& op);

    json effectRemove(const json& op);

    json effectSet(const json& op);

    Layer& audioLayer(const json& op);

    AudioEffect& audioEffect(Layer& l, const json& op);

    void audioValues(AudioEffect& e, const json& op);

    void audioSample(AudioEffect& e, const json& op);

    json audioEffectAdd(const json& op);

    json audioEffectRemove(const json& op);

    json audioEffectSet(const json& op);

    json audioLevel(const json& op);

    json audioRemoveSilence(const json& op);

    std::vector<uint64_t> layerList(const json& op);

    json transitionAdd(const json& op);

    json arrangeDistribute(const json& op);

    json arrangeStagger(const json& op);

    json paramSet(const json& op);

    json dataSet(const json& op);

    json mediaImport(const json& op);

    MediaItem& mediaItem(const json& op);
    std::string folderName(const json& op, const char* key, bool mustExist);
    std::string newFolderName(const json& op, const char* key);
    json folderAdd(const json& op);
    json folderRename(const json& op);
    json folderMove(const json& op) { // to another place in the list (the Media panel's order)
        const std::string name = folderName(op, "folder", true);
        const double i = number(field(op, "index"), "index");
        if (i != std::floor(i)) fail("\"index\" must be a whole number (0: the first folder)");
        std::erase(c_.mediaFolders, name);
        const auto at = static_cast<size_t>(std::clamp(i, 0.0, static_cast<double>(c_.mediaFolders.size())));
        c_.mediaFolders.insert(c_.mediaFolders.begin() + static_cast<std::ptrdiff_t>(at), name);
        return {{"folder", name}, {"index", at}};
    }
    json folderRemove(const json& op) { // its media go to the top
        const std::string name = folderName(op, "folder", true);
        std::erase(c_.mediaFolders, name);
        for (MediaItem& m : c_.media)
            if (m.folder == name) m.folder.clear();
        return json::object();
    }
    json mediaMove(const json& op);

    // --- queries ---
    json queryComposition(const json&);

    json layerSummary(const Layer& l);

    json queryLayers(const json&);

    json queryExpression(const json& op);

    json queryLayer(const json& op);

    Project& p_;
    Composition& c_;
    const OpEnvironment& env_;
};

} // namespace bemotion::opsimpl
