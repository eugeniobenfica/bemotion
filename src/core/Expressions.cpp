#include "core/Expressions.h"
#include "core/Suggest.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace bemotion {

namespace {

// --- values -------------------------------------------------------------------------

struct Val {
    int n = 1; // 1: a number; 2 / 3 / 4: a vector
    float v[4] = {0, 0, 0, 0};
    static Val num(float x) { return {1, {x, 0, 0, 0}}; }
};

struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// --- syntax ---------------------------------------------------------------------------

enum class Op { Num, Str, Var, Call, Vec, Neg, Not, Add, Sub, Mul, Div, Mod, Pow, Lt, Gt, Le, Ge, Eq, Ne, And, Or, Cond, Member, Index, Apply,
                Let, Seq }; // Let: name = kids[0]; Seq: Lets, then the result

struct Node {
    Op op = Op::Num;
    float number = 0;
    std::string name; // variable, function, member, string
    std::vector<std::unique_ptr<Node>> kids;
};
using NodeP = std::unique_ptr<Node>;

NodeP make(Op op, std::vector<NodeP> kids = {}, std::string name = {}) {
    auto n = std::make_unique<Node>();
    n->op = op;
    n->kids = std::move(kids);
    n->name = std::move(name);
    return n;
}

class Parser {
public:
    explicit Parser(const std::string& s) : s_(s) {}
    // Lines (or ;) of `name = ...` (var / let / const allowed, as in After Effects), then the result.
    NodeP parse() {
        std::vector<NodeP> steps;
        for (;;) {
            skip();
            while (eat(";")) skip();
            if (i_ >= s_.size()) fail(steps.empty() ? "nothing to work out" : "the last line must be the result (not a name = ...)");
            const size_t at = i_;
            const bool declared = keyword("var") || keyword("let") || keyword("const");
            skip();
            if (i_ < s_.size() && (std::isalpha(static_cast<unsigned char>(s_[i_])) || s_[i_] == '_')) {
                const std::string name = ident();
                skip();
                if (i_ < s_.size() && s_[i_] == '=' && (i_ + 1 >= s_.size() || s_[i_ + 1] != '=')) {
                    ++i_;
                    std::vector<NodeP> k;
                    k.push_back(expr());
                    steps.push_back(make(Op::Let, std::move(k), name));
                    continue;
                }
                if (declared) fail("expected \"=\" after \"" + name + "\"");
            }
            i_ = at;
            NodeP e = expr();
            skip();
            while (eat(";")) skip();
            if (i_ < s_.size()) fail(std::string("unexpected \"") + s_[i_] + "\"");
            if (steps.empty()) return e;
            steps.push_back(std::move(e));
            return make(Op::Seq, std::move(steps));
        }
    }

private:
    [[noreturn]] void fail(const std::string& what) const { throw Error(what + " (at " + std::to_string(i_ + 1) + ")"); }
    void skip() { // spaces, line breaks and // comments
        for (;;) {
            while (i_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[i_]))) ++i_;
            if (i_ + 1 < s_.size() && s_[i_] == '/' && s_[i_ + 1] == '/') {
                while (i_ < s_.size() && s_[i_] != '\n') ++i_;
                continue;
            }
            return;
        }
    }
    bool keyword(const char* word) { // the word, then a space
        skip();
        const size_t n = std::strlen(word);
        if (s_.compare(i_, n, word) != 0 || i_ + n >= s_.size() || !std::isspace(static_cast<unsigned char>(s_[i_ + n]))) return false;
        i_ += n;
        return true;
    }
    bool eat(const char* token) {
        skip();
        const size_t n = std::strlen(token);
        if (s_.compare(i_, n, token) != 0) return false;
        // "<" is not "<=", "=" never alone
        if (n == 1 && (token[0] == '<' || token[0] == '>' || token[0] == '!') && i_ + 1 < s_.size() && s_[i_ + 1] == '=') return false;
        i_ += n;
        return true;
    }
    void expect(const char* token) {
        if (!eat(token)) fail(std::string("expected \"") + token + "\"");
    }

    NodeP expr() {
        NodeP c = orExpr();
        if (eat("?")) {
            NodeP a = expr();
            expect(":");
            NodeP b = expr();
            std::vector<NodeP> k;
            k.push_back(std::move(c)), k.push_back(std::move(a)), k.push_back(std::move(b));
            return make(Op::Cond, std::move(k));
        }
        return c;
    }
    NodeP binary(Op op, NodeP a, NodeP b) {
        std::vector<NodeP> k;
        k.push_back(std::move(a)), k.push_back(std::move(b));
        return make(op, std::move(k));
    }
    NodeP orExpr() {
        NodeP a = andExpr();
        while (eat("||") || eat("or ")) a = binary(Op::Or, std::move(a), andExpr());
        return a;
    }
    NodeP andExpr() {
        NodeP a = cmp();
        while (eat("&&") || eat("and ")) a = binary(Op::And, std::move(a), cmp());
        return a;
    }
    NodeP cmp() {
        NodeP a = add();
        if (eat("<=")) return binary(Op::Le, std::move(a), add());
        if (eat(">=")) return binary(Op::Ge, std::move(a), add());
        if (eat("==")) return binary(Op::Eq, std::move(a), add());
        if (eat("!=")) return binary(Op::Ne, std::move(a), add());
        if (eat("<")) return binary(Op::Lt, std::move(a), add());
        if (eat(">")) return binary(Op::Gt, std::move(a), add());
        return a;
    }
    NodeP add() {
        NodeP a = mul();
        for (;;) {
            if (eat("+")) a = binary(Op::Add, std::move(a), mul());
            else if (eat("-")) a = binary(Op::Sub, std::move(a), mul());
            else return a;
        }
    }
    NodeP mul() {
        NodeP a = unary();
        for (;;) {
            if (eat("*")) a = binary(Op::Mul, std::move(a), unary());
            else if (eat("/")) a = binary(Op::Div, std::move(a), unary());
            else if (eat("%")) a = binary(Op::Mod, std::move(a), unary());
            else return a;
        }
    }
    NodeP unary() {
        if (eat("-")) {
            std::vector<NodeP> k;
            k.push_back(unary());
            return make(Op::Neg, std::move(k));
        }
        if (eat("!")) {
            std::vector<NodeP> k;
            k.push_back(unary());
            return make(Op::Not, std::move(k));
        }
        NodeP a = postfix();
        if (eat("^")) return binary(Op::Pow, std::move(a), unary());
        return a;
    }
    std::vector<NodeP> args(const char* close) {
        std::vector<NodeP> out;
        if (eat(close)) return out;
        do out.push_back(expr());
        while (eat(","));
        expect(close);
        return out;
    }
    NodeP postfix() {
        NodeP a = primary();
        for (;;) {
            if (eat(".")) {
                skip();
                const std::string name = ident();
                if (name.empty()) fail("expected a name after \".\"");
                std::vector<NodeP> k;
                k.push_back(std::move(a));
                a = make(Op::Member, std::move(k), name);
            } else if (eat("[")) {
                std::vector<NodeP> k;
                k.push_back(std::move(a));
                k.push_back(expr());
                expect("]");
                a = make(Op::Index, std::move(k));
            } else if (a->op != Op::Call && a->op != Op::Var && eat("(")) { // a property at another time
                std::vector<NodeP> k;
                k.push_back(std::move(a));
                for (auto& x : args(")")) k.push_back(std::move(x));
                a = make(Op::Apply, std::move(k));
            } else {
                return a;
            }
        }
    }
    std::string ident() {
        size_t j = i_;
        while (j < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[j])) || s_[j] == '_')) ++j;
        std::string out = s_.substr(i_, j - i_);
        i_ = j;
        return out;
    }
    NodeP primary() {
        skip();
        if (i_ >= s_.size()) fail("unexpected end");
        const char c = s_[i_];
        if (std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && i_ + 1 < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_ + 1])))) {
            char* end = nullptr;
            const float v = std::strtof(s_.c_str() + i_, &end);
            i_ = static_cast<size_t>(end - s_.c_str());
            auto n = make(Op::Num);
            n->number = v;
            return n;
        }
        if (c == '"' || c == '\'') {
            const size_t close = s_.find(c, i_ + 1);
            if (close == std::string::npos) fail("a name in quotes is not closed");
            auto n = make(Op::Str, {}, s_.substr(i_ + 1, close - i_ - 1));
            i_ = close + 1;
            return n;
        }
        if (eat("(")) {
            NodeP e = expr();
            expect(")");
            return e;
        }
        if (eat("[")) return make(Op::Vec, args("]"));
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            const std::string name = ident();
            if (eat("(")) return make(Op::Call, args(")"), name);
            return make(Op::Var, {}, name);
        }
        fail(std::string("unexpected \"") + c + "\"");
    }

    const std::string& s_;
    size_t i_ = 0;
};

