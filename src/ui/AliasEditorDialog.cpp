#include "ui/AliasEditorDialog.h"

#include "core/CommandAlias.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

namespace maxchat::ui {

namespace {

QString cleanAliasName(QString name) {
  name = name.trimmed();
  while (name.startsWith(QLatin1Char('/'))) {
    name.remove(0, 1);
  }
  if (name.isEmpty()) {
    return {};
  }
  for (const QChar ch : name) {
    if (ch.isSpace()) {
      return {};
    }
  }
  return name.toLower();
}

QString cleanAliasTemplate(QString aliasTemplate) {
  aliasTemplate = aliasTemplate.trimmed();
  if (!aliasTemplate.isEmpty() && !aliasTemplate.startsWith(QLatin1Char('/'))) {
    aliasTemplate.prepend(QLatin1Char('/'));
  }
  return aliasTemplate;
}

QTableWidgetItem *newItem(const QString &text) {
  auto *item = new QTableWidgetItem(text);
  item->setFlags(item->flags() | Qt::ItemIsEditable);
  return item;
}

int rowForAlias(const QTableWidget *table, const QString &alias) {
  if (table == nullptr || alias.isEmpty()) {
    return -1;
  }
  for (int row = 0; row < table->rowCount(); ++row) {
    const QTableWidgetItem *item = table->item(row, 0);
    if (item != nullptr &&
        cleanAliasName(item->text()).compare(alias, Qt::CaseInsensitive) == 0) {
      return row;
    }
  }
  return -1;
}

} // namespace

AliasEditorDialog::AliasEditorDialog(const QVariantMap &aliases,
                                     SaveCallback save, QWidget *parent)
    : QDialog(parent), save_(std::move(save)) {
  setWindowTitle(QStringLiteral("Command Aliases"));
  resize(620, 420);

  auto *layout = new QVBoxLayout(this);
  auto *intro = new QLabel(
      QStringLiteral("Aliases expand slash commands before they are sent. Use "
                     "$* for all arguments, $1 for one argument, or $2- for "
                     "the rest."),
      this);
  intro->setWordWrap(true);
  layout->addWidget(intro);

  table_ = new QTableWidget(this);
  table_->setObjectName(QStringLiteral("alias_table"));
  table_->setColumnCount(2);
  table_->setHorizontalHeaderLabels(
      {QStringLiteral("Alias"), QStringLiteral("Expands To")});
  table_->horizontalHeader()->setStretchLastSection(true);
  table_->verticalHeader()->setVisible(false);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::SingleSelection);
  layout->addWidget(table_, 1);

  auto *entryRow = new QHBoxLayout();
  aliasEntry_ = new QLineEdit(this);
  aliasEntry_->setObjectName(QStringLiteral("alias_name"));
  aliasEntry_->setPlaceholderText(QStringLiteral("alias"));
  templateEntry_ = new QLineEdit(this);
  templateEntry_->setObjectName(QStringLiteral("alias_template"));
  templateEntry_->setPlaceholderText(QStringLiteral("/command $*"));
  auto *addButton = new QPushButton(QStringLiteral("Add / Update"), this);
  addButton->setObjectName(QStringLiteral("alias_add_update"));
  auto *removeButton = new QPushButton(QStringLiteral("Remove"), this);
  removeButton->setObjectName(QStringLiteral("alias_remove"));
  auto *restoreButton =
      new QPushButton(QStringLiteral("Restore Defaults"), this);
  restoreButton->setObjectName(QStringLiteral("alias_restore_defaults"));

  connect(aliasEntry_, &QLineEdit::returnPressed, this,
          &AliasEditorDialog::addOrUpdateAlias);
  connect(templateEntry_, &QLineEdit::returnPressed, this,
          &AliasEditorDialog::addOrUpdateAlias);
  connect(addButton, &QPushButton::clicked, this,
          &AliasEditorDialog::addOrUpdateAlias);
  connect(removeButton, &QPushButton::clicked, this,
          &AliasEditorDialog::removeSelected);
  connect(restoreButton, &QPushButton::clicked, this,
          &AliasEditorDialog::restoreDefaults);

