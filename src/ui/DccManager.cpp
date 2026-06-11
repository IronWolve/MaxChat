#include "ui/DccManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtEndian>

#include <algorithm>

namespace maxchat::ui {

namespace {

constexpr qint64 ChunkSize = 64 * 1024;
constexpr qint64 ChatLineCap = 8192;
constexpr qint64 ChatBufferCap = 65536;
// Anti-flood: cap how many unaccepted incoming offers can pile up at once.
constexpr int MaxPendingOffers = 32;

QString quoteName(const QString& name) {
    return QStringLiteral("\"%1\"").arg(QString(name).remove(QLatin1Char('"')));
}

// shlex-ish split: respects "double quoted" tokens so filenames with spaces work.
QStringList splitArgs(const QString& args) {
    QStringList out;
    QString cur;
    bool inQuote = false;
    bool have = false;
    for (const QChar c : args) {
        if (c == QLatin1Char('"')) {
            inQuote = !inQuote;
            have = true;
        } else if (c.isSpace() && !inQuote) {
            if (have) {
                out.append(cur);
                cur.clear();
                have = false;
            }
        } else {
            cur.append(c);
            have = true;
        }
    }
    if (have) {
        out.append(cur);
    }
    return out;
}

QString newToken() {
    // Use the system CSPRNG (like Python's secrets.token_hex) — the token is the
    // only barrier stopping a third party from injecting a fake passive reply.
    return QStringLiteral("%1").arg(QRandomGenerator::system()->generate(), 8, 16,
                                    QLatin1Char('0'));
}

} // namespace

qint64 dccWritableChunk(qint64 offeredSize, qint64 transferred, qint64 available) {
    if (available <= 0) {
        return 0;
    }
    // Unknown/zero/negative offered size → write nothing (don't trust the peer
    // to bound the stream). Otherwise cap to what's left of the offered size.
    if (offeredSize <= 0) {
        return 0;
    }
    const qint64 remaining = offeredSize - transferred;
    if (remaining <= 0) {
        return 0;
    }
    return std::min(available, remaining);
}

DccManager::DccManager(QObject* parent) : QObject(parent) {}

DccManager::~DccManager() {
    for (ChatRuntime* chat : std::as_const(chats_)) {
        delete chat;
    }
}

void DccManager::setDownloadDir(const QString& dir) { downloadDir_ = dir; }
void DccManager::setPortRange(int first, int last) {
    portFirst_ = first;
    portLast_ = last;
}
void DccManager::setPassive(bool passive) { passive_ = passive; }
void DccManager::setAdvertisedIp(const QString& ip) { advertisedIp_ = ip.trimmed(); }
void DccManager::setAcceptPolicy(const QString& policy, const QStringList& trustedNicks) {
    acceptPolicy_ = policy;
    trusted_.clear();
    for (const QString& nick : trustedNicks) {
        trusted_.append(nick.trimmed().toLower());
    }
}

DccTransfer* DccManager::findById(int id) {
    for (DccTransfer& transfer : transfers_) {
        if (transfer.id == id) {
            return &transfer;
        }
    }
    return nullptr;
}

QString DccManager::advertisedIp() const {
    return advertisedIp_.isEmpty() ? QStringLiteral("127.0.0.1") : advertisedIp_;
}

quint16 DccManager::openListenPort(QTcpServer* server) {
    if (portFirst_ > 0 && portLast_ > 0) {
        for (int port = portFirst_; port <= portLast_; ++port) {
            if (server->listen(QHostAddress::Any, static_cast<quint16>(port))) {
                return server->serverPort();
            }
        }
    }
    return server->listen(QHostAddress::Any, 0) ? server->serverPort() : 0;
}

QString DccManager::destPath(const QString& name, qint64 size) const {
    const QString dir = downloadDir_.isEmpty() ? QDir::homePath() : downloadDir_;
    QDir().mkpath(dir);
    const QString base = QFileInfo(name).fileName().isEmpty() ? QStringLiteral("file")
                                                              : QFileInfo(name).fileName();
    const QString candidate = QDir(dir).filePath(base);
    const QFileInfo existing(candidate);
    if (!existing.exists()) {
        return candidate;
    }
    // Partial file of the right name + smaller than the offer → resume candidate.
    if (existing.size() > 0 && size > 0 && existing.size() < size) {
        return candidate;
    }
    // Otherwise disambiguate: file.1.ext, file.2.ext, ...
    const QString stem = existing.completeBaseName();
    const QString suffix = existing.suffix().isEmpty() ? QString()
                                                       : QStringLiteral(".%1").arg(existing.suffix());
    for (int i = 1; i < 10000; ++i) {
        const QString tryPath = QDir(dir).filePath(QStringLiteral("%1.%2%3").arg(stem).arg(i).arg(suffix));
        if (!QFileInfo::exists(tryPath)) {
            return tryPath;
        }
    }
    return candidate;
}

// ---- outgoing SEND -------------------------------------------------------

void DccManager::offerSend(const QString& peer, const QString& filePath) {
    const QFileInfo info(filePath);
    if (!info.isFile()) {
        emit status(QStringLiteral("DCC: file not found: %1").arg(filePath));
        return;
    }

    DccTransfer transfer;
    transfer.id = nextId_++;
    transfer.direction = DccTransfer::Direction::Send;
    transfer.peer = peer;
    transfer.fileName = info.fileName();
    transfer.localPath = info.absoluteFilePath();
    transfer.size = info.size();

    if (passive_) {
        const QString token = newToken();
        transfer.state = DccTransfer::State::Offered;
        transfers_.append(transfer);
        awaitingTokens_.insert(token, transfer.id);
        emitChanged();
        emit ctcpToSend(peer, QStringLiteral("SEND %1 %2 0 %3 %4")
                                  .arg(quoteName(transfer.fileName),
                                       QString::number(QHostAddress(advertisedIp()).toIPv4Address()))
                                  .arg(transfer.size)
                                  .arg(token));
        emit status(QStringLiteral("DCC: offered %1 to %2 (passive)").arg(transfer.fileName, peer));
        return;
    }

    auto* server = new QTcpServer(this);
    const quint16 port = openListenPort(server);
    if (port == 0) {
        server->deleteLater();
        emit status(QStringLiteral("DCC: could not open a listening port."));
        return;
    }
    transfer.port = port;
    transfer.state = DccTransfer::State::Offered;
    transfers_.append(transfer);
    const int id = transfer.id;
    runtimes_[id].server = server;
    sendByPort_.insert(port, id);
    emitChanged();

    connect(server, &QTcpServer::newConnection, this, [this, server, id]() {
        QTcpSocket* socket = server->nextPendingConnection();
        server->close();
        DccTransfer* t = findById(id);
        if (t == nullptr || t->state == DccTransfer::State::Cancelled) {
            socket->abort();
            socket->deleteLater();
            return;
        }
        beginSend(id, socket);
    });

    emit ctcpToSend(peer, QStringLiteral("SEND %1 %2 %3 %4")
                              .arg(quoteName(transfer.fileName),
                                   QString::number(QHostAddress(advertisedIp()).toIPv4Address()))
                              .arg(port)
                              .arg(transfer.size));
    emit status(QStringLiteral("DCC: offered %1 to %2").arg(transfer.fileName, peer));
}

void DccManager::beginSend(int id, QTcpSocket* socket) {
    DccTransfer* t = findById(id);
    if (t == nullptr) {
        socket->abort();
        socket->deleteLater();
        return;
    }
    Runtime& r = rt(id);
    r.socket = socket;
    r.file = new QFile(t->localPath, socket);
    if (!r.file->open(QIODevice::ReadOnly)) {
        finishTransfer(id, false);
        return;
    }
    if (r.startOffset > 0) {
        r.file->seek(r.startOffset);
        t->transferred = r.startOffset;
    }
    t->state = DccTransfer::State::Active;
    emitChanged();

    connect(socket, &QTcpSocket::bytesWritten, this, [this, id](qint64) { pumpSend(id); });
    connect(socket, &QTcpSocket::readyRead, this, [this, id, socket]() {
        // Receiver acks running byte count as big-endian uint32.
        Runtime& rr = rt(id);
        rr.ackBuf.append(socket->readAll());
        DccTransfer* tr = findById(id);
        while (rr.ackBuf.size() >= 4) {
            const quint32 ack =
                qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(rr.ackBuf.constData()));
            rr.ackBuf.remove(0, 4);
            if (tr != nullptr && tr->size > 0 &&
                ack >= static_cast<quint32>(tr->size & 0xFFFFFFFF)) {
                finishTransfer(id, true);
                return;
            }
        }
    });
    connect(socket, &QTcpSocket::errorOccurred, this,
            [this, id](QAbstractSocket::SocketError) { finishTransfer(id, false); });
    connect(socket, &QTcpSocket::disconnected, this, [this, id]() {
        DccTransfer* tr = findById(id);
        if (tr != nullptr && tr->state == DccTransfer::State::Active) {
            finishTransfer(id, rt(id).allSent);
        }
    });
    pumpSend(id);
}

