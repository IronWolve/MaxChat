#include "ui/ScriptTerminalDialog.h"

#include "ui/AnsiRenderer.h"
#include "ui/TerminalFrame.h"

#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QFontDatabase>
#include <QFontDialog>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
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
    // Closing the window only HIDES the terminal; the session stays alive and is
    // re-shown from its tree node. Only File > Kill Terminal destroys it, so no
    // WA_DeleteOnClose here.

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(4);

    buildMenuBar();
    if (menuBar_ != nullptr) {
        root->setMenuBar(menuBar_);
    }

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

    // Let a fixed grid shrink so the fit-font can scale down; a hard grid-sized
    // minimum would block stretching the window smaller.
    display_->setMinimumSize(120, 80);
    applyProfile();
    resizeWindowForFont(fontPointSizeOverride_ > 0 ? fontPointSizeOverride_
                                                   : profile_.fontPointSize);
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
    applyFitFont();
}

void ScriptTerminalDialog::setGridSize(const int cols, const int rows) {
    profile_.cols = std::max(1, cols);
    profile_.rows = std::max(1, rows);
    profile_.fixedGrid = true;
    syncSettingsMenuChecks();
    resizeWindowForFont(fontPointSizeOverride_ > 0 ? fontPointSizeOverride_
                                                   : profile_.fontPointSize);
    applyFitFont();
    if (frameMode_) {
        ensureFrameGrid();
        renderFrameGrid();
    }
}

void ScriptTerminalDialog::setFontPreferences(const QString& family, const int pointSize,
                                              const bool bold) {
    fontFamilyOverride_ = family.trimmed();
    fontPointSizeOverride_ = pointSize;
    fontBoldOverride_ = bold;
    applyProfile();
}

void ScriptTerminalDialog::closeEvent(QCloseEvent* event) {
    // Hide, don't destroy: the terminal is re-shown from its tree node. The
    // owner only tears down the session on an explicit Kill (killRequested).
    hide();
    event->ignore();
}

void ScriptTerminalDialog::resizeEvent(QResizeEvent* event) {
    QDialog::resizeEvent(event);
    applyFitFont();
}

QString ScriptTerminalDialog::activeFamily() const {
    const QString family =
        fontFamilyOverride_.isEmpty() ? profile_.fontFamily : fontFamilyOverride_;
    return QFontDatabase::families().contains(family)
               ? family
               : QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
}

void ScriptTerminalDialog::applyProfile() {
    const int pointSize =
        fontPointSizeOverride_ > 0 ? fontPointSizeOverride_ : profile_.fontPointSize;
    QFont font(activeFamily());
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
    syncSettingsMenuChecks();
    applyFitFont();
}

