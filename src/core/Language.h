#pragma once

// Interface languages. A language file is JSON that maps each English text of the interface to
// its translation (assets/languages/<code>.json, and the user's own in <app data>/languages):
//
//   {"format": "bemotion-language", "code": "pt-BR", "name": "Português (Brasil)",
//    "strings": {"File": "Arquivo", "%1 effects": "%1 efeitos", ...}}
//
// A text missing from the file stays in English. Translations keep the text's placeholders
// (%1, %2... and {name}). The interface marks its texts with tr() (ui/Language.h); tables of
// texts here in the core mark them with N_() so tools/languages.py finds them all.

#include <map>
#include <string>
#include <vector>

// A text shown in the interface, kept as it is here and translated where it is shown.
#define N_(text) text

namespace bemotion {

struct LanguageFile {
    std::string code; // "pt-BR"
    std::string name; // in its own language: "Português (Brasil)"
    std::map<std::string, std::string> strings; // English -> translation (empty translations are left out)
};

bool parseLanguage(const std::string& json, LanguageFile& out, std::string* error = nullptr);

// The placeholders in a text (%1..%99, %n, {word}), sorted.
std::vector<std::string> placeholdersOf(const std::string& text);

// The file that fits a system locale ("pt_BR", "pt-BR", "pt"): the same code, else the same
// language; -1 if none.
int matchLanguage(const std::vector<std::string>& codes, const std::string& locale);

} // namespace bemotion
