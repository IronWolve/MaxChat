#include "ui/ScriptTerminalDialog.h"

#include "ui/AnsiRenderer.h"
#include "ui/TerminalFrame.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMenu>
#include <QKeyEvent>
#include <QScrollBar>
#include <QTextBrowser>
#include <QTextCursor>
#include <QUrl>
#include <QVBoxLayout>

#include <array>
#include <algorithm>

namespace maxchat::ui {

namespace {

constexpr qsizetype PasteGuardBytes = 2048;
constexpr int PasteGuardLines = 20;

class TerminalInputLineEdit final : public QLineEdit {
  public:
    explicit TerminalInputLineEdit(QWidget* parent = nullptr) : QLineEdit(parent) {}

  protected:
    void keyPressEvent(QKeyEvent* event) override {
        if (event != nullptr && event->matches(QKeySequence::Paste)) {
            pasteGuarded();
            event->accept();
            return;
        }
        QLineEdit::keyPressEvent(event);
    }

    void contextMenuEvent(QContextMenuEvent* event) override {
        QMenu menu(this);
        menu.addAction(QStringLiteral("Cut"), this, &QLineEdit::cut)->setEnabled(hasSelectedText());
        menu.addAction(QStringLiteral("Copy"), this, &QLineEdit::copy)->setEnabled(hasSelectedText());
        menu.addAction(QStringLiteral("Paste"), this, [this]() { pasteGuarded(); });
        menu.addSeparator();
        menu.addAction(QStringLiteral("Select All"), this, &QLineEdit::selectAll);
        menu.exec(event->globalPos());
    }

  private:
    void pasteGuarded() {
        const QString text = QApplication::clipboard()->text();
        const bool large =
            text.toUtf8().size() > PasteGuardBytes ||
            text.count(QLatin1Char('\n')) + text.count(QLatin1Char('\r')) >= PasteGuardLines;
        if (large) {
            const QMessageBox::StandardButton answer = QMessageBox::question(
                this, QStringLiteral("Large Paste"),
                QStringLiteral("Paste %1 characters into this terminal input?")
                    .arg(text.size()),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer != QMessageBox::Yes) {
                return;
            }
        }
        insert(text);
    }
};

QString colorCss(const QColor& color) {
    return color.isValid() ? color.name(QColor::HexRgb) : QStringLiteral("transparent");
}

QString frameColor(const int index, const bool background) {
    static const std::array<const char*, 16> colors = {
        "#000000", "#0000aa", "#00aa00", "#00aaaa", "#aa0000", "#aa00aa",
        "#aa5500", "#aaaaaa", "#555555", "#5555ff", "#55ff55", "#55ffff",
        "#ff5555", "#ff55ff", "#ffff55", "#ffffff",
    };
    if (index >= 0 && index < static_cast<int>(colors.size())) {
        return QString::fromLatin1(colors.at(static_cast<size_t>(index)));
    }
    return background ? QStringLiteral("#000000") : QStringLiteral("#aaaaaa");
}

QString htmlEscapedChar(const QChar ch) {
    QString s(ch);
    return s.toHtmlEscaped();
}

} // namespace

ScriptTerminalDialog::ScriptTerminalDialog(QString id, QString title, TerminalProfile profile,
                                           QWidget* parent)
    : QDialog(parent), id_(std::move(id)), profile_(std::move(profile)) {
    setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                   Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint |
                   Qt::WindowCloseButtonHint);
    setWindowTitle(title.isEmpty() ? id_ : title);
    setAttribute(Qt::WA_DeleteOnClose);
    resize(900, 600);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(4);

    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("terminalStatus"));
    status_->setText(QStringLiteral("DISCONNECTED"));
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(status_);

    display_ = new QTextBrowser(this);
    display_->setObjectName(QStringLiteral("scriptTerminalDisplay"));
    display_->setReadOnly(true);
    display_->setOpenExternalLinks(false);
    display_->setOpenLinks(false);
    display_->setUndoRedoEnabled(false);
    connect(display_, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
        if (url.scheme() == QLatin1String("mc-term")) {
            QString path = url.path();
            if (path.startsWith(QLatin1Char('/'))) {
                path.remove(0, 1);
            }
            emit linkActivated(id_, QUrl::fromPercentEncoding(path.toUtf8()));
        }
    });
    root->addWidget(display_, 1);

    auto* inputRow = new QWidget(this);
    auto* inputLayout = new QHBoxLayout(inputRow);
    inputLayout->setContentsMargins(0, 0, 0, 0);
    inputLayout->setSpacing(6);
    prompt_ = new QLabel(QStringLiteral(">"), inputRow);
    prompt_->setObjectName(QStringLiteral("terminalPrompt"));
    inputLayout->addWidget(prompt_);
    input_ = new TerminalInputLineEdit(inputRow);
    input_->setObjectName(QStringLiteral("terminalInput"));
    inputLayout->addWidget(input_, 1);
    root->addWidget(inputRow);

    connect(input_, &QLineEdit::returnPressed, this, [this]() {
        const QString text = input_->text();
        input_->clear();
        emit inputSubmitted(id_, text);
    });

    applyProfile();
}