// Parsed once per text.
struct Compiled {
    NodeP root;
    std::string error;
};
std::mutex g_cacheMutex;
std::unordered_map<std::string, std::shared_ptr<Compiled>> g_cache;

// Every name is known, or named by an earlier line: a word half typed ("wiggle" on the way to
// "wiggle(2, 10)") is not an expression yet.
void checkNames(const Node& n, std::vector<std::string>& named) {
    const std::vector<std::string>& known = expressionNames();
    if (n.op == Op::Seq) {
        for (size_t k = 0; k + 1 < n.kids.size(); ++k) {
            checkNames(*n.kids[k]->kids[0], named);
            named.push_back(n.kids[k]->name);
        }
        checkNames(*n.kids.back(), named);
        return;
    }
    if (n.op == Op::Var && std::find(named.begin(), named.end(), n.name) == named.end() &&
        std::find(known.begin(), known.end(), n.name) == known.end())
        throw Error("unknown name \"" + n.name + "\"" + suggest(n.name, known, false));
    if (n.op == Op::Call) {
        const std::vector<std::string>& functions = expressionFunctions();
        if (std::find(functions.begin(), functions.end(), n.name) == functions.end())
            throw Error("unknown function \"" + n.name + "\"" + suggest(n.name, functions, false));
    }
    for (const NodeP& k : n.kids) checkNames(*k, named);
}

