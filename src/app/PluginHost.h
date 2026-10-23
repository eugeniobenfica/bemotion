#pragma once

// Plugins: folders with a plugin.json manifest and a Lua script that
// registers object types (e.g. charts). Bundled plugins live in
// <assets>/plugins, user plugins in <app data>/plugins.
//
// Scripts run in a sandbox (no file, OS or module access) with an
// instruction budget per draw, so a broken plugin cannot hang the editor.
// See docs/PLUGINS.md for the API.

#include "core/Ops.h"
#include "core/Project.h"

#include <filesystem>
#include <string>
#include <vector>

struct lua_State;

namespace bemotion {

namespace render {
class PluginCanvas;
}

// Keyframes added to a parameter when the object is created (its intro animation).
struct PluginIntro {
    std::string param;
    float from = 0.0f;
    double duration = 1.0; // seconds; the key at the end holds the default value
};

struct PluginObjectType {
    std::string id; // "<plugin id>.<object id>"
    std::string pluginId;
    std::string name;
    std::string description;
    std::string icon; // "chart", "bars", "pie", "line" or a built-in shape icon
    PluginProps prototype;
    std::vector<PluginIntro> intro;
    int drawRef = -1; // Lua registry reference of the draw function
};

// An action a plugin adds to the Plugins menu: Lua that edits the project
// through operations (core/Ops.h), as one undoable step.
struct PluginCommand {
    std::string id; // "<plugin id>.<command id>"
    std::string pluginId;
    std::string name;
    std::string description;
    int runRef = -1; // Lua registry reference of the run function
};

struct Plugin {
    std::string id, name, version, author, description;
    std::filesystem::path dir;
    bool builtin = false;
    bool enabled = true;
    std::string error;     // load error (the plugin is inactive)
    std::string drawError; // last runtime error, cleared on success
    std::vector<std::string> log; // print() output
    // What the plugin may do beyond drawing (plugin.json "permissions"):
    //   "edit"  change the project (commands)     "media"  import media files
    std::vector<std::string> permissions;
    bool may(const std::string& permission) const;
    lua_State* L = nullptr;
};

class PluginHost {
public:
    static PluginHost& get();

    void load(); // (re)loads every plugin and installs the scene drawer
    void shutdown();
    // Reloading from UI code is deferred to the start of the next frame, when
    // nothing holds pointers into the plugin list.
    void requestReload() { reloadRequested_ = true; }
    void processPendingReload() {
        if (reloadRequested_) load();
        reloadRequested_ = false;
    }

    const std::vector<Plugin>& plugins() const { return plugins_; }
    // Object types of enabled plugins, in load order.
    std::vector<const PluginObjectType*> objectTypes() const;
    const PluginObjectType* findType(const std::string& id) const;
    Plugin* findPlugin(const std::string& id);

    void setEnabled(const std::string& pluginId, bool enabled);
    // Copies the folder containing `manifest` (a plugin.json) into the user plugins folder.
    bool install(const std::filesystem::path& manifest, std::string* error = nullptr);
    static std::filesystem::path userDir();

    Layer createLayer(const PluginObjectType& type, Vec2 position, double duration) const;
    // Brings a loaded layer's parameters up to date with the plugin's schema
    // (adds new parameters, refreshes labels and ranges; keeps values).
    void reconcile(Layer& layer) const;
    void reconcile(Composition& comp) const;

    void draw(const Layer& layer, double time, render::PluginCanvas& canvas);

    // Commands of enabled plugins, in load order.
    std::vector<const PluginCommand*> commands() const;
    // Runs a command on `project` (the playhead and selected layer ids are passed to it).
    // The project changes only if the whole command succeeds; `error` explains a failure.
    bool runCommand(const std::string& commandId, Project& project, double time, const std::vector<uint64_t>& selection,
                    const OpEnvironment& env, std::string& error);
    // Runs a standalone script (a "main" with the same `doc` API, permission "edit"):
    // `bemotion script <project> <file.lua>`. Its print() output goes to `log`.
    static bool runScript(const std::string& code, const std::string& chunkName, Project& project, double time,
                          const OpEnvironment& env, std::vector<std::string>& log, std::string& error);

private:
    void loadPlugin(const std::filesystem::path& dir, bool builtin);
    std::vector<Plugin> plugins_;
    std::vector<PluginObjectType> types_;
    std::vector<PluginCommand> commands_;
    bool reloadRequested_ = false;
};

} // namespace bemotion
