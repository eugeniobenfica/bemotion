// A light fuzzer for the core's readers: "never crash on bad input", checked.
//
//   bemotion_fuzz [--seconds N] [--seed S] [--at I]
//
// Feeds mutated copies of the files in tests/corpus/<kind>/ to what reads them:
//   project/     deserializeProject (and the repaired result serialized, read again)
//   ops/         applyOps on a small project
//   svg/         addSvg
//   expression/  checkExpression, and the expression run on a property
//   mesh/        loadMeshFile (by extension), then simplifyMesh
// Each iteration's input follows from the seed and its number alone: `--seed S --at I` runs
// that one again. A crash, an exception or an iteration over 10 s writes the input to
// fuzz-failure.<ext> in the current folder and prints how to replay it.

#include "core/Expressions.h"
#include "core/MeshFile.h"
#include "core/Ops.h"
#include "core/Project.h"
#include "core/Serialization.h"
#include "core/Svg.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(__SANITIZE_ADDRESS__)
#define BEMOTION_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define BEMOTION_ASAN 1
#endif
#endif
#ifdef BEMOTION_ASAN
extern "C" void __sanitizer_set_death_callback(void (*callback)());
#endif

using namespace bemotion;
using json = nlohmann::json;

namespace {

struct Seed {
    std::string kind; // the corpus folder
    std::string ext;  // lower case, without the dot
    std::string data;
};

// What is being tried now, for the failure report.
std::string g_input;
std::string g_ext = "txt";
uint64_t g_seed = 0, g_iteration = 0;
std::atomic<uint64_t> g_started{0};

void writeFailure(const char* why) {
    const std::string file = "fuzz-failure." + g_ext;
    if (FILE* f = std::fopen(file.c_str(), "wb")) {
        std::fwrite(g_input.data(), 1, g_input.size(), f);
        std::fclose(f);
    }
    std::fprintf(stderr, "\nfuzz: %s at iteration %llu; input in %s; replay with --seed %llu --at %llu\n", why,
                 static_cast<unsigned long long>(g_iteration), file.c_str(), static_cast<unsigned long long>(g_seed),
                 static_cast<unsigned long long>(g_iteration));
    std::fflush(stderr);
}

void onSignal(int sig) {
    writeFailure(sig == SIGSEGV ? "crash (SIGSEGV)" : sig == SIGFPE ? "crash (SIGFPE)" : "crash (abort)");
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

std::vector<Seed> readCorpus(const std::filesystem::path& root) {
    std::vector<Seed> seeds;
    std::error_code ec;
    for (const auto& kind : std::filesystem::directory_iterator(root, ec)) {
        if (!kind.is_directory()) continue;
        for (const auto& file : std::filesystem::directory_iterator(kind.path(), ec)) {
            if (!file.is_regular_file()) continue;
            std::ifstream in(file.path(), std::ios::binary);
            std::stringstream ss;
            ss << in.rdbuf();
            std::string ext = file.path().extension().generic_string();
            if (!ext.empty()) ext.erase(0, 1);
            for (char& c : ext)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            seeds.push_back({kind.path().filename().generic_string(), ext, ss.str()});
        }
    }
    return seeds;
}

// --- mutations ---------------------------------------------------------------------------------

const char* const kTokens[] = {
    "{",     "}",      "[",        "]",         "\"",     ",",    ":",          "-1",          "0",
    "1e308", "-1e308", "1e-320",   "NaN",       "null",   "true", "4294967296", "-2147483649", "18446744073709551615",
    "\"\"",  "[]",     "{}",       "\\u0000",   "<",      ">",    "/>",         "<path d=\"",  "(",
    ")",     "?",      "layer(\"", "wiggle(",   "loop()", "time", "/",          "%",           "e",
    "M",     "Z",      "C",        "999999999", "\xff",   "\xc3"};

json randomValue(std::mt19937_64& rng) {
    switch (rng() % 9) {
    case 0: return nullptr;
    case 1: return static_cast<int64_t>(rng());
    case 2: return -1;
    case 3: return 1e308;
    case 4: return -0.0;
    case 5: return std::string(rng() % 40, 'x');
    case 6: return json::array({0, "a", nullptr});
    case 7: return json::object({{"x", 1}});
    default: return rng() % 2 == 0;
    }
}

// Changes one random node of a JSON document: a value replaced, a key or element removed or
// copied. Keeps the input well-formed so the reader gets past the parser.
void mutateJson(json& j, std::mt19937_64& rng, int depth = 0) {
    if ((j.is_object() || j.is_array()) && !j.empty() && depth < 12 && rng() % 4 != 0) {
        auto it = j.begin();
        std::advance(it, static_cast<long>(rng() % j.size()));
        if (rng() % 8 == 0) {
            if (j.is_object())
                j.erase(it.key());
            else
                j.erase(static_cast<size_t>(std::distance(j.begin(), it)));
        } else if (rng() % 12 == 0 && j.is_array()) {
            j.push_back(*it);
        } else {
            mutateJson(*it, rng, depth + 1);
        }
        return;
    }
    if (j.is_number() && rng() % 2 == 0)
        j = j.get<double>() * (rng() % 2 ? -1.0 : 1e6);
    else
        j = randomValue(rng);
}

void mutateBytes(std::string& s, std::mt19937_64& rng, const std::vector<Seed>& seeds) {
    const size_t at = s.empty() ? 0 : rng() % (s.size() + 1);
    switch (rng() % 6) {
    case 0: // flip a bit
        if (!s.empty()) s[rng() % s.size()] ^= static_cast<char>(1 << (rng() % 8));
        break;
    case 1: // a token in
        s.insert(at, kTokens[rng() % std::size(kTokens)]);
        break;
    case 2: { // a piece out
        const size_t n = std::min(s.size() - std::min(at, s.size()), static_cast<size_t>(rng() % 64));
        s.erase(std::min(at, s.size()), n);
        break;
    }
    case 3: { // a piece repeated
        if (s.empty()) break;
        const size_t from = rng() % s.size(), n = std::min(s.size() - from, static_cast<size_t>(rng() % 256));
        s.insert(at, s.substr(from, n));
        break;
    }
    case 4: // a digit run made long
        s.insert(at, std::string(1 + rng() % 30, static_cast<char>('0' + rng() % 10)));
        break;
    default: { // spliced with another seed
        const std::string& other = seeds[rng() % seeds.size()].data;
        if (other.empty()) break;
        const size_t from = rng() % other.size();
        s = s.substr(0, std::min(at, s.size())) + other.substr(from, rng() % 512);
        break;
    }
    }
}

std::string mutate(const Seed& seed, std::mt19937_64& rng, const std::vector<Seed>& seeds) {
    std::string s = seed.data;
    const bool isJson = seed.ext == "json" || seed.ext == "bemotion" || seed.ext == "gltf";
    if (isJson && rng() % 2 == 0) {
        json j = json::parse(s, nullptr, false);
        if (!j.is_discarded()) {
            for (int n = 1 + static_cast<int>(rng() % 4); n > 0; --n)
                mutateJson(j, rng);
            return j.dump();
        }
    }
    for (int n = 1 + static_cast<int>(rng() % 4); n > 0; --n)
        mutateBytes(s, rng, seeds);
    if (s.size() > (1u << 20)) s.resize(1u << 20);
    return s;
}

// --- targets -----------------------------------------------------------------------------------

Project smallProject() {
    Project p;
    applyOps(p, json::parse(R"([{"op": "layer.add", "type": "shape", "shape": "ellipse", "name": "Ball"},
                                {"op": "layer.add", "type": "text", "name": "Box", "text": "Hi"}])"));
    return p;
}

void tryProject(const std::string& text) {
    Project p;
    std::vector<Issue> repairs;
    if (!deserializeProject(text, p, nullptr, &repairs)) return;
    // What loading repaired must read back as it is.
    Project again;
    if (!deserializeProject(serializeProject(p), again)) {
        writeFailure("a loaded project does not read back");
        std::exit(1);
    }
}

void tryOps(const std::string& text) {
    const json ops = json::parse(text, nullptr, false);
    if (ops.is_discarded()) return;
    Project p = smallProject();
    applyOps(p, ops);
}

void trySvg(const std::string& text) {
    Composition comp;
    addSvg(comp, text, "fuzz", 0.0);
}

void tryExpression(const std::string& text) {
    checkExpression(text);
    Project p = smallProject();
    const ExpressionScope scope(p.comp);
    for (Layer& l : p.comp.layers) {
        l.transform.rotation.expression = text;
        l.transform.position.expression = text;
    }
    for (Layer& l : p.comp.layers)
        for (double t : {0.0, 1.5, -3.0}) {
            l.transform.rotation.valueAt(t);
            l.transform.position.valueAt(t);
        }
}

void tryMesh(const std::string& ext, const std::string& data) {
    MeshData mesh;
    std::string error;
    if (!loadMeshFile(ext, data, [](const std::string&, std::string&) { return false; }, mesh, error)) return;
    simplifyMesh(mesh, 64);
}

void runOne(const Seed& seed, const std::string& input) {
    if (seed.kind == "project")
        tryProject(input);
    else if (seed.kind == "ops")
        tryOps(input);
    else if (seed.kind == "svg")
        trySvg(input);
    else if (seed.kind == "expression")
        tryExpression(input);
    else if (seed.kind == "mesh")
        tryMesh(seed.ext, input);
}

} // namespace

int main(int argc, char** argv) {
    // Numbers only: the corpus is found from the source folder (no paths to decode).
    double seconds = 10;
    uint64_t seed = static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
    long long only = -1;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!std::strcmp(argv[i], "--seconds"))
            seconds = std::atof(argv[i + 1]);
        else if (!std::strcmp(argv[i], "--seed"))
            seed = std::strtoull(argv[i + 1], nullptr, 10);
        else if (!std::strcmp(argv[i], "--at"))
            only = std::atoll(argv[i + 1]);
    }
    const std::vector<Seed> seeds = readCorpus(std::filesystem::path(BEMOTION_SOURCE_DIR) / "tests" / "corpus");
    if (seeds.empty()) {
        std::printf("fuzz: no corpus in tests/corpus\n");
        return 1;
    }
    std::map<std::string, int> perKind;
    for (const Seed& s : seeds)
        ++perKind[s.kind];
    g_seed = seed;
    std::signal(SIGSEGV, onSignal);
    std::signal(SIGABRT, onSignal);
    std::signal(SIGFPE, onSignal);