std::shared_ptr<Compiled> compile(const std::string& text) {
    std::lock_guard lock(g_cacheMutex);
    if (auto it = g_cache.find(text); it != g_cache.end()) return it->second;
    auto c = std::make_shared<Compiled>();
    try {
        c->root = Parser(text).parse();
        std::vector<std::string> named;
        checkNames(*c->root, named);
    } catch (const Error& e) {
        c->root.reset();
        c->error = e.what();
    }
    if (g_cache.size() > 4096) g_cache.clear();
    g_cache[text] = c;
    return c;
}

// --- where an expression lives ----------------------------------------------------------

struct Owner {
    const Layer* layer = nullptr;
    std::string propId;
};

struct Context {
    const Composition* comp = nullptr;
    std::unordered_map<const AnimatedBase*, std::pair<uint64_t, std::string>> owners; // property -> layer id, property id
    std::unordered_map<const AnimatedBase*, std::string> errors;
    int depth = 0;
};
thread_local Context t_ctx;
thread_local const Composition* t_editorComp = nullptr;

bool inside(const void* p, const void* begin, size_t bytes) {
    return static_cast<const char*>(p) >= static_cast<const char*>(begin) && static_cast<const char*>(p) < static_cast<const char*>(begin) + bytes;
}
bool belongs(const AnimatedBase* p, const Layer& l) {
    return inside(p, &l, sizeof(Layer)) || (!l.effects.empty() && inside(p, l.effects.data(), l.effects.size() * sizeof(Effect))) ||
           (!l.plugin.params.empty() && inside(p, l.plugin.params.data(), l.plugin.params.size() * sizeof(PluginParam)));
}

void bind(const Composition& comp) {
    t_ctx.owners.clear();
    for (const Layer& l : comp.layers)
        for (const PropertyRef& p : const_cast<Layer&>(l).properties())
            if (p.anim->hasExpression()) t_ctx.owners[p.anim] = {l.id, p.id};
}

Owner ownerOf(const AnimatedBase& prop) {
    const Composition* comp = t_ctx.comp;
    if (!comp) return {};
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (auto it = t_ctx.owners.find(&prop); it != t_ctx.owners.end())
            if (const Layer* l = comp->findLayer(it->second.first); l && belongs(&prop, *l)) return {l, it->second.second};
        if (attempt == 0) bind(*comp); // new or moved since: look again
    }
    return {};
}

// --- evaluation -----------------------------------------------------------------------------

struct Ref { // a value, a layer or a property of a layer
    enum Kind { Value, LayerRef, PropRef } kind = Value;
    Val v;
    const Layer* layer = nullptr;
    std::string prop;
};

struct Eval {
    const AnimatedBase& self;
    Owner owner;
    double t;
    Val value; // the keyframed value
    mutable std::vector<std::pair<std::string, Val>> vars; // name = ... lines, in order

