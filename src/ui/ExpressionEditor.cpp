#include "ui/ExpressionEditor.h"

#include "core/Expressions.h"

#include "ui/EditorContext.h"
#include "ui/ExpressionLibrary.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCompleter>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPointer>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStringListModel>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <tuple>

namespace bemotion::ui {

namespace {

// The language's own words, from the core (core/Expressions.h, docs/EXPRESSIONS.md).
QStringList words(const std::vector<std::string>& list) {
    QStringList out;
    for (const std::string& w : list) out << QString::fromStdString(w);
    return out;
}
const QStringList kFunctions = words(expressionFunctions());
const QStringList kNames = words(expressionNames());
const QStringList kKeywords = {"var", "let", "const", "and", "or"};

class Highlighter final : public QSyntaxHighlighter {
public:
    using QSyntaxHighlighter::QSyntaxHighlighter;

protected:
    void highlightBlock(const QString& line) override {
        // Colors are read now: the theme can change while the app runs.
        auto paint = [&](const QRegularExpression& re, Rgba color, bool bold = false) {
            QTextCharFormat f;
            f.setForeground(theme::q(color));
            if (bold) f.setFontWeight(QFont::DemiBold);
            for (auto it = re.globalMatch(line); it.hasNext();) {
                const auto m = it.next();
                setFormat(static_cast<int>(m.capturedStart(1)), static_cast<int>(m.capturedLength(1)), f);
            }
        };
        static const QRegularExpression number(R"((?<![\w.])(\d+\.?\d*|\.\d+))");
        static const QRegularExpression call(R"(\b([A-Za-z_]\w*)\s*\()");
        static const QRegularExpression word(R"(\b([A-Za-z_]\w*)\b)");
        static const QRegularExpression text(R"((\"[^\"]*\"?|'[^']*'?))");
        static const QRegularExpression comment(R"((//.*)$)");
        paint(number, theme::kGuide);
        for (auto it = word.globalMatch(line); it.hasNext();) {
            const auto m = it.next();
            const QString w = m.captured(1);
            if (kNames.contains(w) || kKeywords.contains(w)) {
                QTextCharFormat f;
                f.setForeground(theme::q(theme::kAccent));
                if (kKeywords.contains(w)) f.setFontWeight(QFont::DemiBold);
                setFormat(static_cast<int>(m.capturedStart(1)), static_cast<int>(m.capturedLength(1)), f);
            }
        }
        paint(call, theme::kText, true);
        paint(text, theme::kSnap);
        paint(comment, theme::kTextFaint);
    }
};

} // namespace

// The text: grows with its lines, completes names as they are typed.
class ExpressionText final : public QPlainTextEdit {
public:
    ExpressionText(EditorContext& ctx, uint64_t layerId, QWidget* parent) : QPlainTextEdit(parent), ctx_(ctx), layerId_(layerId) {
        QFont mono("Consolas");
        mono.setStyleHint(QFont::Monospace);
        mono.setPointSizeF(QApplication::font().pointSizeF()); // as big as the rest of the panel
        setFont(mono);
        setTabChangesFocus(false);
        setLineWrapMode(QPlainTextEdit::WidgetWidth);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff); // it grows instead (the wheel still scrolls past 10 lines)
        new Highlighter(document());
        model_ = new QStringListModel(this);
        completer_ = new QCompleter(model_, this);
        completer_->setWidget(this);
        completer_->setCaseSensitivity(Qt::CaseInsensitive);
        completer_->setCompletionMode(QCompleter::PopupCompletion);
        connect(completer_, qOverload<const QString&>(&QCompleter::activated), this, [this](const QString& c) { complete(c); });
        connect(document(), &QTextDocument::contentsChanged, this, [this] { fitHeight(); });
        fitHeight();
    }

    std::function<void()> onEscape;

