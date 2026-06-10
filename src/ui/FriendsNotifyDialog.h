#pragma once

#include <QDialog>
#include <QStringList>

#include <functional>

class QLineEdit;
class QListWidget;

namespace maxchat::ui {

class FriendsNotifyDialog final : public QDialog {
  public:
    using SaveCallback = std::function<void(const QStringList& friends)>;

    explicit FriendsNotifyDialog(const QStringList& friends, SaveCallback save,
                                 QWidget* parent = nullptr);

    [[nodiscard]] QStringList friends() const;

  private:
    void addFriend();
    void removeSelected();
    void saveAndAccept();

    QListWidget* list_ = nullptr;
    QLineEdit* entry_ = nullptr;
    SaveCallback save_;
};

} // namespace maxchat::ui
