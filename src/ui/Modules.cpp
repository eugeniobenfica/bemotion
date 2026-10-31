#include "ui/Modules.h"

#include "app/Settings.h"
#include "ui/AppSignals.h"
#include "ui/Language.h"

#include <algorithm>
#include <string>

namespace bemotion::ui {

const std::vector<Module>& modules() {
    static const std::vector<Module> list = {
        {kSpeechToText, TR_NOOP("Speech to text"), TR_NOOP("Turns what is said in a clip into subtitles (Transcribe)"), Icon::Subtitles},
        {kPointTracking, TR_NOOP("Point tracking"), TR_NOOP("The Track Point tool: a layer follows a detail of a video"), Icon::Track},
        {kPuppetWarp, TR_NOOP("Puppet and Warp"), TR_NOOP("The Puppet Pin and Warp tools, which bend a layer (bent layers keep their shape)"), Icon::Puppet},
        {kSvgImport, TR_NOOP("SVG import"), TR_NOOP("File › Import SVG and dropped .svg files become shape layers"), Icon::Pen},
        {kLottieExport, TR_NOOP("Lottie export"), TR_NOOP("File › Export Lottie: the animation as JSON for the web and apps"), Icon::Export},
        {kScopes, TR_NOOP("Scopes"), TR_NOOP("Waveform, RGB parade, vectorscope and histogram of the frame"), Icon::Scopes},
    };
    return list;
}

bool moduleEnabled(std::string_view id) {
    const Settings& s = settings();
    if (s.essentialsOnly) return false;
    return std::find(s.disabledModules.begin(), s.disabledModules.end(), id) == s.disabledModules.end();
}

void setModuleEnabled(std::string_view id, bool on) {
    auto& off = settings().disabledModules;
    std::erase(off, std::string(id));
    if (!on) off.emplace_back(id);
    saveSettings();
    emit AppSignals::get().modulesChanged();
}

} // namespace bemotion::ui
