#include "ui/ExperienceScreen.h"

#include "app/PluginHost.h"
#include "app/Settings.h"
#include "ui/AppSignals.h"
#include "ui/Draw.h"
#include "ui/PagedWindow.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QAbstractButton>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace bemotion::ui {

namespace {

// One of the three choices: an icon, its name, a line, what it means.
class ExperienceCard : public QAbstractButton {
public:
    ExperienceCard(Icon icon, QString name, QString line, QStringList points)
        : icon_(icon), name_(trLabel(name)), line_(trLabel(line)), points_(std::move(points)) {
        for (QString& p : points_) p = trLabel(p);
        setCheckable(true);
        setAutoExclusive(true);
        setCursor(Qt::PointingHandCursor);
        setFixedSize(250, 236);
        setFocusPolicy(Qt::StrongFocus);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        Draw d(p);
        const float r = theme::current().rounding + 4.0f;
        const bool on = isChecked(), hot = underMouse();
        const Pt a(1.0f, 1.0f), b(width() - 1.0f, height() - 1.0f);
        d.rectFilled(a, b, on ? theme::withAlpha(theme::kAccent, 26) : hot ? theme::kBgHover : theme::kBgRaised, r);
        d.rect(a, b, on ? theme::kAccent : hasFocus() ? theme::kFocus : theme::kBorder, r, on ? 2.0f : 1.0f);
        // The icon on a disc, then the words.
        d.circleFilled(Pt(38, 40), 20, on ? theme::kAccent : theme::kBgHover);
        drawIcon(d, icon_, Pt(38, 40), 18, on ? theme::kOnAccent : theme::kText);
        if (on) { // a check in the corner
            d.circleFilled(Pt(width() - 26.0f, 26), 10, theme::kAccent);
            d.line(Pt(width() - 31.0f, 26), Pt(width() - 27.0f, 30), theme::kOnAccent, 2.0f);
            d.line(Pt(width() - 27.0f, 30), Pt(width() - 21.0f, 22), theme::kOnAccent, 2.0f);
        }
        QFont f = font();
        QFont title = f;
        title.setBold(true);
        title.setPointSizeF(f.pointSizeF() * 1.3);
        p.setFont(title);
        d.text(Pt(18, 74), theme::kText, name_);
        const float lineTop = 74 + d.textHeight() + 8;
        p.setFont(f);
        p.setPen(qcolor(theme::kTextDim));
        const QRectF lineBox(18, lineTop, width() - 36, 60);
        p.drawText(lineBox, Qt::TextWordWrap, line_);
        float y = lineTop + p.fontMetrics().boundingRect(lineBox.toRect(), Qt::TextWordWrap, line_).height() + 14;
        for (const QString& point : points_) {
            d.circleFilled(Pt(23, y + d.textHeight() * 0.5f), 2.0f, on ? theme::kAccent : theme::kTextFaint);
            p.setPen(qcolor(theme::kTextDim));
            const QRectF box(32, y, width() - 50, 36);
            p.drawText(box, Qt::TextWordWrap, point);
            y += p.fontMetrics().boundingRect(box.toRect(), Qt::TextWordWrap, point).height() + 6;
        }
    }
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }

private:
    Icon icon_;
    QString name_, line_;
    QStringList points_;
};

} // namespace

