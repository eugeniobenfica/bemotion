#include "ui/Language.h"

#include "app/Platform.h"

#include <QCoreApplication>
#include <QHash>
#include <QLocale>
#include <QRegularExpression>
#include <QTranslator>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

namespace bemotion {

namespace {

// Qt's translation hook, fed from a language file: every tr() asks it, and it answers the
// interface's own texts.
class JsonTranslator : public QTranslator {
public:
    explicit JsonTranslator(const LanguageFile& file) {
        for (const auto& [english, translation] : file.strings)
            strings_.insert(QByteArray::fromStdString(english), QString::fromStdString(translation));
    }
    QString translate(const char* context, const char* source, const char*, int n) const override {
        // Ours only (bemotion's classes, plain tr(), QObject::tr of classes without Q_OBJECT):
        // Qt's own texts (key names, its dialogs) stay as Qt has them.
        const bool ours = !context || std::strncmp(context, "bemotion", 8) == 0 || std::strcmp(context, "QObject") == 0;
        if (!source || !ours) return {};
        const auto it = strings_.constFind(QByteArray(source));
        if (it == strings_.constEnd()) return {};
        return n >= 0 ? QString(*it).replace(QStringLiteral("%n"), QString::number(n)) : *it;
    }
    bool isEmpty() const override { return strings_.isEmpty(); }

private:
    QHash<QByteArray, QString> strings_;
};

std::string g_language = "en";

bool readLanguage(const std::filesystem::path& file, LanguageFile& out) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    std::stringstream text;
    text << in.rdbuf();
    std::string error;
    if (parseLanguage(text.str(), out, &error)) return true;
    platform::log("language file " + platform::toUtf8(file) + ": " + error);
    return false;
}

} // namespace

QString tr(const char* english) { return QCoreApplication::translate("bemotion", english); }
QString trs(const std::string& english) {
    const QString t = QCoreApplication::translate("bemotion", english.c_str());
    // A transition played as an animation ("Slide Left In", core/Transitions.h): its name, then in / out.
    if (t == QString::fromStdString(english))
        for (const auto& [suffix, pattern] : {std::pair{" In", TR_NOOP("%1 · in")}, std::pair{" Out", TR_NOOP("%1 · out")}})
            if (english.size() > std::strlen(suffix) && english.compare(english.size() - std::strlen(suffix), std::string::npos, suffix) == 0) {
                const std::string base = english.substr(0, english.size() - std::strlen(suffix));
                const QString b = QCoreApplication::translate("bemotion", base.c_str());
                if (b != QString::fromStdString(base)) return tr(pattern).arg(b);
            }
    return t;
}

QString trLabel(const QString& text) {
    const QByteArray utf8 = text.toUtf8();
    const QString t = QCoreApplication::translate("bemotion", utf8.constData());
    if (t != text) return t;
    static const QRegularExpression numbered(QStringLiteral("^(.+) (\\d+)$"));
    const QRegularExpressionMatch m = numbered.match(text);
    if (!m.hasMatch()) return text;
    return QCoreApplication::translate("bemotion", m.captured(1).toUtf8().constData()) + ' ' + m.captured(2);
}

namespace ui {

std::vector<LanguageInfo> availableLanguages() {
    std::vector<LanguageInfo> out;
    for (const auto& dir : {platform::assetsDir() / "languages", platform::appDataDir() / "languages"}) {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (entry.path().extension() != ".json") continue;
            LanguageFile file;
            if (!readLanguage(entry.path(), file)) continue;
            // The user's file of a bundled language replaces it (a fixed or newer translation).
            std::erase_if(out, [&](const LanguageInfo& l) { return l.code == file.code; });
            out.push_back({file.code, file.name, entry.path()});
        }
    }
    std::sort(out.begin(), out.end(), [](const LanguageInfo& a, const LanguageInfo& b) { return a.name < b.name; });
    return out;
}

void installLanguage(const std::string& setting) {
    g_language = "en";
    static JsonTranslator* translator = nullptr; // the one installed; replaced on a change
    if (translator) {
        QCoreApplication::removeTranslator(translator);
        delete translator;
        translator = nullptr;
    }
    if (setting == "en") return;
    const std::vector<LanguageInfo> languages = availableLanguages();
    std::vector<std::string> codes;
    for (const LanguageInfo& l : languages) codes.push_back(l.code);
    const int i = matchLanguage(codes, setting == "auto" || setting.empty() ? QLocale::system().name().toStdString() : setting);
    if (i < 0) return;
    LanguageFile file;
    if (!readLanguage(languages[i].file, file)) return;
    translator = new JsonTranslator(file);
    QCoreApplication::installTranslator(translator);
    g_language = file.code;
    platform::log("language: " + file.code);
}

std::string currentLanguage() { return g_language; }

} // namespace ui
} // namespace bemotion