    void fitHeight() {
        const int lines = std::clamp(document()->blockCount(), 1, 10);
        int visual = 0;
        for (QTextBlock b = document()->begin(); b.isValid(); b = b.next()) visual += std::max(1, b.layout() ? b.layout()->lineCount() : 1);
        const int shown = std::clamp(std::max(lines, visual), 1, 10);
        setFixedHeight(fontMetrics().lineSpacing() * shown + static_cast<int>(document()->documentMargin() * 2) + frameWidth() * 2 + 6);
    }

protected:
    bool event(QEvent* e) override {
        // Esc belongs to the editor (it closes), not to the window's Deselect.
        if (e->type() == QEvent::ShortcutOverride && static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape) {
            e->accept();
            return true;
        }
        return QPlainTextEdit::event(e);
    }
    void keyPressEvent(QKeyEvent* e) override {
        if (completer_->popup()->isVisible()) {
            switch (e->key()) {
            case Qt::Key_Enter:
            case Qt::Key_Return:
            case Qt::Key_Tab:
            case Qt::Key_Escape:
            case Qt::Key_Backtab: e->ignore(); return; // the completer takes them
            default: break;
            }
        }
        if (e->key() == Qt::Key_Escape) { // done writing
            clearFocus();
            if (onEscape) onEscape();
            return;
        }
        if (e->key() == Qt::Key_Tab) {
            insertPlainText("  ");
            return;
        }
        QPlainTextEdit::keyPressEvent(e);
        const QString typed = e->text();
        if (typed.isEmpty() || (!typed.at(0).isLetterOrNumber() && typed != "_" && typed != ".")) {
            if (e->key() != Qt::Key_Backspace) completer_->popup()->hide();
            return;
        }
        offer();
    }
    void resizeEvent(QResizeEvent* e) override {
        QPlainTextEdit::resizeEvent(e);
        fitHeight();
    }

private:
    // The word being typed, and whether it follows a "." (a property of a layer).
    QString prefix(bool* member) const {
        QTextCursor c = textCursor();
        const QString line = c.block().text();
        int i = c.positionInBlock();
        int j = i;
        while (j > 0 && (line.at(j - 1).isLetterOrNumber() || line.at(j - 1) == '_')) --j;
        *member = j > 0 && line.at(j - 1) == '.';
        return line.mid(j, i - j);
    }
    void offer() {
        bool member = false;
        const QString p = prefix(&member);
        if (!member && p.size() < 2) {
            completer_->popup()->hide();
            return;
        }
        QStringList words;
        if (member) { // properties (of any layer: the one before the dot is not worked out here)
            words << "x" << "y" << "z" << "r" << "g" << "b" << "a" << "start" << "end";
            for (Layer& l : ctx_.comp().layers)
                for (const PropertyRef& pr : l.properties())
                    if (pr.effectIndex < 0 && !words.contains(QString::fromUtf8(pr.id))) words << QString::fromUtf8(pr.id);
        } else {
            for (const QString& f : kFunctions) words << f + "(";
            words << kNames << kKeywords;
            for (const Layer& l : ctx_.comp().layers)
                if (l.id != layerId_) words << QString("layer(\"%1\")").arg(QString::fromStdString(l.name));
        }
        words.removeDuplicates();
        model_->setStringList(words);
        completer_->setCompletionPrefix(p);
        if (completer_->completionCount() == 0 || (completer_->completionCount() == 1 && completer_->currentCompletion() == p)) {
            completer_->popup()->hide();
            return;
        }
        QRect r = cursorRect();
        r.setWidth(completer_->popup()->sizeHintForColumn(0) + completer_->popup()->verticalScrollBar()->sizeHint().width() + 16);
        completer_->complete(r);
    }
    void complete(const QString& completion) {
        bool member = false;
        const QString p = prefix(&member);
        QTextCursor c = textCursor();
        c.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, static_cast<int>(p.size()));
        if (completion.endsWith('(')) { // a function: its parentheses, the cursor inside
            c.insertText(completion + ")");
            c.movePosition(QTextCursor::Left);
        } else {
            c.insertText(completion);
        }
        setTextCursor(c);
    }

    EditorContext& ctx_;
    uint64_t layerId_;
    QStringListModel* model_;
    QCompleter* completer_;
};