ExperienceScreen::ExperienceScreen(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_StyledBackground, false);
    setFocusPolicy(Qt::StrongFocus);
    auto* outer = new QVBoxLayout(this);
    outer->addStretch(2);
    auto* center = new QWidget;
    center->setMaximumWidth(830);
    auto* col = new QVBoxLayout(center);
    col->setContentsMargins(24, 0, 24, 0);
    col->setSpacing(10);
    auto* title = new QLabel(tr("Welcome to bemotion"));
    QFont tf = title->font();
    tf.setBold(true);
    tf.setPointSizeF(tf.pointSizeF() * 2.0);
    title->setFont(tf);
    title->setAlignment(Qt::AlignHCenter);
    col->addWidget(title);
    auto* about = new QLabel(tr("How much do you want to see? You can change it any time in Settings › Interface."));
    setTone(about, Tone::TextDim);
    about->setAlignment(Qt::AlignHCenter);
    col->addWidget(about);
    col->addSpacing(18);

    auto* cards = new QHBoxLayout;
    cards->setSpacing(16);
    cards->addStretch();
    struct Choice {
        const char* id;
        Icon icon;
        const char* name;
        const char* line;
        QStringList points;
    };
    const Choice choices[] = {
        {"friendly", Icon::Star, TR_NOOP("Friendly"), TR_NOOP("The essentials, nothing in the way."), {TR_NOOP("A short toolbar"), TR_NOOP("No expressions or curves to deal with"), TR_NOOP("Simple settings")}},
        {"balanced", Icon::Sliders, TR_NOOP("Balanced"), TR_NOOP("bemotion picks for you: the usual tools, the rest when you need it."),
         {TR_NOOP("The usual toolbar"), TR_NOOP("Audio meters"), TR_NOOP("Everything else a click away")}},
        {"expert", Icon::Graph, TR_NOOP("Expert"), TR_NOOP("Everything at hand."), {TR_NOOP("Every option in Settings"), TR_NOOP("Rulers, meters, the graph editor"), TR_NOOP("A fuller toolbar")}},
    };
    for (int i = 0; i < 3; ++i) {
        auto* card = new ExperienceCard(choices[i].icon, choices[i].name, choices[i].line, choices[i].points);
        const std::string id = choices[i].id;
        connect(card, &QAbstractButton::clicked, this, [this, id] { choose(id); });
        cards_[i] = card;
        cards->addWidget(card);
    }
    cards->addStretch();
    col->addLayout(cards);
    col->addSpacing(14);

    auto* options = new SettingsCard;
    essentials_ = new Switch;
    options->addRow(tr("Essentials only"), tr("Plugins off: no charts, maps, 3D models or voice recording, for the simplest bemotion. "
                                       "They come back in Settings › Plugins."),
                    essentials_);
    col->addWidget(options);
    col->addSpacing(14);
    auto* row = new QHBoxLayout;
    row->addStretch();
    auto* start = accentButton(tr("Start"));
    start->setMinimumWidth(140);
    start->setDefault(true);
    connect(start, &QPushButton::clicked, this, [this] { apply(); });
    row->addWidget(start);
    col->addLayout(row);

    auto* centerRow = new QHBoxLayout;
    centerRow->addStretch();
    centerRow->addWidget(center, 1);
    centerRow->addStretch();
    outer->addLayout(centerRow);
    outer->addStretch(3);
    choose("balanced");
}

void ExperienceScreen::choose(const std::string& experience) {
    choice_ = experience;
    const int i = experience == "friendly" ? 0 : experience == "expert" ? 2 : 1;
    cards_[i]->setChecked(true);
    for (QAbstractButton* c : cards_) c->update();
}

void ExperienceScreen::apply() {
    applyExperience(choice_);
    Settings& s = settings();
    s.essentialsOnly = essentials_->isChecked();
    s.firstRun = false;
    saveSettings();
    if (s.essentialsOnly) PluginHost::get().requestReload();
    emit AppSignals::get().toolbarChanged();
    emit AppSignals::get().pluginsChanged();
    emit AppSignals::get().appearanceChanged();
    hide();
    emit finished();
}

void ExperienceScreen::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), qcolor(theme::kBgDeep));
}

void ExperienceScreen::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) apply();
    else if (e->key() == Qt::Key_Left || e->key() == Qt::Key_Right) {
        const int i = choice_ == "friendly" ? 0 : choice_ == "expert" ? 2 : 1;
        const int next = std::clamp(i + (e->key() == Qt::Key_Right ? 1 : -1), 0, 2);
        choose(next == 0 ? "friendly" : next == 2 ? "expert" : "balanced");
    } else QWidget::keyPressEvent(e);
}

} // namespace bemotion::ui
