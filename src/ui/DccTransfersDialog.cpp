#include "ui/DccTransfersDialog.h"

#include "ui/DccManager.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace maxchat::ui {

namespace {

QString stateLabel(DccTransfer::State state) {
    switch (state) {
    case DccTransfer::State::Pending:
        return QStringLiteral("Pending");
    case DccTransfer::State::Offered:
        return QStringLiteral("Offered");
    case DccTransfer::State::Connecting:
        return QStringLiteral("Connecting");
    case DccTransfer::State::Active:
        return QStringLiteral("Active");
    case DccTransfer::State::Resuming:
        return QStringLiteral("Resuming");
    case DccTransfer::State::Done:
        return QStringLiteral("Done");
    case DccTransfer::State::Failed:
        return QStringLiteral("Failed");
    case DccTransfer::State::Cancelled:
        return QStringLiteral("Cancelled");
    }
    return {};
}

QString humanBytes(qint64 bytes) {
    constexpr double kb = 1024.0;
    if (bytes >= kb * kb * kb) {
        return QStringLiteral("%1 GB").arg(bytes / (kb * kb * kb), 0, 'f', 1);
    }
    if (bytes >= kb * kb) {
        return QStringLiteral("%1 MB").arg(bytes / (kb * kb), 0, 'f', 1);
    }
    if (bytes >= kb) {
        return QStringLiteral("%1 KB").arg(bytes / kb, 0, 'f', 1);
    }
    return QStringLiteral("%1 B").arg(bytes);
}

// Em dash placeholder for "no estimate".
const QString kNoEta = QString::fromUtf8("\xE2\x80\x94");

QString etaLabel(double secs) {
    if (secs <= 0 || secs != secs) { // 0 or NaN
        return kNoEta;
    }
    const qint64 s = static_cast<qint64>(secs);
    if (s < 60) {
        return QStringLiteral("%1s").arg(s);
    }
    if (s < 3600) {
        return QStringLiteral("%1m %2s").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1h %2m").arg(s / 3600).arg((s % 3600) / 60, 2, 10, QLatin1Char('0'));
}

bool isTerminal(DccTransfer::State state) {
    return state == DccTransfer::State::Done || state == DccTransfer::State::Failed ||
           state == DccTransfer::State::Cancelled;
}

} // namespace

DccTransfersDialog::DccTransfersDialog(DccManager* manager, QWidget* parent)
    : QDialog(parent), manager_(manager) {
    setWindowTitle(QStringLiteral("File Transfers (DCC)"));
    resize(560, 320);

    auto* root = new QVBoxLayout(this);
    table_ = new QTableWidget(this);
    table_->setColumnCount(7);
    table_->setHorizontalHeaderLabels(
        {QStringLiteral("Dir"), QStringLiteral("Peer"), QStringLiteral("File"),
         QStringLiteral("Progress"), QStringLiteral("Rate"), QStringLiteral("ETA"),
         QStringLiteral("State")});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    root->addWidget(table_, 1);

    auto* buttons = new QHBoxLayout();
    auto* accept = new QPushButton(QStringLiteral("Accept"), this);
    auto* cancel = new QPushButton(QStringLiteral("Cancel Transfer"), this);
    auto* openFolder = new QPushButton(QStringLiteral("Open folder"), this);
    auto* close = new QPushButton(QStringLiteral("Close"), this);
    buttons->addWidget(accept);
    buttons->addWidget(cancel);
    buttons->addWidget(openFolder);
    buttons->addStretch(1);
    buttons->addWidget(close);
    root->addLayout(buttons);
    connect(openFolder, &QPushButton::clicked, this, [this]() {
        const int row = table_->currentRow();
        if (row < 0) {
            return;
        }
        const QString path =
            table_->item(row, 2)->data(Qt::UserRole + 1).toString();
        if (!path.isEmpty()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
        }
    });

    const auto selectedId = [this]() -> int {
        const int row = table_->currentRow();
        if (row < 0) {
            return -1;
        }
        return table_->item(row, 0)->data(Qt::UserRole).toInt();
    };
    connect(accept, &QPushButton::clicked, this, [this, selectedId]() {
        const int id = selectedId();
        if (id >= 0) {
            manager_->acceptTransfer(id);
        }
    });
    connect(cancel, &QPushButton::clicked, this, [this, selectedId]() {
        const int id = selectedId();
        if (id >= 0) {
            manager_->cancelTransfer(id);
        }
    });
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(manager_, &DccManager::transfersChanged, this, &DccTransfersDialog::refresh);

    // Recompute rate/ETA once a second; the data-changed signal handles the rest.
    clock_.start();
    rateTimer_ = new QTimer(this);
    connect(rateTimer_, &QTimer::timeout, this, &DccTransfersDialog::sampleRates);
    rateTimer_->start(1000);
    refresh();
}

void DccTransfersDialog::sampleRates() {
    const qint64 now = clock_.elapsed();
    for (const DccTransfer& t : manager_->transfers()) {
        if (t.state != DccTransfer::State::Active) {
            // Drop the running average so a resumed transfer measures afresh.
            sampleRate_.remove(t.id);
            sampleBytes_[t.id] = t.transferred;
            sampleTimeMs_[t.id] = now;
            continue;
        }
        if (sampleTimeMs_.contains(t.id)) {
            const qint64 dt = now - sampleTimeMs_.value(t.id);
            if (dt > 0) {
                const double inst =
                    std::max(0.0, double(t.transferred - sampleBytes_.value(t.id)) * 1000.0 / dt);
                sampleRate_[t.id] =
                    sampleRate_.contains(t.id) ? sampleRate_.value(t.id) * 0.4 + inst * 0.6 : inst;
            }
        }
        sampleBytes_[t.id] = t.transferred;
        sampleTimeMs_[t.id] = now;
    }
    refresh();
}

void DccTransfersDialog::refresh() {
    const QVector<DccTransfer> transfers = manager_->transfers();
    table_->setRowCount(transfers.size());
    for (int row = 0; row < transfers.size(); ++row) {
        const DccTransfer& t = transfers.at(row);
        const QString progress =
            t.size > 0 ? QStringLiteral("%1%  (%2 / %3)")
                             .arg(t.transferred * 100 / t.size)
                             .arg(humanBytes(t.transferred), humanBytes(t.size))
                       : humanBytes(t.transferred);
        auto* dirItem = new QTableWidgetItem(
            t.direction == DccTransfer::Direction::Send ? QString::fromUtf8("Send \xE2\x86\x91")
                                                        : QString::fromUtf8("Recv \xE2\x86\x93"));
        dirItem->setData(Qt::UserRole, t.id);
        table_->setItem(row, 0, dirItem);
        table_->setItem(row, 1, new QTableWidgetItem(t.peer));
        auto* fileItem = new QTableWidgetItem(t.fileName);
        fileItem->setData(Qt::UserRole + 1, t.localPath);
        table_->setItem(row, 2, fileItem);
        table_->setItem(row, 3, new QTableWidgetItem(progress));

        QString rate;
        QString eta = kNoEta;
        const double r = sampleRate_.value(t.id, 0.0);
        if (t.state == DccTransfer::State::Active && r > 1.0) {
            rate = humanBytes(static_cast<qint64>(r)) + QStringLiteral("/s");
            const double remaining = t.size > t.transferred ? double(t.size - t.transferred) / r : 0.0;
            eta = etaLabel(remaining);
        }
        if (isTerminal(t.state)) {
            rate.clear();
            eta = kNoEta;
        }
        table_->setItem(row, 4, new QTableWidgetItem(rate));
        table_->setItem(row, 5, new QTableWidgetItem(eta));
        table_->setItem(row, 6, new QTableWidgetItem(stateLabel(t.state)));
    }
}

} // namespace maxchat::ui
