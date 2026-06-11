#pragma once

#include <QDialog>
#include <QElapsedTimer>
#include <QHash>

class QTableWidget;
class QTimer;

namespace maxchat::ui {

class DccManager;

// Lists active/pending DCC transfers with progress, live rate/ETA, and
// Accept/Cancel actions.
class DccTransfersDialog final : public QDialog {
    Q_OBJECT

  public:
    explicit DccTransfersDialog(DccManager* manager, QWidget* parent = nullptr);

  private:
    void refresh();
    void sampleRates(); // 1 Hz: recompute per-transfer EMA throughput, then refresh

    DccManager* manager_ = nullptr;
    QTableWidget* table_ = nullptr;
    QTimer* rateTimer_ = nullptr;
    QElapsedTimer clock_;
    // Per-transfer rate sampling, keyed by transfer id.
    QHash<int, qint64> sampleBytes_;
    QHash<int, qint64> sampleTimeMs_;
    QHash<int, double> sampleRate_; // bytes/sec EMA; absent until first measured
};

} // namespace maxchat::ui
