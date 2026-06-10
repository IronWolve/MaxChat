#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

class QFile;
class QTcpServer;
class QTcpSocket;

namespace maxchat::ui {

// A single DCC file transfer record (shown in the transfers dialog).
struct DccTransfer {
    enum class Direction { Send, Receive };
    enum class State { Pending, Offered, Connecting, Active, Resuming, Done, Failed, Cancelled };

    int id = 0;
    Direction direction = Direction::Send;
    State state = State::Pending;
    QString peer;
    QString fileName;
    QString localPath;
    quint32 host = 0; // incoming offer address (host-order uint32)
    quint16 port = 0;
    qint64 size = 0;
    qint64 transferred = 0;
};

// One DCC CHAT session (a direct newline text link).
struct DccChat {
    enum class State { Connecting, Active, Closed };
    int id = 0;
    QString peer;
    State state = State::Connecting;
};

// Manages DCC file transfers and chats over direct TCP, negotiating via CTCP DCC
// strings relayed through the IRC connection. Supports active + passive modes,
// RESUME/ACCEPT, accept policy, port range, and a configurable advertised IP.
class DccManager final : public QObject {
    Q_OBJECT

  public:
    explicit DccManager(QObject* parent = nullptr);
    ~DccManager() override;

    void setDownloadDir(const QString& dir);
    void setPortRange(int first, int last);
    void setPassive(bool passive);
    void setAdvertisedIp(const QString& ip); // "" = use 127.0.0.1 fallback
    void setAcceptPolicy(const QString& policy, const QStringList& trustedNicks);

    void offerSend(const QString& peer, const QString& filePath);
    void offerChat(const QString& peer);
    // Parse a received CTCP DCC argument string (without the leading "DCC").
    void handleIncoming(const QString& sender, const QString& args);

    void acceptTransfer(int id);
    void cancelTransfer(int id);

    // DCC CHAT line I/O (peer keyed by lowercase nick).
    void sendChatLine(const QString& peer, const QString& line);
    void closeChat(const QString& peer);

    [[nodiscard]] QVector<DccTransfer> transfers() const { return transfers_; }

  signals:
    void ctcpToSend(const QString& peer, const QString& ctcpArgs);
    void transfersChanged();
    void status(const QString& message);
    void chatStateChanged(const QString& peer, int state); // DccChat::State as int
    void chatLineReceived(const QString& peer, const QString& line);

  private:
    struct Runtime {
        QTcpServer* server = nullptr;
        QTcpSocket* socket = nullptr;
        QFile* file = nullptr;
        bool allSent = false;
        qint64 startOffset = 0;
        QByteArray ackBuf;
    };
    struct ChatRuntime {
        DccChat info;
        QTcpServer* server = nullptr;
        QTcpSocket* socket = nullptr;
        QByteArray buffer;
        QString pendingToken;
    };

    DccTransfer* findById(int id);
    Runtime& rt(int id) { return runtimes_[id]; }
    void emitChanged() { emit transfersChanged(); }
    [[nodiscard]] quint16 openListenPort(QTcpServer* server);
    [[nodiscard]] QString advertisedIp() const;
    [[nodiscard]] QString destPath(const QString& name, qint64 size) const;

    void beginSend(int id, QTcpSocket* socket);
    void beginReceive(int id, QTcpSocket* socket);
    void pumpSend(int id);
    void finishTransfer(int id, bool ok);

    void inSend(const QString& sender, const QStringList& toks);
    void inResume(const QString& sender, const QStringList& toks);
    void inAccept(const QString& sender, const QStringList& toks);
    void inChat(const QString& sender, const QStringList& toks);
    void connectChat(ChatRuntime& chat, quint32 host, quint16 port);
    void wireChatSocket(const QString& key);

    QVector<DccTransfer> transfers_;
    QHash<int, Runtime> runtimes_;
    QHash<QString, int> awaitingTokens_; // passive send token -> id
    QHash<quint16, int> sendByPort_;     // active send local port -> id
    QHash<QString, int> resuming_;       // resume key -> id
    QHash<QString, ChatRuntime*> chats_; // lowercase peer -> chat
    int nextId_ = 1;
    QString downloadDir_;
    int portFirst_ = 0;
    int portLast_ = 0;
    bool passive_ = true;
    QString advertisedIp_;
    QString acceptPolicy_ = QStringLiteral("ask");
    QStringList trusted_;
};

} // namespace maxchat::ui
