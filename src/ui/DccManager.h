#pragma once

#include <QFile>
#include <QObject>
#include <QString>
#include <QVector>

class QTcpServer;
class QTcpSocket;

namespace maxchat::ui {

// A single DCC transfer (active-mode SEND or GET). Foundational: no passive
// (reverse) mode or RESUME yet.
struct DccTransfer {
    enum class Direction { Send, Receive };
    enum class State { Pending, Active, Done, Failed, Cancelled };

    int id = 0;
    Direction direction = Direction::Send;
    State state = State::Pending;
    QString peer;
    QString fileName;
    QString localPath;
    quint32 host = 0; // for incoming offers (network byte order value)
    quint16 port = 0;
    qint64 size = 0;
    qint64 transferred = 0;
};

// Manages DCC file transfers. Sends/receives over direct TCP and negotiates via
// CTCP DCC strings (relayed through the IRC connection by the owner).
class DccManager final : public QObject {
    Q_OBJECT

  public:
    explicit DccManager(QObject* parent = nullptr);

    void setDownloadDir(const QString& dir);

    // Begin an outgoing send; emits ctcpToSend() with the DCC SEND offer.
    void offerSend(const QString& peer, const QString& filePath);
    // Handle an incoming CTCP DCC argument string ("SEND name ip port size").
    void handleIncoming(const QString& sender, const QString& args);
    // Accept / cancel a pending or active transfer by id.
    void acceptTransfer(int id);
    void cancelTransfer(int id);

    [[nodiscard]] QVector<DccTransfer> transfers() const { return transfers_; }

  signals:
    void ctcpToSend(const QString& peer, const QString& ctcpArgs);
    void transfersChanged();
    void status(const QString& message);

  private:
    DccTransfer* findById(int id);
    void startReceive(DccTransfer& transfer);
    void emitChanged() { emit transfersChanged(); }

    QVector<DccTransfer> transfers_;
    int nextId_ = 1;
    QString downloadDir_;
};

} // namespace maxchat::ui