int ScriptTerminalDialog::fitFontPointForViewport() const {
    const int availW = std::max(1, display_->viewport()->width());
    const int availH = std::max(1, display_->viewport()->height());
    const int cols = std::max(1, profile_.cols);
    const int rows = std::max(1, profile_.rows);
    QFont probe(activeFamily());
    probe.setStyleHint(QFont::Monospace);
    probe.setFixedPitch(true);
    probe.setBold(fontBoldOverride_);
    int lo = 4;
    int hi = 200;
    int best = lo;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        probe.setPointSize(mid);
        const QFontMetrics metrics(probe);
        const int w = std::max(1, metrics.horizontalAdvance(QLatin1Char('M'))) * cols;
        const int h = std::max(1, metrics.lineSpacing()) * rows;
        if (w <= availW && h <= availH) {
            best = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return best;
}

void ScriptTerminalDialog::applyFitFont() {
    if (!profile_.fixedGrid || profile_.fitMode == QLatin1String("none")) {
        return; // free profiles keep the configured point size
    }
    const int point = fitFontPointForViewport();
    QFont font = display_->font();
    if (font.pointSize() == point) {
        return;
    }
    font.setPointSize(point);
    display_->setFont(font);
    input_->setFont(font);
    prompt_->setFont(font);
    status_->setFont(font);
    if (frameMode_) {
        renderFrameGrid();
    }
}

void ScriptTerminalDialog::resizeWindowForFont(const int pointSize) {
    if (!profile_.fixedGrid) {
        return;
    }
    QFont probe(activeFamily());
    probe.setStyleHint(QFont::Monospace);
    probe.setFixedPitch(true);
    probe.setBold(fontBoldOverride_);
    probe.setPointSize(std::max(4, pointSize));
    const QFontMetrics metrics(probe);
    const int cellW = std::max(1, metrics.horizontalAdvance(QLatin1Char('M')));
    const int cellH = std::max(1, metrics.lineSpacing());
    // Grow the window so the display viewport holds the whole grid at this size;
    // the resize then re-fits the font to whatever the user drags it to.
    const int wantViewportW = profile_.cols * cellW;
    const int wantViewportH = profile_.rows * cellH;
    const int chromeW = width() - display_->viewport()->width();
    const int chromeH = height() - display_->viewport()->height();
    const int newW = wantViewportW + std::max(chromeW, 32);
    const int newH = wantViewportH + std::max(chromeH, 120);
    resize(newW, newH);
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

void ScriptTerminalDialog::buildMenuBar() {
    menuBar_ = new QMenuBar(this);

    QMenu* fileMenu = menuBar_->addMenu(QStringLiteral("File"));
    fileMenu->addAction(QStringLiteral("Close"), this, [this]() { hide(); });
    fileMenu->addSeparator();
    QAction* kill = fileMenu->addAction(QStringLiteral("Kill Terminal"));
    connect(kill, &QAction::triggered, this, [this]() { emit killRequested(id_); });

    QMenu* settingsMenu = menuBar_->addMenu(QStringLiteral("Settings"));

    // Font family — a short curated monospace list plus a full chooser.
    QMenu* fontMenu = settingsMenu->addMenu(QStringLiteral("Font"));
    const QStringList families = {QStringLiteral("JetBrains Mono"), QStringLiteral("Cascadia Mono"),
                                  QStringLiteral("Consolas"), QStringLiteral("Courier New"),
                                  QStringLiteral("DejaVu Sans Mono")};
    for (const QString& family : families) {
        QAction* act = fontMenu->addAction(family);
        connect(act, &QAction::triggered, this, [this, family]() {
            fontFamilyOverride_ = family;
            applyProfile();
            emit fontPreferenceChanged(family, fontPointSizeOverride_, fontBoldOverride_);
        });
    }
    fontMenu->addSeparator();
    QAction* chooseFont = fontMenu->addAction(QStringLiteral("Choose..."));
    connect(chooseFont, &QAction::triggered, this, [this]() {
        bool ok = false;
        const QFont chosen = QFontDialog::getFont(&ok, display_->font(), this,
                                                  QStringLiteral("Terminal Font"));
        if (ok) {
            fontFamilyOverride_ = chosen.family();
            fontBoldOverride_ = chosen.bold();
            applyProfile();
            emit fontPreferenceChanged(chosen.family(), fontPointSizeOverride_, fontBoldOverride_);
        }
    });

    // Font size — sets the base size (window re-zooms; resizing then re-fits).
    QMenu* sizeMenu = settingsMenu->addMenu(QStringLiteral("Font Size"));
    fontSizeGroup_ = new QActionGroup(this);
    fontSizeGroup_->setExclusive(true);
    for (const int pt : {8, 10, 12, 14, 16, 18, 20, 24}) {
        QAction* act = sizeMenu->addAction(QStringLiteral("%1 pt").arg(pt));
        act->setCheckable(true);
        act->setData(pt);
        fontSizeGroup_->addAction(act);
        connect(act, &QAction::triggered, this, [this, pt]() {
            fontPointSizeOverride_ = pt;
            applyProfile();
            resizeWindowForFont(pt);
            emit fontPreferenceChanged(activeFamily(), pt, fontBoldOverride_);
        });
    }

    // Terminal grid size — columns stay 80.
    QMenu* gridMenu = settingsMenu->addMenu(QStringLiteral("Terminal Size"));
    gridSizeGroup_ = new QActionGroup(this);
    gridSizeGroup_->setExclusive(true);
    const QVector<QPair<int, int>> grids = {{80, 25}, {80, 40}};
    for (const auto& grid : grids) {
        QAction* act = gridMenu->addAction(QStringLiteral("%1 x %2").arg(grid.first).arg(grid.second));
        act->setCheckable(true);
        act->setData(grid.second);
        gridSizeGroup_->addAction(act);
        connect(act, &QAction::triggered, this, [this, grid]() {
            setGridSize(grid.first, grid.second);
            emit gridSizeChanged(id_, grid.first, grid.second);
        });
    }
}

void ScriptTerminalDialog::syncSettingsMenuChecks() {
    if (fontSizeGroup_ != nullptr) {
        const int point =
            fontPointSizeOverride_ > 0 ? fontPointSizeOverride_ : profile_.fontPointSize;
        for (QAction* act : fontSizeGroup_->actions()) {
            act->setChecked(act->data().toInt() == point);
        }
    }
    if (gridSizeGroup_ != nullptr) {
        for (QAction* act : gridSizeGroup_->actions()) {
            act->setChecked(act->data().toInt() == profile_.rows);
        }
    }
}

} // namespace maxchat::ui