    float noise(float x, uint32_t seed) const { // smooth 1D value noise in [-1, 1]
        auto hash = [seed](int32_t i) {
            uint32_t h = static_cast<uint32_t>(i) * 374761393u + seed * 668265263u;
            h = (h ^ (h >> 13)) * 1274126177u;
            return static_cast<float>(h ^ (h >> 16)) / 4294967295.0f * 2.0f - 1.0f;
        };
        const float f = std::floor(x);
        const float u = x - f, s = u * u * (3.0f - 2.0f * u);
        const int32_t i = static_cast<int32_t>(f);
        return hash(i) + (hash(i + 1) - hash(i)) * s;
    }
    uint32_t seedOf(float extra = 0) const {
        uint32_t h = 2166136261u;
        auto mix = [&h](const void* data, size_t n) {
            for (size_t i = 0; i < n; ++i) h = (h ^ static_cast<const unsigned char*>(data)[i]) * 16777619u;
        };
        if (owner.layer) {
            mix(&owner.layer->id, sizeof owner.layer->id);
            mix(owner.propId.data(), owner.propId.size());
        }
        mix(&extra, sizeof extra);
        return h;
    }

    Val keyed(double at) const {
        Val out;
        out.n = self.components();
        self.keyedValue(at, out.v);
        return out;
    }

    Val propertyAt(const Layer& layer, const std::string& id, double at) const {
        Layer& l = const_cast<Layer&>(layer);
        auto props = l.properties();
        const PropertyRef* p = l.findProperty(props, id.c_str());
        if (!p) throw Error("\"" + layer.name + "\" has no property \"" + id + "\"");
        if (t_ctx.depth > 8) throw Error("expressions read each other in a circle");
        ++t_ctx.depth;
        const std::vector<float> v = propertyValue(*p, at);
        --t_ctx.depth;
        Val out;
        out.n = static_cast<int>(v.size());
        for (int i = 0; i < out.n && i < 4; ++i) out.v[i] = v[i];
        return out;
    }

    Val value_(const Ref& r) const {
        if (r.kind == Ref::Value) return r.v;
        if (r.kind == Ref::PropRef) return propertyAt(*r.layer, r.prop, t);
        throw Error("a layer is not a number: add a property (layer(\"Ball\").position)");
    }

    Val num(const Node& n) const { return value_(run(n)); }
    float scalar(const Node& n) const {
        const Val v = num(n);
        return v.v[0];
    }

    static Val each(const Val& a, const Val& b, float (*f)(float, float)) {
        Val out;
        out.n = std::max(a.n, b.n);
        for (int i = 0; i < out.n; ++i) out.v[i] = f(a.v[a.n == 1 ? 0 : std::min(i, a.n - 1)], b.v[b.n == 1 ? 0 : std::min(i, b.n - 1)]);
        return out;
    }
    static Val map1(Val a, float (*f)(float)) {
        for (int i = 0; i < a.n; ++i) a.v[i] = f(a.v[i]);
        return a;
    }

