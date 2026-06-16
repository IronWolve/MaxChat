#include "ui/ChatFindDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace maxchat::ui {

ChatFindDialog::ChatFindDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Find in Chat"));
    resize(420, 130);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    auto* form = new QFormLayout();
    m_searchInput = new QLineEdit(this);
    m_searchInput->setObjectName(QStringLiteral("searchInput"));
    form->addRow(tr("Find:"), m_searchInput);
    layout->addLayout(form);

    auto* options = new QHBoxLayout();
    m_caseSensitive = new QCheckBox(tr("Case sensitive"), this);
    m_caseSensitive->setObjectName(QStringLiteral("caseSensitive"));
    m_wrapSearch = new QCheckBox(tr("Wrap"), this);
    m_wrapSearch->setObjectName(QStringLiteral("wrapSearch"));
    m_wrapSearch->setChecked(true);
    options->addWidget(m_caseSensitive);
    options->addWidget(m_wrapSearch);
    options->addStretch(1);
    layout->addLayout(options);

    auto* buttons = new QDialogButtonBox(this);
    auto* previousButton =
        buttons->addButton(QStringLiteral("Previous"), QDialogButtonBox::ActionRole);
    auto* nextButton = buttons->addButton(QStringLiteral("Next"), QDialogButtonBox::ActionRole);
    auto* closeButton = buttons->addButton(QDialogButtonBox::Close);
    nextButton->setDefault(true);
    layout->addWidget(buttons);

    connect(m_searchInput, &QLineEdit::returnPressed, this, &ChatFindDialog::requestFindNext);
    connect(previousButton, &QPushButton::clicked, this, &ChatFindDialog::requestFindPrevious);
    connect(nextButton, &QPushButton::clicked, this, &ChatFindDialog::requestFindNext);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::close);
}

void ChatFindDialog::setSearchText(const QString& text) {
    if (m_searchInput != nullptr) {
        m_searchInput->setText(text);
        m_searchInput->selectAll();
    }
}

void ChatFindDialog::setCaseSensitive(bool enabled) {
    if (m_caseSensitive != nullptr) {
        m_caseSensitive->setChecked(enabled);
    }
}

void ChatFindDialog::setWrapSearch(bool enabled) {
    if (m_wrapSearch != nullptr) {
        m_wrapSearch->setChecked(enabled);
    }
}

QString ChatFindDialog::searchText() const {
    return m_searchInput == nullptr ? QString() : m_searchInput->text();
}

bool ChatFindDialog::caseSensitive() const {
    return m_caseSensitive != nullptr && m_caseSensitive->isChecked();
}

bool ChatFindDialog::wrapSearch() const {
    return m_wrapSearch == nullptr || m_wrapSearch->isChecked();
}

void ChatFindDialog::requestFindNext() {
    emit findNextRequested(searchText(), caseSensitive(), wrapSearch());
}

void ChatFindDialog::requestFindPrevious() {
    emit findPreviousRequested(searchText(), caseSensitive(), wrapSearch());
}

} // namespace maxchat::ui
