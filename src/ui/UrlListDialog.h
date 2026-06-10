#pragma once

#include <QDialog>
#include <QStringList>

class QListWidget;

namespace maxchat::ui {

class UrlListDialog final : public QDialog {
    Q_OBJECT

  public:
    explicit UrlListDialog(QWidget* parent = nullptr);

    void setUrls(const QStringList& urls);
    void appendUrls(const QStringList& urls);
    void clearUrls();

    [[nodiscard]] QStringList urls() const;

  signals:
    void clearRequested();

  private:
    [[nodiscard]] QString selectedOrLastUrl() const;

    QListWidget* m_urlList = nullptr;
};

} // namespace maxchat::ui
