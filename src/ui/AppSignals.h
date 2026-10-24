#pragma once

// Application-wide notifications between windows that do not own each other
// (the settings window changes the keymap, the editor rebinds its shortcuts).

#include <QObject>

namespace bemotion::ui {

class AppSignals : public QObject {
    Q_OBJECT
public:
    static AppSignals& get() {
        static AppSignals s;
        return s;
    }

signals:
    void shortcutsChanged();  // keymap edited
    void appearanceChanged(); // theme, fonts, sizes
    void toolbarChanged();    // the timeline toolbar's tools
    void interfaceResetRequested(); // restore the editor's dock layout
    void pluginsChanged();    // enabled / installed plugins
};

} // namespace bemotion::ui
