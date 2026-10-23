#pragma once

#include "core/Project.h"

#include <QElapsedTimer>
#include <QImage>
#include <QWidget>

#include <filesystem>

class QTimer;

namespace bemotion {

// The opening animation played when the app starts, before the home screen.
// It is an ordinary bemotion project (assets/welcome/Welcome.bemotion, also
// the Welcome project users can open and edit), drawn over the whole window.
// A click or any key skips to its ending; finished() fires once it has faded out.
class IntroScreen : public QWidget {
    Q_OBJECT
public:
    explicit IntroScreen(QWidget* parent = nullptr);
    bool start(const std::filesystem::path& project); // false when it cannot be loaded

signals:
    void finished();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override { skip(); }
    void keyPressEvent(QKeyEvent*) override { skip(); }

private:
    void step();
    void skip();

    Project project_;
    QTimer* timer_ = nullptr;
    QElapsedTimer clock_;
    QImage frame_;
    double time_ = 0.0;
    double reveal_ = 0.0; // after the animation: the home screen shows through
    bool active_ = false;
};

} // namespace bemotion
