#pragma once

#include <QDialog>

class QCheckBox;
class QLineEdit;

namespace maxchat::ui {

class ChatFindDialog final : public QDialog {
    Q_OBJECT

  public:
    explicit ChatFindDialog(QWidget* parent = nullptr);

    void setSearchText(const QString& text);
    void setCaseSensitive(bool enabled);
    void setWrapSearch(bool enabled);

    [[nodiscard]] QString searchText() const;
    [[nodiscard]] bool caseSensitive() const;
    [[nodiscard]] bool wrapSearch() const;

    void requestFindNext();
    void requestFindPrevious();

  signals:
    void findNextRequested(const QString& text, bool caseSensitive, bool wrapSearch);
    void findPreviousRequested(const QString& text, bool caseSensitive, bool wrapSearch);

  private:
    QLineEdit* m_searchInput = nullptr;
    QCheckBox* m_caseSensitive = nullptr;
    QCheckBox* m_wrapSearch = nullptr;
};

} // namespace maxchat::ui
