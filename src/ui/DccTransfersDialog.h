#pragma once

#include <QDialog>

class QTableWidget;

namespace maxchat::ui {

class DccManager;

// Lists active/pending DCC transfers with progress and Accept/Cancel actions.
class DccTransfersDialog final : public QDialog {
    Q_OBJECT

  public:
    explicit DccTransfersDialog(DccManager* manager, QWidget* parent = nullptr);

  private:
    void refresh();

    DccManager* manager_ = nullptr;
    QTableWidget* table_ = nullptr;
};

} // namespace maxchat::ui