void DccManager::pumpSend(int id) {
    DccTransfer* t = findById(id);
    Runtime& r = rt(id);
    if (t == nullptr || r.socket == nullptr || r.file == nullptr) {
        return;
    }
    while (r.socket->bytesToWrite() < 4 * ChunkSize && !r.file->atEnd()) {
        const QByteArray chunk = r.file->read(ChunkSize);
        if (chunk.isEmpty()) {
            break;
        }
        r.socket->write(chunk);
        t->transferred += chunk.size();
    }
    if (r.file->atEnd()) {
        r.allSent = true;
    }
    emitChanged();
}

// ---- incoming dispatch ---------------------------------------------------

void DccManager::handleIncoming(const QString& sender, const QString& args) {
    QStringList toks = splitArgs(args);
    if (!toks.isEmpty() && toks.first().compare(QStringLiteral("DCC"), Qt::CaseInsensitive) == 0) {
        toks.removeFirst();
    }
    if (toks.isEmpty()) {
        return;
    }
    const QString kind = toks.first().toUpper();
    if (kind == QStringLiteral("SEND")) {
        inSend(sender, toks);
    } else if (kind == QStringLiteral("RESUME")) {
        inResume(sender, toks);
    } else if (kind == QStringLiteral("ACCEPT")) {
        inAccept(sender, toks);
    } else if (kind == QStringLiteral("CHAT")) {
        inChat(sender, toks);
    }
}

