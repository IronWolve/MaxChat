#pragma once

#include <QDialog>
#include <QVariantMap>

#include <functional>

class QLineEdit;
class QTableWidget;

namespace maxchat::ui {

class AliasEditorDialog final : public QDialog {
public:
  using SaveCallback = std::function<void(const QVariantMap &aliases)>;

  explicit AliasEditorDialog(const QVariantMap &aliases, SaveCallback save = {},
                             QWidget *parent = nullptr);

  [[nodiscard]] QVariantMap aliases() const;

private:
  void populate(const QVariantMap &aliases);
  void syncEditorFromSelection();
  void addOrUpdateAlias();
  void removeSelected();
  void restoreDefaults();
  void saveAndAccept();

  QTableWidget *table_ = nullptr;
  QLineEdit *aliasEntry_ = nullptr;
  QLineEdit *templateEntry_ = nullptr;
  SaveCallback save_;
};

} // namespace maxchat::ui
