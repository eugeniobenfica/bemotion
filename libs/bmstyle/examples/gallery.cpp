// bmstyle's gallery: every control in one window, and a theme switch. Also the shortest example
// of an app using the library (build with -DBMSTYLE_BUILD_EXAMPLES=ON).

#include "bmstyle/BmStyle.h"

#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QPainter>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {

using namespace bmstyle;

// A custom-drawn widget: bmstyle::Draw and the theme's tokens, read when painting.
class Swatches : public QWidget {
public:
    Swatches() {
        setMinimumHeight(56);
        connect(&Appearance::get(), &Appearance::changed, this, [this] { update(); });
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        Draw d(p);
        const Rgba colors[] = {theme::kBgDeep, theme::kBgPanel, theme::kBgRaised, theme::kField, theme::kAccent, theme::kDanger, theme::kText};
        float x = 0;
        for (Rgba c : colors) {
            d.rectFilled({x, 4}, {x + 40, 48}, c, theme::current().rounding);
            d.rect({x + 0.5f, 4.5f}, {x + 39.5f, 47.5f}, theme::kBorder, theme::current().rounding);
            x += 48;
        }
        drawIcon(d, Icon::Star, {x + 24, 26}, 28, theme::kAccent);
    }
};

QWidget* section(const QString& title, Icon icon, QWidget* content) {
    auto* box = new QWidget;
    auto* col = new QVBoxLayout(box);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);
    auto* body = new CategoryBody(content);
    auto* head = new CategoryHeader(icon, title, -1, false);
    head->toggled = [body] { body->setVisible(!body->isVisible()); };
    col->addWidget(head);
    col->addWidget(body);
    return box;
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    install(theme::darkPalette());

    QWidget window;
    window.setWindowTitle("bmstyle gallery");
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* page = new QWidget;
    auto* col = new QVBoxLayout(page);
    col->setContentsMargins(metrics::kPanelMargin * 2, metrics::kPanelMargin * 2, metrics::kPanelMargin * 2, metrics::kPanelMargin * 2);
    col->setSpacing(metrics::kGroupGap);

    // The theme, switched while the app runs.
    col->addWidget(sectionLabel("Theme"));
    col->addWidget(segmented({"Dark", "Light", "Midnight"}, 0, [](int i) {
        theme::apply(i == 0 ? theme::darkPalette() : i == 1 ? theme::lightPalette() : theme::midnightPalette());
    }));
    col->addWidget(new Swatches);

    // Buttons.
    auto* buttons = new QWidget;
    auto* flow = new FlowLayout(buttons);
    flow->setSpacing(metrics::kGap * 2);
    flow->addWidget(accentButton("Export"));
    flow->addWidget(new QPushButton("Cancel"));
    for (Icon icon : {Icon::Play, Icon::Pause, Icon::Loop, Icon::Magnet, Icon::Trash}) flow->addWidget(new IconButton(icon, "An icon button"));
    auto* toggle = new IconButton(Icon::Eye, "Active");
    toggle->setActive(true);
    flow->addWidget(toggle);
    col->addWidget(section("Buttons", Icon::Pointer, buttons));

    // Fields.
    auto* fields = new QWidget;
    auto* fcol = new QVBoxLayout(fields);
    fcol->setContentsMargins(0, 0, 0, 0);
    fcol->setSpacing(metrics::kGap);
    auto* opacity = new DragValue;
    opacity->setRange(0, 100);
    opacity->setFormat("%.0f%%");
    opacity->setValue(80);
    fcol->addWidget(new LabeledRow("Opacity (drag or click)", opacity));
    fcol->addWidget(new LabeledRow("Snapping", new Switch));
    auto* combo = new QComboBox;
    combo->addItems({"Linear", "Easy Ease", "Hold"});
    fcol->addWidget(new LabeledRow("Easing", combo));
    fcol->addWidget(new LabeledRow("Name", new QLineEdit("Title")));
    auto* faint = new ElidedLabel("A long path that shortens itself when there is no room: C:/Users/someone/Videos/project/footage/clip.mov",
                                  Qt::ElideMiddle);
    setTone(faint, Tone::TextFaint);
    fcol->addWidget(faint);
    col->addWidget(section("Fields", Icon::Sliders, fields));

    // Tiles that reflow.
    auto* grid = new ResponsiveGrid(84);
    for (auto [icon, name] : {std::pair{Icon::Text, "Text"}, {Icon::Rectangle, "Rectangle"}, {Icon::Ellipse, "Ellipse"}, {Icon::Star, "Star"},
                              {Icon::Camera, "Camera"}, {Icon::Film, "Video"}, {Icon::Audio, "Audio"}, {Icon::Cube, "3D Model"}})
        grid->addWidget(new Tile(icon, name));
    col->addWidget(section("Tiles", Icon::Duplicate, grid));
    for (auto [icon, name] : {std::pair{Icon::FxBlur, "Gaussian Blur"}, {Icon::FxColor, "Curves"}, {Icon::FxDistort, "Wave Warp"}})
        col->addWidget(new ChoiceRow(icon, name));
    col->addStretch();

    scroll->setWidget(page);
    auto* outer = new QVBoxLayout(&window);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);
    window.resize(560, 720);
    window.show();
    return QApplication::exec();
}