void DccManager::inSend(const QString& sender, const QStringList& toks) {
    if (toks.size() < 5) {
        return;
    }
    const QString name = QFileInfo(toks.at(1)).fileName();
    const quint32 host = toks.at(2).toUInt();
    const quint16 port = static_cast<quint16>(toks.at(3).toUInt());
    // Clamp a negative/garbage offered size to 0 (treated as "write nothing").
    const qint64 size = std::max<qint64>(0, toks.at(4).toLongLong());
    const QString token = toks.size() >= 6 ? toks.at(5) : QString();

    // Passive reply to our own offer: peer listened, we connect out and send.
    if (!token.isEmpty() && awaitingTokens_.contains(token)) {
        if (port == 0) {
            return;
        }
        const int id = awaitingTokens_.take(token);
        DccTransfer* t = findById(id);
        if (t == nullptr || t->state == DccTransfer::State::Cancelled) {
            return;
        }
        t->state = DccTransfer::State::Connecting;
        emitChanged();
        auto* socket = new QTcpSocket(this);
        connect(socket, &QTcpSocket::connected, this, [this, id, socket]() { beginSend(id, socket); });
        connect(socket, &QTcpSocket::errorOccurred, this,
                [this, id](QAbstractSocket::SocketError) { finishTransfer(id, false); });
        socket->connectToHost(QHostAddress(host), port);
        return;
    }
    if (port == 0 && token.isEmpty()) {
        return;
    }

    // Anti-flood: ignore new offers once too many are already waiting.
    int pendingOffers = 0;
    for (const DccTransfer& t : transfers_) {
        if (t.direction == DccTransfer::Direction::Receive &&
            (t.state == DccTransfer::State::Pending ||
             t.state == DccTransfer::State::Offered ||
             t.state == DccTransfer::State::Resuming)) {
            ++pendingOffers;
        }
    }
    if (pendingOffers >= MaxPendingOffers) {
        emit status(
            QStringLiteral("DCC: ignoring file offer from %1 (too many pending).").arg(sender));
        return;
    }

    DccTransfer transfer;
    transfer.id = nextId_++;
    transfer.direction = DccTransfer::Direction::Receive;
    transfer.state = DccTransfer::State::Pending;
    transfer.peer = sender;
    transfer.fileName = name;
    transfer.host = host;
    transfer.port = port;
    transfer.size = size;
    transfer.localPath = destPath(name, size);
    transfers_.append(transfer);
    // Remember the token for a passive offer (port 0) so accept() can reply.
    if (!token.isEmpty()) {
        awaitingTokens_.insert(QStringLiteral("recv:%1").arg(token), transfer.id);
    }
    emitChanged();

    const QString low = sender.trimmed().toLower();
    if (acceptPolicy_ == QStringLiteral("all") ||
        (acceptPolicy_ == QStringLiteral("trusted") && trusted_.contains(low))) {
        emit status(QStringLiteral("DCC: auto-accepting %1 from %2").arg(name, sender));
        acceptTransfer(transfer.id);
    } else {
        emit status(QStringLiteral("DCC: file offer %1 from %2 - accept in Transfers")
                        .arg(name, sender));
    }
}