    Val call(const Node& n) const {
        const std::string& f = n.name;
        const auto& k = n.kids;
        auto need = [&](size_t count) {
            if (k.size() < count) throw Error(f + "() needs " + std::to_string(count) + " value" + (count == 1 ? "" : "s"));
        };
        auto arg = [&](size_t i) { return num(*k[i]); };
        using F1 = float (*)(float);
        static const std::unordered_map<std::string, F1> unary = {
            {"sin", [](float x) { return std::sin(x); }},    {"cos", [](float x) { return std::cos(x); }},
            {"tan", [](float x) { return std::tan(x); }},    {"asin", [](float x) { return std::asin(x); }},
            {"acos", [](float x) { return std::acos(x); }},  {"atan", [](float x) { return std::atan(x); }},
            {"sqrt", [](float x) { return std::sqrt(std::max(0.0f, x)); }},
            {"abs", [](float x) { return std::fabs(x); }},   {"floor", [](float x) { return std::floor(x); }},
            {"ceil", [](float x) { return std::ceil(x); }},  {"round", [](float x) { return std::round(x); }},
            {"fract", [](float x) { return x - std::floor(x); }}, {"sign", [](float x) { return x > 0 ? 1.0f : x < 0 ? -1.0f : 0.0f; }},
            {"exp", [](float x) { return std::exp(x); }},    {"log", [](float x) { return std::log(std::max(1e-30f, x)); }},
            {"degrees", [](float x) { return x * 57.2957795f; }}, {"radians", [](float x) { return x * 0.0174532925f; }},
        };
        if (auto it = unary.find(f); it != unary.end()) {
            need(1);
            return map1(arg(0), it->second);
        }
        if (f == "min" || f == "max") {
            need(2);
            Val v = arg(0);
            for (size_t i = 1; i < k.size(); ++i)
                v = each(v, arg(i), f == "min" ? +[](float a, float b) { return std::min(a, b); } : +[](float a, float b) { return std::max(a, b); });
            return v;
        }
        if (f == "pow") return need(2), each(arg(0), arg(1), [](float a, float b) { return std::pow(a, b); });
        if (f == "atan2") return need(2), each(arg(0), arg(1), [](float a, float b) { return std::atan2(a, b); });
        if (f == "clamp") {
            need(3);
            return each(each(arg(0), arg(1), [](float a, float b) { return std::max(a, b); }), arg(2), [](float a, float b) { return std::min(a, b); });
        }
        if (f == "lerp" || f == "mix") {
            need(3);
            const Val a = arg(0), b = arg(1);
            const float u = scalar(*k[2]);
            Val out = each(a, b, [](float x, float y) { return y - x; });
            for (int i = 0; i < out.n; ++i) out.v[i] = a.v[a.n == 1 ? 0 : i] + out.v[i] * u;
            return out;
        }
        if (f == "map") { // map(x, from_low, from_high, to_low, to_high), clamped
            need(5);
            const float x = scalar(*k[0]), a = scalar(*k[1]), b = scalar(*k[2]);
            const float u = std::clamp(b != a ? (x - a) / (b - a) : 0.0f, 0.0f, 1.0f);
            const Val lo = arg(3), hi = arg(4);
            Val out = each(lo, hi, [](float x0, float y0) { return y0 - x0; });
            for (int i = 0; i < out.n; ++i) out.v[i] = lo.v[lo.n == 1 ? 0 : i] + out.v[i] * u;
            return out;
        }
        if (f == "smooth" || f == "ease") { // 0..1 eased in and out
            need(1);
            return map1(arg(0), [](float x) {
                x = std::clamp(x, 0.0f, 1.0f);
                return x * x * (3.0f - 2.0f * x);
            });
        }
        if (f == "length") {
            need(1);
            const Val v = arg(0);
            float s = 0;
            for (int i = 0; i < v.n; ++i) s += v.v[i] * v.v[i];
            return Val::num(std::sqrt(s));
        }
        if (f == "distance") {
            need(2);
            const Val d = each(arg(0), arg(1), [](float a, float b) { return a - b; });
            float s = 0;
            for (int i = 0; i < d.n; ++i) s += d.v[i] * d.v[i];
            return Val::num(std::sqrt(s));
        }
        if (f == "random") { // random(), random(max), random(min, max): the same every frame
            const float r = (noise(0.5f, seedOf(k.size() > 2 ? scalar(*k[2]) : 0.0f)) + 1.0f) * 0.5f;
            if (k.empty()) return Val::num(r);
            if (k.size() == 1) return each(arg(0), Val::num(r), [](float a, float b) { return a * b; });
            const Val lo = arg(0), hi = arg(1);
            Val out = each(lo, hi, [](float a, float b) { return b - a; });
            for (int i = 0; i < out.n; ++i) out.v[i] = lo.v[lo.n == 1 ? 0 : i] + out.v[i] * r;
            return out;
        }
        if (f == "noise") {
            need(1);
            return Val::num(noise(scalar(*k[0]), seedOf(k.size() > 1 ? scalar(*k[1]) : 0.0f)));
        }
        if (f == "wiggle") { // wiggle(times a second, amount): a smooth shake, different per component
            need(2);
            const float freq = scalar(*k[0]);
            const Val amount = arg(1);
            Val out = value;
            const float x = static_cast<float>(t) * freq;
            for (int i = 0; i < out.n; ++i) {
                const uint32_t s = seedOf(static_cast<float>(i + 1));
                const float n2 = noise(x, s) * 0.7f + noise(x * 2.1f + 13.0f, s ^ 0x9e3779b9u) * 0.3f;
                out.v[i] += n2 * amount.v[amount.n == 1 ? 0 : std::min(i, amount.n - 1)];
            }
            return out;
        }
        if (f == "loop" || f == "pingpong") { // the keyframes again and again, after the last one
            const int n = self.keyCount();
            if (n < 2) return value;
            const double a = self.keyTime(0), b = self.keyTime(n - 1), span = b - a;
            double at = t;
            if (t > b && span > 1e-6) {
                const double cycles = (t - a) / span;
                const double phase = cycles - std::floor(cycles);
                at = f == "pingpong" && static_cast<long long>(std::floor(cycles)) % 2 == 1 ? b - phase * span : a + phase * span;
            }
            Val out;
            out.n = self.components();
            self.keyedValue(at, out.v);
            return out;
        }
        if (f == "valueAt") { // this property's keyframes at another time
            need(1);
            return keyed(scalar(*k[0]));
        }
        if (f == "vec" || f == "rgb" || f == "rgba") {
            Val out;
            out.n = static_cast<int>(std::min<size_t>(4, k.size()));
            for (int i = 0; i < out.n; ++i) out.v[i] = scalar(*k[i]);
            if (f != "vec") { // 0..255
                for (int i = 0; i < out.n && i < 3; ++i) out.v[i] /= 255.0f;
                if (out.n == 3) out.n = 4, out.v[3] = 1.0f;
            }
            return out;
        }
        throw Error("unknown function \"" + f + "\"");
    }

