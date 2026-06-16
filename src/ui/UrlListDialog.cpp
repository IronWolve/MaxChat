#include "ui/UrlListDialog.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QListWidget>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace maxchat::ui {

namespace {

QUrl openableUrl(const QString& url) {
    QUrl out;
    if (url.startsWith(QStringLiteral("www."), Qt::CaseInsensitive)) {
        out = QUrl(QStringLiteral("https://") + url);
    } else {
        out = QUrl(url);
    }
    // Defence in depth: the URL detector only emits http/https/ftp/www., but
    // this dialog must not depend on its producer — never hand file:// or
    // javascript: to QDesktopServices.
    const QString scheme = out.scheme().toLower();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https") &&
        scheme != QLatin1String("ftp")) {
        return {};
    }
    return out;
}

} // namespace

UrlListDialog::UrlListDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("URL List"));
    resize(700, 460);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    m_urlList = new QListWidget(this);
    m_urlList->setObjectName(QStringLiteral("urlList"));
    layout->addWidget(m_urlList);

    auto* buttons = new QDialogButtonBox(this);
    auto* openButton = buttons->addButton(QStringLiteral("Open"), QDialogButtonBox::ActionRole);
    auto* copyButton = buttons->addButton(QStringLiteral("Copy"), QDialogButtonBox::ActionRole);
    auto* clearButton = buttons->addButton(QStringLiteral("Clear"), QDialogButtonBox::ResetRole);
    auto* closeButton = buttons->addButton(QDialogButtonBox::Close);
    closeButton->setDefault(true);
    layout->addWidget(buttons);

    connect(openButton, &QPushButton::clicked, this, [this]() {
        const QString url = selectedOrLastUrl();
        if (!url.isEmpty()) {
            QDesktopServices::openUrl(openableUrl(url));
        }
    });
    connect(copyButton, &QPushButton::clicked, this, [this]() {
        if (QGuiApplication::clipboard() != nullptr) {
            QGuiApplication::clipboard()->setText(urls().join(QLatin1Char('\n')));
        }
    });
    connect(clearButton, &QPushButton::clicked, this, [this]() {
        clearUrls();
        emit clearRequested();
    });
    connect(closeButton, &QPushButton::clicked, this, &QDialog::close);
    connect(m_urlList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        if (item != nullptr) {
            QDesktopServices::openUrl(openableUrl(item->text()));
        }
    });
}

void UrlListDialog::setUrls(const QStringList& urls) {
    if (m_urlList == nullptr) {
        return;
    }
    m_urlList->clear();
    appendUrls(urls);
}

void UrlListDialog::appendUrls(const QStringList& urls) {
    if (m_urlList == nullptr) {
        return;
    }
    for (const QString& url : urls) {
        const QString trimmed = url.trimmed();
        if (!trimmed.isEmpty()) {
            m_urlList->addItem(trimmed);
        }
    }
    if (m_urlList->count() > 0) {
        m_urlList->setCurrentRow(m_urlList->count() - 1);
        m_urlList->scrollToBottom();
    }
}

void UrlListDialog::clearUrls() {
    if (m_urlList != nullptr) {
        m_urlList->clear();
    }
}

QStringList UrlListDialog::urls() const {
    QStringList result;
    if (m_urlList == nullptr) {
        return result;
    }
    for (int row = 0; row < m_urlList->count(); ++row) {
        const QListWidgetItem* item = m_urlList->item(row);
        if (item != nullptr) {
            result.append(item->text());
        }
    }
    return result;
}

QString UrlListDialog::selectedOrLastUrl() const {
    if (m_urlList == nullptr || m_urlList->count() == 0) {
        return QString();
    }
    const QListWidgetItem* current = m_urlList->currentItem();
    if (current != nullptr) {
        return current->text();
    }
    const QListWidgetItem* last = m_urlList->item(m_urlList->count() - 1);
    return last == nullptr ? QString() : last->text();
}

} // namespace maxchat::ui