void DccManager::acceptTransfer(int id) {
    DccTransfer* t = findById(id);
    if (t == nullptr || t->direction != DccTransfer::Direction::Receive) {
        return;
    }
    if (t->state != DccTransfer::State::Pending) {
        return;
    }

    // Resume if a partial file of the right name exists.
    const QFileInfo info(t->localPath);
    if (info.exists() && info.size() > 0 && t->size > 0 && info.size() < t->size) {
        rt(id).startOffset = info.size();
        t->transferred = info.size();
        t->state = DccTransfer::State::Resuming;
        resuming_.insert(QStringLiteral("%1:%2:%3").arg(t->peer.toLower(), t->fileName).arg(t->port),
                         id);
        emitChanged();
        emit ctcpToSend(t->peer, QStringLiteral("RESUME %1 %2 %3")
                                     .arg(quoteName(t->fileName))
                                     .arg(t->port)
                                     .arg(info.size()));
        return; // wait for ACCEPT
    }

    if (t->port != 0) {
        // Active offer: connect to the sender.
        t->state = DccTransfer::State::Connecting;
        emitChanged();
        auto* socket = new QTcpSocket(this);
        connect(socket, &QTcpSocket::connected, this,
                [this, id, socket]() { beginReceive(id, socket); });
        connect(socket, &QTcpSocket::errorOccurred, this,
                [this, id](QAbstractSocket::SocketError) { finishTransfer(id, false); });
        socket->connectToHost(QHostAddress(t->host), t->port);
    } else {
        // Passive offer: we listen and reply with our ip/port + the token.
        QString token;
        for (auto it = awaitingTokens_.constBegin(); it != awaitingTokens_.constEnd(); ++it) {
            if (it.value() == id && it.key().startsWith(QStringLiteral("recv:"))) {
                token = it.key().mid(5);
                break;
            }
        }
        auto* server = new QTcpServer(this);
        const quint16 port = openListenPort(server);
        if (port == 0) {
            server->deleteLater();
            finishTransfer(id, false);
            return;
        }
        rt(id).server = server;
        t->state = DccTransfer::State::Offered;
        emitChanged();
        connect(server, &QTcpServer::newConnection, this, [this, server, id]() {
            QTcpSocket* socket = server->nextPendingConnection();
            server->close();
            beginReceive(id, socket);
        });
        emit ctcpToSend(t->peer, QStringLiteral("SEND %1 %2 %3 %4")
                                     .arg(quoteName(t->fileName),
                                          QString::number(QHostAddress(advertisedIp()).toIPv4Address()))
                                     .arg(port)
                                     .arg(t->size > 0 ? QString::number(t->size) + QStringLiteral(" ") + token
                                                      : token));
    }
}