    Ref run(const Node& n) const {
        auto v = [](Val x) { return Ref{Ref::Value, x}; };
        switch (n.op) {
        case Op::Num: return v(Val::num(n.number));
        case Op::Str: throw Error("a name in quotes goes in layer(\"...\")");
        case Op::Seq: {
            for (size_t k = 0; k + 1 < n.kids.size(); ++k) vars.emplace_back(n.kids[k]->name, value_(run(*n.kids[k]->kids[0])));
            return run(*n.kids.back());
        }
        case Op::Let: throw Error("cannot work that out");
        case Op::Var: {
            const std::string& x = n.name;
            for (auto it = vars.rbegin(); it != vars.rend(); ++it)
                if (it->first == x) return v(it->second);
            const Composition* comp = t_ctx.comp;
            const Layer* l = owner.layer;
            if (x == "time") return v(Val::num(static_cast<float>(t)));
            if (x == "value") return v(value);
            if (x == "pi") return v(Val::num(kPi));
            if (x == "fps") return v(Val::num(comp ? static_cast<float>(comp->fps) : 30.0f));
            if (x == "frame") return v(Val::num(comp ? static_cast<float>(comp->frameAt(t)) : static_cast<float>(t * 30.0)));
            if (x == "width" || x == "height") return v(Val::num(comp ? static_cast<float>(x == "width" ? comp->width : comp->height) : 0.0f));
            if (x == "center") {
                Val c;
                c.n = 2;
                c.v[0] = comp ? comp->width * 0.5f : 960.0f, c.v[1] = comp ? comp->height * 0.5f : 540.0f;
                return v(c);
            }
            if (x == "velocity" || x == "speed") { // how fast the keyframes change, per second
                const double h = 0.5 / (comp && comp->fps > 0 ? comp->fps : 30.0);
                const double a = std::max(0.0, t - h), b = t + h;
                Val d = each(keyed(b), keyed(a), [](float p, float q) { return p - q; });
                float s = 0;
                for (int i = 0; i < d.n; ++i) d.v[i] /= static_cast<float>(b - a), s += d.v[i] * d.v[i];
                return v(x == "speed" ? Val::num(std::sqrt(s)) : d);
            }
            if (x == "start" || x == "end" || x == "duration" || x == "progress" || x == "index") {
                if (!l) throw Error("\"" + x + "\" needs the layer (not known here)");
                if (x == "start") return v(Val::num(static_cast<float>(l->inPoint)));
                if (x == "end") return v(Val::num(static_cast<float>(l->outPoint)));
                if (x == "duration") return v(Val::num(static_cast<float>(l->outPoint - l->inPoint)));
                if (x == "index") return v(Val::num(static_cast<float>(comp ? comp->indexOf(l->id) + 1 : 1)));
                const double len = std::max(1e-6, l->outPoint - l->inPoint);
                return v(Val::num(static_cast<float>(std::clamp((t - l->inPoint) / len, 0.0, 1.0))));
            }
            throw Error("unknown name \"" + x + "\"");
        }
        case Op::Call: {
            if (n.name == "layer") { // layer("Name"), layer(index from the top), layer(): this one
                const Composition* comp = t_ctx.comp;
                if (!comp) throw Error("layer() needs the composition (not known here)");
                const Layer* found = nullptr;
                if (n.kids.empty()) found = owner.layer;
                else if (n.kids[0]->op == Op::Str) {
                    for (const Layer& l : comp->layers)
                        if (l.name == n.kids[0]->name) found = &l;
                    if (!found) throw Error("no layer is named \"" + n.kids[0]->name + "\"");
                } else {
                    const int i = static_cast<int>(scalar(*n.kids[0])) - 1;
                    if (i >= 0 && i < static_cast<int>(comp->layers.size())) found = &comp->layers[i];
                }
                if (!found) throw Error("that layer does not exist");
                Ref r;
                r.kind = Ref::LayerRef;
                r.layer = found;
                return r;
            }
            return v(call(n));
        }
        case Op::Vec: {
            Val out;
            out.n = static_cast<int>(std::min<size_t>(4, n.kids.size()));
            if (out.n == 0) throw Error("an empty [ ]");
            for (int i = 0; i < out.n; ++i) out.v[i] = scalar(*n.kids[i]);
            return v(out);
        }
        case Op::Neg: return v(map1(num(*n.kids[0]), [](float x) { return -x; }));
        case Op::Not: return v(Val::num(scalar(*n.kids[0]) == 0.0f ? 1.0f : 0.0f));
        case Op::Cond: return scalar(*n.kids[0]) != 0.0f ? run(*n.kids[1]) : run(*n.kids[2]);
        case Op::And: return v(Val::num(scalar(*n.kids[0]) != 0.0f && scalar(*n.kids[1]) != 0.0f ? 1.0f : 0.0f));
        case Op::Or: return v(Val::num(scalar(*n.kids[0]) != 0.0f || scalar(*n.kids[1]) != 0.0f ? 1.0f : 0.0f));
        case Op::Member: {
            const Ref obj = run(*n.kids[0]);
            if (obj.kind == Ref::LayerRef) { // a property of that layer, or a few facts about it
                Ref r;
                r.kind = Ref::PropRef;
                r.layer = obj.layer;
                r.prop = n.name;
                if (n.name == "start") return v(Val::num(static_cast<float>(obj.layer->inPoint)));
                if (n.name == "end") return v(Val::num(static_cast<float>(obj.layer->outPoint)));
                if (n.name == "z") r.prop = "position_z";
                return r;
            }
            // layer("Ball").p.progress, layer("Ball").fx0.size: ids with a dot.
            if (obj.kind == Ref::PropRef && obj.prop.find('.') == std::string::npos &&
                (obj.prop == "p" || (obj.prop.size() > 2 && obj.prop.compare(0, 2, "fx") == 0 &&
                                     std::all_of(obj.prop.begin() + 2, obj.prop.end(), [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)) != 0; })))) {
                Ref r = obj;
                r.prop += "." + n.name;
                return r;
            }
            const Val x = value_(obj);
            static const char* names[] = {"x", "y", "z", "w", "r", "g", "b", "a"};
            for (int i = 0; i < 8; ++i)
                if (n.name == names[i]) {
                    const int c = i % 4;
                    if (c >= x.n) throw Error("\"" + n.name + "\" of something with " + std::to_string(x.n) + " value(s)");
                    return v(Val::num(x.v[c]));
                }
            throw Error("unknown part \"." + n.name + "\"");
        }
        case Op::Index: {
            const Val x = num(*n.kids[0]);
            const int i = static_cast<int>(scalar(*n.kids[1]));
            if (i < 0 || i >= x.n) throw Error("[" + std::to_string(i) + "] is outside it");
            return v(Val::num(x.v[i]));
        }
        case Op::Apply: { // property(time)
            const Ref obj = run(*n.kids[0]);
            if (obj.kind != Ref::PropRef || n.kids.size() < 2) throw Error("only a property can be read at another time");
            return v(propertyAt(*obj.layer, obj.prop, scalar(*n.kids[1])));
        }
        default: break;
        }
        const Val a = num(*n.kids[0]), b = num(*n.kids[1]);
        switch (n.op) {
        case Op::Add: return v(each(a, b, [](float x, float y) { return x + y; }));
        case Op::Sub: return v(each(a, b, [](float x, float y) { return x - y; }));
        case Op::Mul: return v(each(a, b, [](float x, float y) { return x * y; }));
        case Op::Div: return v(each(a, b, [](float x, float y) { return y != 0.0f ? x / y : 0.0f; }));
        case Op::Mod: return v(each(a, b, [](float x, float y) { return y != 0.0f ? x - y * std::floor(x / y) : 0.0f; }));
        case Op::Pow: return v(each(a, b, [](float x, float y) { return std::pow(x, y); }));
        case Op::Lt: return v(Val::num(a.v[0] < b.v[0]));
        case Op::Gt: return v(Val::num(a.v[0] > b.v[0]));
        case Op::Le: return v(Val::num(a.v[0] <= b.v[0]));
        case Op::Ge: return v(Val::num(a.v[0] >= b.v[0]));
        case Op::Eq: return v(Val::num(std::fabs(a.v[0] - b.v[0]) < 1e-6f));
        case Op::Ne: return v(Val::num(std::fabs(a.v[0] - b.v[0]) >= 1e-6f));
        default: throw Error("cannot work that out");
        }
    }
};

} // namespace

