#include "ui/ScriptTerminalDialog.h"

#include "ui/AnsiRenderer.h"

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
    QTextCursor cursor = display_->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertHtml(AnsiRenderer::toHtml(text));
    display_->setTextCursor(cursor);
    display_->verticalScrollBar()->setValue(display_->verticalScrollBar()->maximum());
}

void ScriptTerminalDialog::clear() {
    display_->clear();
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

void ScriptTerminalDialog::closeEvent(QCloseEvent* event) {
    emit terminalClosed(id_);
    QDialog::closeEvent(event);
}

void ScriptTerminalDialog::resizeEvent(QResizeEvent* event) {
    QDialog::resizeEvent(event);
    updateFixedGridSize();
}

void ScriptTerminalDialog::applyProfile() {
    QFont font(profile_.fontFamily);
    if (!QFontDatabase::families().contains(profile_.fontFamily)) {
        font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    }
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    font.setPointSize(profile_.fontPointSize);
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

} // namespace maxchat::ui