ExpressionEditor::ExpressionEditor(EditorContext& ctx, uint64_t layerId, bool vector, QWidget* parent)
    : QWidget(parent), ctx_(ctx), layerId_(layerId), vector_(vector) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 4);
    col->setSpacing(2);
    text_ = new ExpressionText(ctx, layerId, this);
    text_->setPlaceholderText(vector ? "= an expression, e.g. wiggle(2, 25)" : "= an expression, e.g. time * 90");
    text_->setToolTip("Worked out every frame. Several lines: name = ... first, the result last.\n"
                      "Names complete as you type; ƒ inserts functions and other layers' properties. Esc: done.");
    connect(text_, &QPlainTextEdit::textChanged, this, [this] { emit edited(text()); });
    col->addWidget(text_);
    // Under the text: the problem (if it does not read), then insert, close and remove.
    auto* footer = new QHBoxLayout;
    footer->setSpacing(2);
    problem_ = new QLabel;
    problem_->setWordWrap(true);
    ui::setTone(problem_, ui::Tone::Danger);
    footer->addWidget(problem_, 1);
    auto* fx = new QToolButton;
    fx_ = fx;
    fx->setText(QString::fromUtf8("ƒ  Insert"));
    fx->setToolTip("The expression library: examples, functions, this clip's values, other layers' properties");
    fx->setFixedHeight(22);
    fx->setCursor(Qt::PointingHandCursor);
    connect(fx, &QToolButton::clicked, this, [this] { showMenu(); });
    footer->addWidget(fx, 0, Qt::AlignTop);
    // Close (the expression stays: the fx button by the diamond opens it again) and remove.
    rowButtons_ = new QWidget;
    auto* buttons = new QHBoxLayout(rowButtons_);
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(2);
    auto* close = new ui::IconButton(ui::Icon::ChevronUp, "Close (the expression stays; fx opens it again)  Esc", 22);
    connect(close, &QAbstractButton::clicked, this, [this] { emit closeRequested(); });
    buttons->addWidget(close);
    auto* remove = new ui::IconButton(ui::Icon::Trash, "Remove the expression", 22);
    connect(remove, &QAbstractButton::clicked, this, [this] { emit removeRequested(); });
    buttons->addWidget(remove);
    footer->addWidget(rowButtons_, 0, Qt::AlignTop);
    col->addLayout(footer);
    text_->onEscape = [this] { emit closeRequested(); };
}

QString ExpressionEditor::text() const { return text_->toPlainText(); }

void ExpressionEditor::setText(const QString& text) {
    if (text == text_->toPlainText()) return;
    const QSignalBlocker b(text_);
    text_->setPlainText(text);
    text_->moveCursor(QTextCursor::Start);
    text_->fitHeight();
}

void ExpressionEditor::setRowButtons(bool shown) {
    rowButtons_->setVisible(shown);
    text_->onEscape = shown ? std::function<void()>([this] { emit closeRequested(); }) : std::function<void()>();
}

bool ExpressionEditor::editing() const { return text_->hasFocus(); }

void ExpressionEditor::focusText() {
    text_->setFocus();
    text_->moveCursor(QTextCursor::End);
}

void ExpressionEditor::setProblem(const QString& problem) { problem_->setText(problem); }

void ExpressionEditor::insert(const QString& snippet) {
    const int caret = static_cast<int>(snippet.indexOf('|'));
    QString s = snippet;
    s.remove('|');
    QTextCursor c = text_->textCursor();
    c.insertText(s);
    if (caret >= 0) c.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, static_cast<int>(s.size()) - caret);
    text_->setTextCursor(c);
    text_->setFocus();
}

void ExpressionEditor::setProperty(const std::string& propId, bool color) {
    propId_ = propId;
    color_ = color;
}

std::vector<ExpressionEntry> expressionExamples(const std::string& propId, bool vector, bool color) {
    const QString id = QString::fromStdString(propId.empty() ? std::string("position") : propId);
    const char* cat = "Examples";
    return {
        {cat, vector ? "wiggle(2, 25)" : color ? "wiggle(1, 0.1)" : "wiggle(2, 10)", {}, "Shake gently: twice a second, a little"},
        {cat, vector ? "value + [time * 100, 0]" : "value + time * 90", {}, "Keep moving: a steady amount a second"},
        {cat, "loop()", {}, "Play the keyframes again and again"},
        {cat, "pingpong()", {}, "Play them back and forth"},
        {cat, "value * (1 + sin(time * 6) * 0.08)", {}, "Pulse: a little bigger, a little smaller"},
        {cat, QString("layer(\"Name\").%1").arg(id), QString("layer(\"|Name\").%1").arg(id), "Follow another layer (type its name)"},
        {cat, QString("layer(\"Name\").%1(time - 0.25)").arg(id), QString("layer(\"|Name\").%1(time - 0.25)").arg(id),
         "Follow it a quarter of a second later"},
        {cat, vector ? "lerp(value, value + [300, 0], smooth(progress))" : "lerp(value, value * 2, smooth(progress))", {},
         "Change smoothly over the whole clip"},
        {cat, "valueAt(time - 0.2)", {}, "Its own keyframes, a moment late"},
        {cat, "progress < 0.5 ? 0 : 100", {}, "A choice: one value, then another"},
    };
}