ExpressionScope::ExpressionScope(const Composition& comp) : previous_(t_ctx.comp) {
    t_ctx.comp = &comp;
    bind(comp);
}

ExpressionScope::~ExpressionScope() {
    t_ctx.comp = previous_ ? previous_ : t_editorComp;
    if (t_ctx.comp) bind(*t_ctx.comp);
}

void setExpressionComposition(const Composition* comp) {
    // Outside any scope (or in the editor's own) the editor's composition is what reads: a
    // closed editor's composition must never stay bound once another one, or none, takes over.
    const bool editors = !t_ctx.comp || t_ctx.comp == t_editorComp || t_ctx.comp == comp;
    t_editorComp = comp;
    if ((editors || !comp) && t_ctx.comp != comp) {
        t_ctx.comp = comp;
        t_ctx.owners.clear();
        t_ctx.errors.clear();
    }
}

std::string checkExpression(const std::string& expression) { return compile(expression)->error; }

std::string expressionError(const AnimatedBase& property) {
    if (const std::string syntax = checkExpression(property.expression); !syntax.empty()) return syntax;
    auto it = t_ctx.errors.find(&property);
    return it == t_ctx.errors.end() ? std::string() : it->second;
}

namespace {

// Runs `compiled` as `property`'s expression: its value in `out`, or the problem.
std::string run(const Compiled& compiled, const AnimatedBase& property, const Owner& owner, double t, float out[4]) {
    if (!compiled.root) return compiled.error;
    Eval e{property, owner, t, {}};
    e.value = e.keyed(t);
    try {
        const Val r = e.value_(e.run(*compiled.root));
        const int n = property.components();
        for (int i = 0; i < n; ++i) out[i] = r.v[r.n == 1 ? 0 : std::min(i, r.n - 1)];
        if (n == 4 && r.n == 3) out[3] = 1.0f; // [r, g, b]: opaque
        for (int i = 0; i < n; ++i)
            if (!std::isfinite(out[i])) throw Error("the result is not a number (a division by zero?)");
        return {};
    } catch (const Error& err) {
        return err.what();
    }
}

} // namespace

