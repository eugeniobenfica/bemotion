#pragma once

// The interface's text in the user's language (see core/Language.h for the files). Every text
// the interface shows goes through tr(): a literal as tr("Save"), a text from a table (marked
// N_() in the core, TR_NOOP() here) as tr(label) where it is shown, and std::string text as
// trs(text). Classes with Q_OBJECT have their own tr(), which ends up in the same place.

#include "core/Language.h"

#include <QString>

#include <filesystem>
#include <string>
#include <vector>

// A text in a table of the interface, translated where it is shown.
#define TR_NOOP(text) text

namespace bemotion {

QString tr(const char* english);
QString trs(const std::string& english);
// A label from a table (a property, a section): translated, and a numbered one ("Gaussian Blur
// 2", "Mask 3") as its name and number. Text already translated, or not in English, stays.
QString trLabel(const QString& text);

namespace ui {

struct LanguageInfo {
    std::string code, name;
    std::filesystem::path file;
};
// The bundled languages (assets/languages) and the user's own (<app data>/languages), by name.
std::vector<LanguageInfo> availableLanguages();
// Installs the language of the "language" setting ("auto": the system's, when there is a file
// for it; "en": none), replacing the one in use. The interface reads its texts as it is built:
// after a change, AppSignals::languageChanged has the windows build themselves again.
void installLanguage(const std::string& setting);
std::string currentLanguage(); // the code in use, "en" for English

} // namespace ui
} // namespace bemotion