void DccManager::beginReceive(int id, QTcpSocket* socket) {
    DccTransfer* t = findById(id);
    if (t == nullptr || t->state == DccTransfer::State::Cancelled) {
        socket->abort();
        socket->deleteLater();
        return;
    }
    Runtime& r = rt(id);
    r.socket = socket;
    r.file = new QFile(t->localPath, socket);
    const bool resume = r.startOffset > 0;
    if (!r.file->open(resume ? (QIODevice::ReadWrite) : QIODevice::WriteOnly)) {
        finishTransfer(id, false);
        return;
    }
    if (resume) {
        r.file->seek(r.startOffset);
        t->transferred = r.startOffset;
    }
    t->state = DccTransfer::State::Active;
    emitChanged();

    connect(socket, &QTcpSocket::readyRead, this, [this, id, socket]() {
        DccTransfer* tr = findById(id);
        Runtime& rr = rt(id);
        if (tr == nullptr || rr.file == nullptr) {
            return;
        }
        QByteArray data = socket->readAll();
        const qint64 writable = dccWritableChunk(tr->size, tr->transferred, data.size());
        if (writable <= 0) {
            // Nothing we should persist: a zero-length offer (complete it
            // cleanly) or already-complete/over-size data (drop it — a peer
            // can't make us write past the offered size).
            if (tr->size == 0) {
                finishTransfer(id, true);
            }
            return;
        }
        if (writable < data.size()) {
            data.truncate(static_cast<int>(writable));
        }
        rr.file->write(data);
        tr->transferred += data.size();
        const quint32 ack = qToBigEndian<quint32>(static_cast<quint32>(tr->transferred & 0xFFFFFFFF));
        socket->write(reinterpret_cast<const char*>(&ack), sizeof(ack));
        emitChanged();
        if (tr->size > 0 && tr->transferred >= tr->size) {
            finishTransfer(id, true);
        }
    });
    connect(socket, &QTcpSocket::errorOccurred, this,
            [this, id](QAbstractSocket::SocketError) { finishTransfer(id, false); });
    connect(socket, &QTcpSocket::disconnected, this, [this, id]() {
        DccTransfer* tr = findById(id);
        if (tr != nullptr && tr->state == DccTransfer::State::Active) {
            finishTransfer(id, tr->size == 0 || tr->transferred >= tr->size);
        }
    });
}

void DccManager::inResume(const QString& sender, const QStringList& toks) {
    if (toks.size() < 4) {
        return;
    }
    const quint16 port = static_cast<quint16>(toks.at(2).toUInt());
    const qint64 pos = toks.at(3).toLongLong();
    const QString token = toks.size() >= 5 ? toks.at(4) : QString();
    int id = -1;
    if (!token.isEmpty() && awaitingTokens_.contains(token)) {
        id = awaitingTokens_.value(token);
    } else if (sendByPort_.contains(port)) {
        id = sendByPort_.value(port);
    }
    DccTransfer* t = id >= 0 ? findById(id) : nullptr;
    if (t == nullptr) {
        return;
    }
    rt(id).startOffset = pos;
    t->transferred = pos;
    emit ctcpToSend(sender, QStringLiteral("ACCEPT %1 %2 %3")
                                .arg(quoteName(t->fileName))
                                .arg(port)
                                .arg(pos) +
                                (token.isEmpty() ? QString() : QStringLiteral(" %1").arg(token)));
    emitChanged();
}

void DccManager::inAccept(const QString& sender, const QStringList& toks) {
    if (toks.size() < 4) {
        return;
    }
    const QString name = QFileInfo(toks.at(1)).fileName();
    const quint16 port = static_cast<quint16>(toks.at(2).toUInt());
    const qint64 pos = toks.at(3).toLongLong();
    const QString token = toks.size() >= 5 ? toks.at(4) : QString();
    const QString key = token.isEmpty()
                            ? QStringLiteral("%1:%2:%3").arg(sender.toLower(), name).arg(port)
                            : token;
    int id = resuming_.value(key, resuming_.value(token, -1));
    DccTransfer* t = id >= 0 ? findById(id) : nullptr;
    if (t == nullptr) {
        return;
    }
    rt(id).startOffset = pos;
    t->transferred = pos;
    // The original offer was active (had a port) → connect now.
    if (t->port != 0) {
        t->state = DccTransfer::State::Connecting;
        emitChanged();
        auto* socket = new QTcpSocket(this);
        connect(socket, &QTcpSocket::connected, this,
                [this, id, socket]() { beginReceive(id, socket); });
        connect(socket, &QTcpSocket::errorOccurred, this,
                [this, id](QAbstractSocket::SocketError) { finishTransfer(id, false); });
        socket->connectToHost(QHostAddress(t->host), t->port);
    }
}