QSize ScriptTerminalDialog::terminalSize() const {
    if (profile_.fixedGrid) {
        return QSize(profile_.cols, profile_.rows);
    }
    const QFontMetrics metrics(display_->font());
    const int cols = std::max(1, display_->viewport()->width() /
                                     std::max(1, metrics.horizontalAdvance(QLatin1Char('M'))));
    const int rows = std::max(1, display_->viewport()->height() /
                                     std::max(1, metrics.lineSpacing()));
    return QSize(cols, rows);
}

void ScriptTerminalDialog::writeText(const QString& text) {
    if (frameMode_) {
        frameGrid_.clear();
        frameMode_ = false;
        display_->clear();
    }
    QTextCursor cursor = display_->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertHtml(AnsiRenderer::toHtml(text));
    display_->setTextCursor(cursor);
    display_->verticalScrollBar()->setValue(display_->verticalScrollBar()->maximum());
}

void ScriptTerminalDialog::clear() {
    frameGrid_.clear();
    frameCursorRow_ = 0;
    frameCursorCol_ = 0;
    frameFg_ = 7;
    frameBg_ = 0;
    frameMode_ = false;
    display_->clear();
}

bool ScriptTerminalDialog::applyFrame(const QString& ops, QString* error) {
    QVector<TerminalFrame::Op> parsed;
    if (!TerminalFrame::parse(ops, &parsed, error)) {
        return false;
    }
    frameMode_ = true;
    ensureFrameGrid();
    for (const TerminalFrame::Op& op : parsed) {
        switch (op.type) {
        case TerminalFrame::OpType::Clear:
            for (auto& row : frameGrid_) {
                for (FrameCell& cell : row) {
                    cell = {};
                }
            }
            frameCursorRow_ = 0;
            frameCursorCol_ = 0;
            frameFg_ = 7;
            frameBg_ = 0;
            break;
        case TerminalFrame::OpType::Home:
            frameCursorRow_ = 0;
            frameCursorCol_ = 0;
            break;
        case TerminalFrame::OpType::Position:
            frameCursorRow_ =
                std::clamp(op.row - 1, 0, std::max(0, static_cast<int>(frameGrid_.size()) - 1));
            frameCursorCol_ =
                std::clamp(op.col - 1, 0,
                           std::max(0, static_cast<int>(frameGrid_.value(frameCursorRow_).size()) - 1));
            break;
        case TerminalFrame::OpType::Attribute:
            frameFg_ = std::clamp(op.fg, 0, 15);
            frameBg_ = std::clamp(op.bg, 0, 15);
            break;
        case TerminalFrame::OpType::Write:
        case TerminalFrame::OpType::ExtendedWrite:
            for (const QChar ch : op.text) {
                if (ch == QLatin1Char('\n')) {
                    ++frameCursorRow_;
                    frameCursorCol_ = 0;
                    continue;
                }
                if (frameCursorRow_ < 0 || frameCursorRow_ >= frameGrid_.size()) {
                    continue;
                }
                auto& row = frameGrid_[frameCursorRow_];
                if (frameCursorCol_ < 0 || frameCursorCol_ >= row.size()) {
                    continue;
                }
                row[frameCursorCol_] = {ch, frameFg_, frameBg_};
                ++frameCursorCol_;
                if (frameCursorCol_ >= row.size()) {
                    frameCursorCol_ = 0;
                    ++frameCursorRow_;
                }
            }
            break;
        case TerminalFrame::OpType::Newline:
            ++frameCursorRow_;
            frameCursorCol_ = 0;
            break;
        }
    }
    frameCursorRow_ =
        std::clamp(frameCursorRow_, 0, std::max(0, static_cast<int>(frameGrid_.size()) - 1));
    renderFrameGrid();
    return true;
}

void ScriptTerminalDialog::setStatusText(const QString& text) {
    status_->setText(text);
}

