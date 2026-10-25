#pragma once

#include <QWidget>

#include <string>

class QAbstractButton;

namespace bemotion::ui {

class Switch;

// Shown over the whole window on bemotion's first start: how much the interface shows
// (Friendly, Balanced, Expert: app/Settings applyExperience) and whether plugins load at all
// (Essentials only). Everything can be changed later in Settings › Interface. finished()
// fires once the choice is applied.
class ExperienceScreen : public QWidget {
    Q_OBJECT
public:
    explicit ExperienceScreen(QWidget* parent = nullptr);

signals:
    void finished();

protected:
    void paintEvent(QPaintEvent*) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    void choose(const std::string& experience);
    void apply();
    std::string choice_ = "balanced";
    QAbstractButton* cards_[3] = {};
    Switch* essentials_ = nullptr;
};

} // namespace bemotion::ui