void ExpressionEditor::showMenu() {
    std::vector<ExpressionEntry> e = expressionExamples(propId_, vector_, color_);
    auto add = [&e](const char* category, const QString& code, const QString& snippet, const QString& about) {
        e.push_back({category, code, snippet, about});
    };
    const char* clip = "This Clip";
    add(clip, "time", {}, "Seconds, now");
    add(clip, "value", {}, "What the keyframes (or the static value) say now");
    add(clip, "progress", {}, "0 at the clip's start, 1 at its end");
    add(clip, "start", {}, "The clip's in point, seconds");
    add(clip, "end", {}, "The clip's out point, seconds");
    add(clip, "duration", {}, "The clip's length, seconds");
    add(clip, "velocity", {}, "How fast the keyframes change, per second");
    add(clip, "speed", {}, "The same, as one number");
    add(clip, "index", {}, "The layer's row, 1 at the top");
    add(clip, "frame", {}, "The frame number");
    add(clip, "fps", {}, "Frames per second");
    const char* motion = "Animation";
    add(motion, "wiggle(freq, amount)", vector_ ? "wiggle(2, 25|)" : "wiggle(2, 10|)", "The value, shaken smoothly");
    add(motion, "loop()", {}, "The keyframes again and again");
    add(motion, "pingpong()", {}, "The keyframes back and forth");
    add(motion, "valueAt(t)", "valueAt(time - 0.2|)", "This property's keyframes at another time");
    add(motion, "noise(x)", "noise(time|)", "Smooth noise, -1 to 1");
    add(motion, "random(min, max)", "random(0, 100|)", "A fixed random number for this property");
    add(motion, "lerp(a, b, t)", "lerp(|value, value * 2, progress)", "From a to b as t goes 0 to 1");
    add(motion, "smooth(t)", "smooth(progress|)", "0 to 1, eased in and out");
    add(motion, "map(x, a, b, lo, hi)", "map(|time, 0, 1, 0, 100)", "x from a to b onto lo to hi");
    const char* math = "Math";
    for (const auto& [code, snippet, about] : std::initializer_list<std::tuple<const char*, const char*, const char*>>{
             {"sin(x)", "sin(time|)", "A wave, -1 to 1 (radians)"}, {"cos(x)", "cos(time|)", "The same wave, a quarter turn ahead"},
             {"abs(x)", "abs(|)", "Without its sign"}, {"round(x)", "round(|)", "To the nearest whole number"},
             {"floor(x)", "floor(|)", "Down to a whole number"}, {"min(a, b)", "min(|, )", "The smaller"},
             {"max(a, b)", "max(|, )", "The larger"}, {"clamp(v, lo, hi)", "clamp(|value, 0, 100)", "Kept between two values"},
             {"pow(a, b)", "pow(|, 2)", "a to the power of b"}, {"sqrt(x)", "sqrt(|)", "Square root"},
             {"length(v)", "length(|)", "How long a vector is"}, {"distance(a, b)", "distance(|, )", "How far apart two points are"},
             {"degrees(r)", "degrees(|)", "Radians to degrees"}, {"pi", "pi", "3.14159..."}})
        add(math, code, snippet, about);
    const char* values = "Values";
    add(values, "[x, y]", "[|, ]", "A vector: positions, scales, sizes");
    add(values, "rgb(r, g, b)", "rgb(232, 185, 49|)", "A color, 0 to 255");
    add(values, "width, height", "width", "The composition's size");
    add(values, "center", {}, "The middle of the frame");
    add(values, "condition ? a : b", "time < 1 ? 0 : 100|", "A choice");
    add(values, "var name = value", "var |name = value\n", "A line that names a value (the last line is the result)");
    // Other layers' properties: what After Effects' pick whip does, by name.
    const char* layers = "Layers";
    const Composition& comp = ctx_.comp();
    for (const Layer& l : comp.layers) {
        if (l.type == LayerType::Camera && l.id == layerId_) continue;
        const QString name = QString::fromStdString(l.name);
        const QString ref = l.id == layerId_ ? QString("layer()") : QString("layer(\"%1\")").arg(name);
        e.push_back({layers, {}, {}, l.id == layerId_ ? name + "  (this one)" : name, true});
        for (const PropertyRef& pr : const_cast<Layer&>(l).properties()) {
            if (pr.effectIndex >= 0 || (pr.whileAnimated && !pr.anim->isAnimated())) continue;
            add(layers, ref + "." + QString::fromUtf8(pr.id), {}, QString::fromUtf8(pr.label) + " (add (time - 0.2) to read it a moment earlier)");
        }
        add(layers, ref + ".start", {}, "Its clip's in point");
    }
    QPointer<ExpressionEditor> self(this);
    showExpressionLibrary(fx_, std::move(e), [self](const QString& snippet) {
        if (self) self->insert(snippet);
    });
}

} // namespace bemotion::ui