void ScriptTerminalDialog::setPromptText(const QString& text) {
    prompt_->setText(text.isEmpty() ? QStringLiteral(">") : text);
}

void ScriptTerminalDialog::setProfile(const TerminalProfile& profile) {
    profile_ = profile;
    applyProfile();
}

void ScriptTerminalDialog::setFitMode(const QString& mode) {
    profile_.fitMode = mode;
    updateFixedGridSize();
}

void ScriptTerminalDialog::setFontPreferences(const QString& family, const int pointSize,
                                              const bool bold) {
    fontFamilyOverride_ = family.trimmed();
    fontPointSizeOverride_ = pointSize;
    fontBoldOverride_ = bold;
    applyProfile();
}

void ScriptTerminalDialog::closeEvent(QCloseEvent* event) {
    emit terminalClosed(id_);
    QDialog::closeEvent(event);
}

void ScriptTerminalDialog::resizeEvent(QResizeEvent* event) {
    QDialog::resizeEvent(event);
    updateFixedGridSize();
}

void ScriptTerminalDialog::applyProfile() {
    const QString family =
        fontFamilyOverride_.isEmpty() ? profile_.fontFamily : fontFamilyOverride_;
    const int pointSize =
        fontPointSizeOverride_ > 0 ? fontPointSizeOverride_ : profile_.fontPointSize;
    QFont font(family);
    if (!QFontDatabase::families().contains(family)) {
        font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    }
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    font.setBold(fontBoldOverride_);
    font.setPointSize(pointSize);
    display_->setFont(font);
    input_->setFont(font);
    prompt_->setFont(font);
    status_->setFont(font);

    const QString css = QStringLiteral(
                            "QTextBrowser, QLineEdit, QLabel { color: %1; "
                            "background-color: %2; }")
                            .arg(colorCss(profile_.foreground),
                                 colorCss(profile_.background));
    display_->setStyleSheet(css);
    input_->setStyleSheet(css);
    prompt_->setStyleSheet(css);
    status_->setStyleSheet(css);
    updateFixedGridSize();
}

void ScriptTerminalDialog::updateFixedGridSize() {
    if (!profile_.fixedGrid || profile_.fitMode == QLatin1String("none")) {
        return;
    }
    const QFontMetrics metrics(display_->font());
    const int charWidth = std::max(1, metrics.horizontalAdvance(QLatin1Char('M')));
    const int lineHeight = std::max(1, metrics.lineSpacing());
    display_->setMinimumSize(profile_.cols * charWidth + 24, profile_.rows * lineHeight + 24);
}

void ScriptTerminalDialog::ensureFrameGrid() {
    const QSize size = terminalSize();
    const int rows = std::max(1, size.height());
    const int cols = std::max(1, size.width());
    if (frameGrid_.size() != rows || (!frameGrid_.isEmpty() && frameGrid_.first().size() != cols)) {
        frameGrid_.resize(rows);
        for (auto& row : frameGrid_) {
            row.resize(cols);
        }
    }
}

void ScriptTerminalDialog::renderFrameGrid() {
    QString html;
    html += QStringLiteral("<pre style=\"margin:0;white-space:pre;\">");
    int lastFg = -1;
    int lastBg = -1;
    bool spanOpen = false;
    const auto closeSpan = [&html, &spanOpen]() {
        if (spanOpen) {
            html += QStringLiteral("</span>");
            spanOpen = false;
        }
    };
    const auto openSpan = [&html, &spanOpen](const int fg, const int bg) {
        html += QStringLiteral("<span style=\"color:%1;background-color:%2;\">")
                    .arg(frameColor(fg, false), frameColor(bg, true));
        spanOpen = true;
    };
    for (int rowIndex = 0; rowIndex < frameGrid_.size(); ++rowIndex) {
        const auto& row = frameGrid_.at(rowIndex);
        for (const FrameCell& cell : row) {
            if (!spanOpen || cell.fg != lastFg || cell.bg != lastBg) {
                closeSpan();
                openSpan(cell.fg, cell.bg);
                lastFg = cell.fg;
                lastBg = cell.bg;
            }
            html += htmlEscapedChar(cell.ch);
        }
        if (rowIndex + 1 < frameGrid_.size()) {
            html += QLatin1Char('\n');
        }
    }
    closeSpan();
    html += QStringLiteral("</pre>");
    display_->setHtml(html);
    display_->verticalScrollBar()->setValue(0);
}

} // namespace maxchat::ui