void DccManager::finishTransfer(int id, bool ok) {
    DccTransfer* t = findById(id);
    if (t == nullptr) {
        return;
    }
    if (t->state == DccTransfer::State::Done || t->state == DccTransfer::State::Failed ||
        t->state == DccTransfer::State::Cancelled) {
        return;
    }
    Runtime& r = rt(id);
    if (r.file != nullptr) {
        r.file->close();
    }
    if (r.socket != nullptr) {
        r.socket->disconnectFromHost();
    }
    if (r.server != nullptr) {
        r.server->close();
        r.server->deleteLater();
        r.server = nullptr;
    }
    t->state = ok ? DccTransfer::State::Done : DccTransfer::State::Failed;
    // Forget negotiation bookkeeping for this transfer.
    sendByPort_.remove(t->port);
    for (const QString& k : awaitingTokens_.keys(id)) {
        awaitingTokens_.remove(k);
    }
    for (const QString& k : resuming_.keys(id)) {
        resuming_.remove(k);
    }
    emitChanged();
    emit status(ok ? QStringLiteral("DCC: %1 %2")
                         .arg(t->direction == DccTransfer::Direction::Send
                                  ? QStringLiteral("sent")
                                  : QStringLiteral("received"),
                              t->fileName)
                   : QStringLiteral("DCC: %1 failed").arg(t->fileName));
}

void DccManager::cancelTransfer(int id) {
    DccTransfer* t = findById(id);
    if (t == nullptr) {
        return;
    }
    if (t->state == DccTransfer::State::Done || t->state == DccTransfer::State::Failed ||
        t->state == DccTransfer::State::Cancelled) {
        return;
    }
    Runtime& r = rt(id);
    if (r.socket != nullptr) {
        r.socket->abort();
    }
    if (r.server != nullptr) {
        r.server->close();
        r.server->deleteLater();
        r.server = nullptr;
    }
    if (r.file != nullptr) {
        r.file->close();
    }
    t->state = DccTransfer::State::Cancelled;
    emitChanged();
}

// ---- DCC CHAT ------------------------------------------------------------

void DccManager::offerChat(const QString& peer) {
    const QString key = peer.trimmed().toLower();
    auto* chat = new ChatRuntime();
    chat->info.id = nextId_++;
    chat->info.peer = peer;
    chats_.insert(key, chat);

    if (passive_) {
        chat->pendingToken = newToken();
        // Expire the pending token after 2 min so a stale/forged late reply can't
        // reactivate the offer (matches the Python 120s timeout).
        const QString expiringToken = chat->pendingToken;
        QTimer::singleShot(120000, this, [this, key, expiringToken]() {
            ChatRuntime* c = chats_.value(key);
            if (c != nullptr && c->pendingToken == expiringToken && c->socket == nullptr) {
                c->pendingToken.clear();
            }
        });
        emit ctcpToSend(peer, QStringLiteral("CHAT chat %1 0 %2")
                                  .arg(QString::number(QHostAddress(advertisedIp()).toIPv4Address()),
                                       chat->pendingToken));
    } else {
        chat->server = new QTcpServer(this);
        const quint16 port = openListenPort(chat->server);
        if (port == 0) {
            chat->info.state = DccChat::State::Closed;
            emit chatStateChanged(peer, static_cast<int>(chat->info.state));
            return;
        }
        connect(chat->server, &QTcpServer::newConnection, this, [this, key]() {
            ChatRuntime* c = chats_.value(key);
            if (c == nullptr) {
                return;
            }
            c->socket = c->server->nextPendingConnection();
            c->server->close();
            wireChatSocket(key);
            c->info.state = DccChat::State::Active;
            emit chatStateChanged(c->info.peer, static_cast<int>(c->info.state));
        });
        emit ctcpToSend(peer, QStringLiteral("CHAT chat %1 %2")
                                  .arg(QString::number(QHostAddress(advertisedIp()).toIPv4Address()))
                                  .arg(port));
    }
    emit chatStateChanged(peer, static_cast<int>(chat->info.state));
}

