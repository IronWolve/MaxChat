#include "ui/RawLogDialog.h"

#include <QClipboard>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextCursor>
#include <QVBoxLayout>

namespace maxchat::ui {

RawLogDialog::RawLogDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("Raw Log"));
    resize(780, 520);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    m_logView = new QPlainTextEdit(this);
    m_logView->setObjectName(QStringLiteral("rawLogView"));
    m_logView->setReadOnly(true);
    m_logView->setLineWrapMode(QPlainTextEdit::NoWrap);
    layout->addWidget(m_logView);

    auto* buttons = new QDialogButtonBox(this);
    auto* clearButton = buttons->addButton(QStringLiteral("Clear"), QDialogButtonBox::ResetRole);
    auto* copyButton = buttons->addButton(QStringLiteral("Copy"), QDialogButtonBox::ActionRole);
    auto* closeButton = buttons->addButton(QDialogButtonBox::Close);
    closeButton->setDefault(true);
    layout->addWidget(buttons);

    connect(clearButton, &QPushButton::clicked, this, [this]() {
        clearLog();
        emit clearRequested();
    });
    connect(copyButton, &QPushButton::clicked, this, [this]() {
        if (QGuiApplication::clipboard() != nullptr) {
            QGuiApplication::clipboard()->setText(rawText());
        }
    });
    connect(closeButton, &QPushButton::clicked, this, &QDialog::close);
}

void RawLogDialog::setLines(const QStringList& lines) {
    if (m_logView == nullptr) {
        return;
    }
    m_logView->setPlainText(lines.join(QLatin1Char('\n')));
    m_logView->moveCursor(QTextCursor::End);
}

void RawLogDialog::appendLine(const QString& line) {
    if (m_logView == nullptr) {
        return;
    }
    m_logView->appendPlainText(line);
}

void RawLogDialog::clearLog() {
    if (m_logView != nullptr) {
        m_logView->clear();
    }
}

QString RawLogDialog::rawText() const {
    return m_logView == nullptr ? QString() : m_logView->toPlainText();
}

} // namespace maxchat::ui
