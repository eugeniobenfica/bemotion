#include "ui/ExpressionEditor.h"

#include "ui/EditorContext.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCompleter>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStringListModel>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace bemotion::ui {

namespace {

// The language's own words (core/Expressions.cpp, docs/EXPRESSIONS.md).
const QStringList kFunctions = {"sin", "cos", "tan", "asin", "acos", "atan", "atan2", "degrees", "radians", "abs", "floor",
                                "ceil", "round", "fract", "sign", "sqrt", "pow", "exp", "log", "min", "max", "clamp", "lerp",
                                "mix", "map", "smooth", "ease", "length", "distance", "wiggle", "noise", "random", "loop",
                                "pingpong", "vec", "rgb", "rgba", "layer"};
const QStringList kNames = {"time", "frame", "fps", "value", "start", "end", "duration", "progress", "index", "width",
                            "height", "center", "pi"};
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

    void fitHeight() {
        const int lines = std::clamp(document()->blockCount(), 1, 10);
        int visual = 0;
        for (QTextBlock b = document()->begin(); b.isValid(); b = b.next()) visual += std::max(1, b.layout() ? b.layout()->lineCount() : 1);
        const int shown = std::clamp(std::max(lines, visual), 1, 10);
        setFixedHeight(fontMetrics().lineSpacing() * shown + static_cast<int>(document()->documentMargin() * 2) + frameWidth() * 2 + 6);
    }

protected:
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
    auto* row = new QHBoxLayout;
    row->setSpacing(4);
    text_ = new ExpressionText(ctx, layerId, this);
    text_->setPlaceholderText(vector ? "= an expression, e.g. wiggle(2, 25)" : "= an expression, e.g. time * 90");
    text_->setToolTip("Worked out every frame. Several lines: name = ... first, the result last.\n"
                      "Names complete as you type; ƒ inserts functions and other layers' properties. Esc: done.");
    connect(text_, &QPlainTextEdit::textChanged, this, [this] { emit edited(text()); });
    row->addWidget(text_, 1);
    auto* fx = new QToolButton;
    fx->setText(QString::fromUtf8("ƒ"));
    fx->setToolTip("Insert a function, a value of this clip, or a property of another layer");
    fx->setFixedSize(26, 26);
    connect(fx, &QToolButton::clicked, this, [this] { showMenu(); });
    row->addWidget(fx, 0, Qt::AlignTop);
    col->addLayout(row);
    problem_ = new QLabel;
    problem_->setWordWrap(true);
    ui::setTone(problem_, ui::Tone::Danger);
    problem_->hide();
    col->addWidget(problem_);
}

QString ExpressionEditor::text() const { return text_->toPlainText(); }

void ExpressionEditor::setText(const QString& text) {
    if (text == text_->toPlainText()) return;
    const QSignalBlocker b(text_);
    text_->setPlainText(text);
    text_->moveCursor(QTextCursor::Start);
    text_->fitHeight();
}

bool ExpressionEditor::editing() const { return text_->hasFocus(); }

void ExpressionEditor::focusText() {
    text_->setFocus();
    text_->moveCursor(QTextCursor::End);
}

void ExpressionEditor::setProblem(const QString& problem) {
    problem_->setText(problem);
    problem_->setVisible(!problem.isEmpty());
}

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