void DccManager::inChat(const QString& sender, const QStringList& toks) {
    if (toks.size() < 4) {
        return;
    }
    const quint32 host = toks.at(2).toUInt();
    const quint16 port = static_cast<quint16>(toks.at(3).toUInt());
    const QString token = toks.size() >= 5 ? toks.at(4) : QString();
    const QString key = sender.trimmed().toLower();

    // Passive reply to our own offer.
    ChatRuntime* existing = chats_.value(key);
    if (existing != nullptr && !existing->pendingToken.isEmpty() && existing->pendingToken == token) {
        if (port == 0) {
            return;
        }
        connectChat(*existing, host, port);
        return;
    }

    if (port == 0 && token.isEmpty()) {
        return;
    }
    // Anti-flood: ignore new chat offers once too many are pending/connecting.
    int pendingChats = 0;
    for (const ChatRuntime* c : std::as_const(chats_)) {
        if (c != nullptr && c->info.state == DccChat::State::Connecting) {
            ++pendingChats;
        }
    }
    if (pendingChats >= MaxPendingOffers && existing == nullptr) {
        emit status(
            QStringLiteral("DCC: ignoring chat offer from %1 (too many pending).").arg(sender));
        return;
    }
    auto* chat = new ChatRuntime();
    chat->info.id = nextId_++;
    chat->info.peer = sender;
    if (existing != nullptr) {
        delete chats_.take(key);
    }
    chats_.insert(key, chat);
    if (port != 0) {
        connectChat(*chat, host, port);
    } else {
        // Passive offer to us: listen and reply.
        chat->server = new QTcpServer(this);
        const quint16 myPort = openListenPort(chat->server);
        connect(chat->server, &QTcpServer::newConnection, this, [this, key]() {
            ChatRuntime* c = chats_.value(key);
            if (c == nullptr) {
                return;
            }
            c->socket = c->server->nextPendingConnection();
            c->server->close();
            wireChatSocket(key);
            c->info.state = DccChat::State::Active;
            emit chatStateChanged(c->info.peer, static_cast<int>(c->info.state));
        });
        emit ctcpToSend(sender, QStringLiteral("CHAT chat %1 %2 %3")
                                    .arg(QString::number(QHostAddress(advertisedIp()).toIPv4Address()))
                                    .arg(myPort)
                                    .arg(token));
    }
    emit chatStateChanged(sender, static_cast<int>(chat->info.state));
}

void DccManager::connectChat(ChatRuntime& chat, quint32 host, quint16 port) {
    const QString key = chat.info.peer.trimmed().toLower();
    chat.socket = new QTcpSocket(this);
    chat.info.state = DccChat::State::Connecting;
    wireChatSocket(key);
    chat.socket->connectToHost(QHostAddress(host), port);
}

void DccManager::wireChatSocket(const QString& key) {
    ChatRuntime* chat = chats_.value(key);
    if (chat == nullptr || chat->socket == nullptr) {
        return;
    }
    QTcpSocket* socket = chat->socket;
    connect(socket, &QTcpSocket::connected, this, [this, key]() {
        ChatRuntime* c = chats_.value(key);
        if (c != nullptr) {
            c->info.state = DccChat::State::Active;
            emit chatStateChanged(c->info.peer, static_cast<int>(c->info.state));
        }
    });
    connect(socket, &QTcpSocket::readyRead, this, [this, key, socket]() {
        ChatRuntime* c = chats_.value(key);
        if (c == nullptr) {
            return;
        }
        c->buffer.append(socket->readAll());
        if (c->buffer.size() > ChatBufferCap) {
            closeChat(c->info.peer);
            return;
        }
        int nl;
        while ((nl = c->buffer.indexOf('\n')) >= 0) {
            QByteArray lineBytes = c->buffer.left(nl);
            c->buffer.remove(0, nl + 1);
            if (lineBytes.endsWith('\r')) {
                lineBytes.chop(1);
            }
            if (lineBytes.size() > ChatLineCap) {
                closeChat(c->info.peer);
                return;
            }
            emit chatLineReceived(c->info.peer, QString::fromUtf8(lineBytes));
        }
    });
    connect(socket, &QTcpSocket::disconnected, this, [this, key]() {
        ChatRuntime* c = chats_.value(key);
        if (c != nullptr) {
            c->info.state = DccChat::State::Closed;
            emit chatStateChanged(c->info.peer, static_cast<int>(c->info.state));
        }
    });
}

void DccManager::sendChatLine(const QString& peer, const QString& line) {
    ChatRuntime* chat = chats_.value(peer.trimmed().toLower());
    if (chat != nullptr && chat->socket != nullptr &&
        chat->info.state == DccChat::State::Active) {
        chat->socket->write((line + QLatin1Char('\n')).toUtf8());
    }
}

void DccManager::closeChat(const QString& peer) {
    const QString key = peer.trimmed().toLower();
    ChatRuntime* chat = chats_.value(key);
    if (chat == nullptr) {
        return;
    }
    if (chat->socket != nullptr) {
        chat->socket->abort();
    }
    if (chat->server != nullptr) {
        chat->server->close();
    }
    chat->info.state = DccChat::State::Closed;
    emit chatStateChanged(chat->info.peer, static_cast<int>(chat->info.state));
    delete chats_.take(key);
}

} // namespace maxchat::ui
