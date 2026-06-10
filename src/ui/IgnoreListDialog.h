#pragma once

#include <QDialog>
#include <QStringList>

#include <functional>

class QLineEdit;
class QListWidget;

namespace maxchat::ui {

class IgnoreListDialog final : public QDialog {
  public:
    using SaveCallback = std::function<void(const QStringList& masks)>;

    explicit IgnoreListDialog(const QStringList& ignores, SaveCallback save,
                              QWidget* parent = nullptr);

    [[nodiscard]] QStringList masks() const;

  private:
    void addMask();
    void removeSelected();
    void saveAndAccept();

    QListWidget* list_ = nullptr;
    QLineEdit* entry_ = nullptr;
    SaveCallback save_;
};

} // namespace maxchat::ui