  entryRow->addWidget(aliasEntry_);
  entryRow->addWidget(templateEntry_, 1);
  entryRow->addWidget(addButton);
  entryRow->addWidget(removeButton);
  entryRow->addWidget(restoreButton);
  layout->addLayout(entryRow);

  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  buttons->setObjectName(QStringLiteral("alias_buttons"));
  connect(buttons, &QDialogButtonBox::accepted, this,
          &AliasEditorDialog::saveAndAccept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons);

  connect(table_, &QTableWidget::currentCellChanged, this,
          [this](int, int, int, int) { syncEditorFromSelection(); });

  populate(aliases);
}

QVariantMap AliasEditorDialog::aliases() const {
  QVariantMap out;
  if (table_ == nullptr) {
    return out;
  }

  for (int row = 0; row < table_->rowCount(); ++row) {
    const QTableWidgetItem *aliasItem = table_->item(row, 0);
    const QTableWidgetItem *templateItem = table_->item(row, 1);
    if (aliasItem == nullptr || templateItem == nullptr) {
      continue;
    }
    const QString alias = cleanAliasName(aliasItem->text());
    const QString aliasTemplate = cleanAliasTemplate(templateItem->text());
    if (!alias.isEmpty() && !aliasTemplate.isEmpty()) {
      out.insert(alias, aliasTemplate);
    }
  }
  return out;
}

void AliasEditorDialog::populate(const QVariantMap &aliases) {
  if (table_ == nullptr) {
    return;
  }

  table_->setRowCount(0);
  QStringList keys = aliases.keys();
  std::sort(keys.begin(), keys.end(),
            [](const QString &left, const QString &right) {
              return left.compare(right, Qt::CaseInsensitive) < 0;
            });

  for (const QString &key : keys) {
    const QString alias = cleanAliasName(key);
    const QString aliasTemplate =
        cleanAliasTemplate(aliases.value(key).toString());
    if (alias.isEmpty() || aliasTemplate.isEmpty()) {
      continue;
    }
    const int row = table_->rowCount();
    table_->insertRow(row);
    table_->setItem(row, 0, newItem(alias));
    table_->setItem(row, 1, newItem(aliasTemplate));
  }
  if (table_->rowCount() > 0) {
    table_->selectRow(0);
    syncEditorFromSelection();
  }
}

void AliasEditorDialog::syncEditorFromSelection() {
  if (table_ == nullptr || aliasEntry_ == nullptr ||
      templateEntry_ == nullptr) {
    return;
  }

  const int row = table_->currentRow();
  const QTableWidgetItem *aliasItem = row >= 0 ? table_->item(row, 0) : nullptr;
  const QTableWidgetItem *templateItem =
      row >= 0 ? table_->item(row, 1) : nullptr;
  aliasEntry_->setText(aliasItem == nullptr ? QString() : aliasItem->text());
  templateEntry_->setText(templateItem == nullptr ? QString()
                                                  : templateItem->text());
}

void AliasEditorDialog::addOrUpdateAlias() {
  if (table_ == nullptr) {
    return;
  }

  const QString alias =
      cleanAliasName(aliasEntry_ == nullptr ? QString() : aliasEntry_->text());
  const QString aliasTemplate = cleanAliasTemplate(
      templateEntry_ == nullptr ? QString() : templateEntry_->text());
  if (alias.isEmpty() || aliasTemplate.isEmpty()) {
    return;
  }

  int row = rowForAlias(table_, alias);
  if (row < 0) {
    row = table_->rowCount();
    table_->insertRow(row);
  }
  table_->setItem(row, 0, newItem(alias));
  table_->setItem(row, 1, newItem(aliasTemplate));
  table_->selectRow(row);
}

void AliasEditorDialog::removeSelected() {
  if (table_ == nullptr) {
    return;
  }
  const int row = table_->currentRow();
  if (row >= 0) {
    table_->removeRow(row);
  }
}

void AliasEditorDialog::restoreDefaults() {
  populate(maxchat::core::defaultCommandAliases());
}

void AliasEditorDialog::saveAndAccept() {
  if (save_) {
    save_(aliases());
  }
  accept();
}

} // namespace maxchat::ui