bool evaluateExpression(const AnimatedBase& property, double t, float out[4]) {
    const auto compiled = compile(property.expression);
    if (!compiled->root) return false;
    if (std::string problem = run(*compiled, property, ownerOf(property), t, out); !problem.empty()) {
        t_ctx.errors[&property] = std::move(problem);
        return false;
    }
    t_ctx.errors.erase(&property);
    return true;
}

std::string evaluateExpressionFor(const std::string& expression, const Layer& layer, const PropertyRef& prop, double t,
                                  std::vector<float>& out) {
    float v[4] = {0, 0, 0, 0};
    const std::string problem = run(*compile(expression), *prop.anim, {&layer, prop.id}, t, v);
    out.assign(v, v + prop.anim->components());
    return problem;
}

const std::vector<std::string>& expressionNames() {
    static const std::vector<std::string> names = {"time",     "frame",    "fps",   "value", "velocity", "speed",  "start", "end",
                                                   "duration", "progress", "index", "width", "height",   "center", "pi"};
    return names;
}

const std::vector<std::string>& expressionFunctions() {
    static const std::vector<std::string> functions = {
        "sin",   "cos",   "tan",    "asin",   "acos",     "atan",   "atan2", "degrees", "radians", "abs",      "floor",   "ceil",
        "round", "fract", "sign",   "sqrt",   "pow",      "exp",    "log",   "min",     "max",     "clamp",    "lerp",    "mix",
        "map",   "smooth", "ease",  "length", "distance", "wiggle", "noise", "random",  "loop",    "pingpong", "valueAt", "vec",
        "rgb",   "rgba",  "layer"};
    return functions;
}


} // namespace bemotion