void ExpressionEditor::showMenu() {
    QMenu menu;
    menu.setToolTipsVisible(true);
    auto add = [this](QMenu* m, const QString& label, const QString& snippet, const QString& tip = {}) {
        QAction* a = m->addAction(label, this, [this, snippet] { insert(snippet); });
        if (!tip.isEmpty()) a->setToolTip(tip);
    };
    QMenu* clip = menu.addMenu("This Clip");
    add(clip, "time", "time", "Seconds, now");
    add(clip, "value", "value", "What the keyframes (or the static value) say now");
    add(clip, "progress", "progress", "0 at the clip's start, 1 at its end");
    add(clip, "start / end", "start", "The clip's in and out, seconds");
    add(clip, "duration", "duration");
    add(clip, "index", "index", "The layer's row, 1 at the top");
    add(clip, "frame", "frame");
    QMenu* motion = menu.addMenu("Animation");
    add(motion, "wiggle(times a second, amount)", vector_ ? "wiggle(2, 25|)" : "wiggle(2, 10|)", "The value, shaken smoothly");
    add(motion, "loop()", "loop()", "The keyframes again and again");
    add(motion, "pingpong()", "pingpong()", "The keyframes back and forth");
    add(motion, "noise(x)", "noise(time|)", "Smooth noise, -1 to 1");
    add(motion, "random(min, max)", "random(0, 100|)", "A fixed random number for this property");
    add(motion, "lerp(a, b, t)", "lerp(|value, value * 2, progress)", "From a to b as t goes 0 to 1");
    add(motion, "smooth(t)", "smooth(progress|)", "0 to 1, eased in and out");
    QMenu* math = menu.addMenu("Math");
    for (const auto& [label, snippet] : std::initializer_list<std::pair<const char*, const char*>>{
             {"sin(x)", "sin(time|)"}, {"cos(x)", "cos(time|)"}, {"abs(x)", "abs(|)"}, {"round(x)", "round(|)"},
             {"floor(x)", "floor(|)"}, {"min(a, b)", "min(|, )"}, {"max(a, b)", "max(|, )"}, {"clamp(v, lo, hi)", "clamp(|value, 0, 100)"},
             {"map(x, a, b, lo, hi)", "map(|time, 0, 1, 0, 100)"}, {"pow(a, b)", "pow(|, 2)"}, {"sqrt(x)", "sqrt(|)"},
             {"length(v)", "length(|)"}, {"distance(a, b)", "distance(|, )"}, {"pi", "pi"}})
        add(math, label, snippet);
    QMenu* values = menu.addMenu("Values");
    add(values, "[x, y]", "[|, ]", "A vector: positions, scales, sizes");
    add(values, "rgb(r, g, b)", "rgb(232, 185, 49|)", "A color, 0 to 255");
    add(values, "width / height", "width", "The composition's size");
    add(values, "center", "center", "The middle of the frame");
    add(values, "condition ? a : b", "time < 1 ? 0 : 100|", "A choice");
    add(values, "name = ...", "var |name = value\n", "A line that names a value (the last line is the result)");
    // Other layers' properties: what After Effects' pick whip does, by name.
    QMenu* layers = menu.addMenu("Layers");
    const Composition& comp = ctx_.comp();
    for (const Layer& l : comp.layers) {
        if (l.type == LayerType::Camera && l.id == layerId_) continue;
        const QString name = QString::fromStdString(l.name);
        QMenu* sub = layers->addMenu(l.id == layerId_ ? name + "  (this one)" : name);
        const QString ref = l.id == layerId_ ? QString("layer()") : QString("layer(\"%1\")").arg(name);
        std::string group;
        for (const PropertyRef& pr : const_cast<Layer&>(l).properties()) {
            if (pr.effectIndex >= 0 || (pr.whileAnimated && !pr.anim->isAnimated())) continue;
            if (pr.group != group) {
                if (!group.empty()) sub->addSeparator();
                group = pr.group;
            }
            add(sub, QString::fromUtf8(pr.label), ref + "." + QString::fromUtf8(pr.id),
                QString("%1.%2 (add (time - 0.2) to read it a moment earlier)").arg(ref, QString::fromUtf8(pr.id)));
        }
        sub->addSeparator();
        add(sub, "Clip Start", ref + ".start");
        add(sub, "Clip End", ref + ".end");
    }
    if (comp.layers.empty()) layers->addAction("No layers")->setEnabled(false);
    menu.exec(QCursor::pos());
}

} // namespace bemotion::ui