#ifdef BEMOTION_ASAN
    __sanitizer_set_death_callback([] { writeFailure("sanitizer error"); });
#endif
    std::set_terminate([] {
        writeFailure("uncaught exception");
        std::_Exit(1);
    });
    // A stuck input stops the run rather than the CI job's clock.
    std::thread([] {
        uint64_t seen = 0;
        int still = 0;
        for (;;) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            const uint64_t now = g_started.load();
            still = now == seen ? still + 1 : 0;
            seen = now;
            if (still >= 10) {
                writeFailure("an input took over 10 s");
                std::_Exit(1);
            }
        }
    }).detach();

    const auto start = std::chrono::steady_clock::now();
    uint64_t done = 0;
    for (uint64_t i = only >= 0 ? static_cast<uint64_t>(only) : 0;; ++i) {
        if (only >= 0 ? i > static_cast<uint64_t>(only)
                      : std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() >= seconds)
            break;
        std::mt19937_64 rng(seed ^ (i * 0x9E3779B97F4A7C15ull));
        const Seed& s = seeds[rng() % seeds.size()];
        g_iteration = i;
        g_ext = s.ext.empty() ? "txt" : s.ext;
        g_input = rng() % 16 == 0 ? s.data : mutate(s, rng, seeds);
        g_started.fetch_add(1);
        try {
            runOne(s, g_input);
        } catch (const std::exception& e) {
            writeFailure((std::string("exception: ") + e.what()).c_str());
            return 1;
        }
        ++done;
    }
    std::printf("fuzz: %llu inputs from %zu seeds (", static_cast<unsigned long long>(done), seeds.size());
    for (auto it = perKind.begin(); it != perKind.end(); ++it)
        std::printf("%s%s %d", it == perKind.begin() ? "" : ", ", it->first.c_str(), it->second);
    std::printf("), seed %llu: no failures\n", static_cast<unsigned long long>(seed));
    return 0;
}
