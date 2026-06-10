#pragma once

#include "core/NetworkImport.h"

#include <QDialog>

class QCheckBox;
class QLabel;
class QListWidget;
class QPushButton;

namespace maxchat::ui {

class ServerListDialog final : public QDialog {
    Q_OBJECT

  public:
    explicit ServerListDialog(maxchat::core::NetworkConfigList networks, bool connectOnStart,
                              QWidget* parent = nullptr);

    [[nodiscard]] maxchat::core::NetworkConfigList networks() const;
    [[nodiscard]] maxchat::core::NetworkConfig selectedNetwork() const;
    [[nodiscard]] bool connectOnStart() const;
    [[nodiscard]] bool connectWasRequested() const;
    [[nodiscard]] QString currentHomepageButtonText() const;

    void setCurrentRow(int row);
    [[nodiscard]] bool moveCurrentNetwork(int delta);
    [[nodiscard]] bool removeCurrentNetwork();
    void resetToDefaults();

    [[nodiscard]] static QString networkLabel(const maxchat::core::NetworkConfig& network);
    [[nodiscard]] static QString homepageButtonText(const maxchat::core::NetworkConfig& network);

  private slots:
    void addNetwork();
    void editCurrentNetwork();
    void openHomepage();
    void requestConnect();
    void syncSelection();

  private:
    void buildUi();
    void refreshList(int rowToSelect = 0);
    [[nodiscard]] int currentRow() const;
    [[nodiscard]] bool editNetwork(maxchat::core::NetworkConfig& network);

    maxchat::core::NetworkConfigList networks_;
    bool connectRequested_ = false;

    QListWidget* list_ = nullptr;
    QLabel* countLabel_ = nullptr;
    QPushButton* homepageButton_ = nullptr;
    QPushButton* editButton_ = nullptr;
    QPushButton* deleteButton_ = nullptr;
    QPushButton* upButton_ = nullptr;
    QPushButton* downButton_ = nullptr;
    QPushButton* connectButton_ = nullptr;
    QCheckBox* connectOnStart_ = nullptr;
};

} // namespace maxchat::ui
