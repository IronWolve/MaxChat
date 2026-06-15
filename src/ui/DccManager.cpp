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

// Strict numeric parsing: toUInt() without an ok-flag turned "70000" into
// port 4464 and "65536" into port 0 (misclassified as passive).
bool parsePort(const QString& tok, quint16* out, bool allowZero) {
    bool ok = false;
    const uint value = tok.toUInt(&ok);
    if (!ok || value > 65535 || (value == 0 && !allowZero)) {
        return false;
    }
    *out = static_cast<quint16>(value);
    return true;
}

bool parseHost(const QString& tok, quint32* out) {
    bool ok = false;
    const quint32 value = tok.toUInt(&ok);
    if (!ok || value == 0) { // 0.0.0.0 connects to localhost on Linux
        return false;
    }
    const QHostAddress addr(value);
    if (addr.isMulticast()) {
        return false;
    }
    *out = value; // loopback allowed: local DCC between two clients is legit
    return true;
}

// Windows-safe filename: QFileInfo::fileName() already blocks ../ traversal;
// this adds backslashes, drive colons, trailing dots/spaces, and the reserved
// device names (an offer literally named "CON" hits the console device).
QString sanitizeFileName(const QString& raw) {
    QString base = QFileInfo(raw).fileName();
    base.replace(QLatin1Char('\\'), QLatin1Char('_'));
    base.replace(QLatin1Char(':'), QLatin1Char('_'));
    while (base.endsWith(QLatin1Char('.')) || base.endsWith(QLatin1Char(' '))) {
        base.chop(1);
    }
    if (base.isEmpty()) {
        return QStringLiteral("file");
    }
    static const QStringList kReserved = {
        QStringLiteral("CON"),  QStringLiteral("PRN"),  QStringLiteral("AUX"),
        QStringLiteral("NUL"),  QStringLiteral("COM1"), QStringLiteral("COM2"),
        QStringLiteral("COM3"), QStringLiteral("COM4"), QStringLiteral("COM5"),
        QStringLiteral("COM6"), QStringLiteral("COM7"), QStringLiteral("COM8"),
        QStringLiteral("COM9"), QStringLiteral("LPT1"), QStringLiteral("LPT2"),
        QStringLiteral("LPT3"), QStringLiteral("LPT4"), QStringLiteral("LPT5"),
        QStringLiteral("LPT6"), QStringLiteral("LPT7"), QStringLiteral("LPT8"),
        QStringLiteral("LPT9")};
    if (kReserved.contains(base.section(QLatin1Char('.'), 0, 0).toUpper())) {
        base.prepend(QLatin1Char('_'));
    }
    return base;
}

bool sameNick(const QString& a, const QString& b) {
    return a.trimmed().compare(b.trimmed(), Qt::CaseInsensitive) == 0;
}

QString newToken() {
    // Use the system CSPRNG (like Python's secrets.token_hex(16)) — the token is
    // the only barrier stopping a third party from injecting a fake passive reply,
    // so use 128 bits (two 64-bit draws). 32 bits is brute-forceable on a LAN
    // within the offer's lifetime.
    const quint64 hi = QRandomGenerator::system()->generate64();
    const quint64 lo = QRandomGenerator::system()->generate64();
    return QStringLiteral("%1%2")
        .arg(hi, 16, 16, QLatin1Char('0'))
        .arg(lo, 16, 16, QLatin1Char('0'));
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

DccManager::DccManager(QObject* parent) : QObject(parent) {
    clock_.start();
    sweepTimer_.setInterval(30000);
    connect(&sweepTimer_, &QTimer::timeout, this, [this]() { sweepStale(); });
    sweepTimer_.start();
}

void DccManager::sweepStale() {
    constexpr qint64 kOfferTtlMs = 10 * 60 * 1000; // unaccepted offers expire
    constexpr qint64 kStallMs = 120 * 1000;        // no progress = dead peer
    const qint64 now = clock_.elapsed();
    QVector<int> expire;
    QVector<int> stall;
    for (const DccTransfer& t : std::as_const(transfers_)) {
        if (t.direction == DccTransfer::Direction::Receive &&
            t.state == DccTransfer::State::Pending && t.offeredAtMs > 0 &&
            now - t.offeredAtMs > kOfferTtlMs) {
            expire.append(t.id);
        } else if (t.state == DccTransfer::State::Active) {
            const Runtime& r = runtimes_.value(t.id);
            if (r.lastProgressMs > 0 && now - r.lastProgressMs > kStallMs) {
                stall.append(t.id);
            }
        }
    }
    for (const int id : expire) {
        cancelTransfer(id);
        emit status(QStringLiteral("DCC: file offer expired (not accepted)."));
    }
    for (const int id : stall) {
        finishTransfer(id, false);
        emit status(QStringLiteral("DCC: transfer stalled - no data for 2 minutes."));
    }
    for (auto it = pendingChatOffers_.begin(); it != pendingChatOffers_.end();) {
        if (now - it.value().atMs > 120000) {
            it = pendingChatOffers_.erase(it);
        } else {
            ++it;
        }
    }
}

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
    const QString base = sanitizeFileName(name);
    // A path is taken if a final file exists, a .part is in progress, or
    // another live receive already reserved it (two same-name offers used to
    // share one path and clobber each other).
    const auto reservedByTransfer = [this](const QString& path) {
        for (const DccTransfer& t : transfers_) {
            if (t.direction != DccTransfer::Direction::Receive || t.localPath != path) {
                continue;
            }
            if (t.state == DccTransfer::State::Pending ||
                t.state == DccTransfer::State::Offered ||
                t.state == DccTransfer::State::Resuming ||
                t.state == DccTransfer::State::Connecting ||
                t.state == DccTransfer::State::Active) {
                return true;
            }
        }
        return false;
    };
    const QString candidate = QDir(dir).filePath(base);
    // Resume only OUR OWN partial (.part). The old "any smaller same-name file
    // is a resume candidate" appended a stranger's bytes to unrelated files
    // the user already had.
    const QFileInfo part(candidate + QStringLiteral(".part"));
    if (part.exists() && part.size() > 0 && size > 0 && part.size() < size &&
        !reservedByTransfer(candidate)) {
        return candidate;
    }
    if (!QFileInfo::exists(candidate) && !part.exists() && !reservedByTransfer(candidate)) {
        return candidate;
    }
    const QFileInfo info(candidate);
    const QString stem = info.completeBaseName();
    const QString suffix =
        info.suffix().isEmpty() ? QString() : QStringLiteral(".%1").arg(info.suffix());
    for (int i = 1; i < 10000; ++i) {
        const QString tryPath =
            QDir(dir).filePath(QStringLiteral("%1.%2%3").arg(stem).arg(i).arg(suffix));
        if (!QFileInfo::exists(tryPath) &&
            !QFileInfo::exists(tryPath + QStringLiteral(".part")) &&
            !reservedByTransfer(tryPath)) {
            return tryPath;
        }
    }
    return candidate;
}

// ---- outgoing SEND -------------------------------------------------------

void DccManager::offerSend(const QString& peer, const QString& filePath) {
    if (!enabled_) { emit status(QStringLiteral("DCC: file transfers are disabled.")); return; }
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
        // Active DCC SEND has no token and the recipient's IP is not known in
        // advance (we advertise OURS and they dial in), so the connecting peer
        // cannot be verified here — whoever races to the ephemeral port first
        // gets the file. The listener is closed after one connection to shrink
        // the window; passive mode (token-bound) is preferred for untrusted nets.
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
    r.lastProgressMs = clock_.elapsed();
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
        // Receiver acks running byte count as big-endian uint32. The counter
        // wraps at 4 GiB — track the wraps so a >4 GiB send doesn't "complete"
        // on the first ack (old code compared against size & 0xFFFFFFFF).
        Runtime& rr = rt(id);
        rr.ackBuf.append(socket->readAll());
        rr.lastProgressMs = clock_.elapsed();
        DccTransfer* tr = findById(id);
        while (rr.ackBuf.size() >= 4) {
            const quint32 ack =
                qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(rr.ackBuf.constData()));
            rr.ackBuf.remove(0, 4);
            if (ack < rr.lastAck32) {
                rr.ackBase += Q_INT64_C(0x100000000);
            }
            rr.lastAck32 = ack;
            const qint64 ackTotal = rr.ackBase + ack;
            if (tr != nullptr && tr->size > 0 && ackTotal >= tr->size) {
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
        r.lastProgressMs = clock_.elapsed();
    }
    if (r.file->atEnd()) {
        r.allSent = true;
    }
    emitChanged();
}

// ---- incoming dispatch ---------------------------------------------------

void DccManager::handleIncoming(const QString& sender, const QString& args) {
    if (!enabled_) return;
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
    const QString name = sanitizeFileName(toks.at(1));
    quint32 host = 0;
    quint16 port = 0;
    const bool hostOk = parseHost(toks.at(2), &host);
    if (!parsePort(toks.at(3), &port, /*allowZero=*/true)) {
        return;
    }
    // Clamp a negative/garbage offered size to 0 (treated as "write nothing").
    const qint64 size = std::max<qint64>(0, toks.at(4).toLongLong());
    const QString token = toks.size() >= 6 ? toks.at(5) : QString();

    // Passive reply to our own offer: peer listened, we connect out and send.
    if (!token.isEmpty() && awaitingTokens_.contains(token)) {
        if (port == 0 || !hostOk) {
            return;
        }
        const int id = awaitingTokens_.value(token);
        DccTransfer* t = findById(id);
        if (t == nullptr || t->state == DccTransfer::State::Cancelled) {
            return;
        }
        // The token authorizes the SEND — but only from the nick we offered
        // to. Anyone observing the offer could otherwise echo the token with
        // their own ip/port and steal the file.
        if (!sameNick(sender, t->peer)) {
            return;
        }
        awaitingTokens_.remove(token);
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
    if (port != 0 && !hostOk) {
        return; // active offer with a garbage host (would connect to 0.0.0.0)
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
    transfer.offeredAtMs = clock_.elapsed();
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

    // Resume only our own partial download (<final>.part).
    const QFileInfo info(t->localPath + QStringLiteral(".part"));
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
            DccTransfer* t = findById(id);
            if (t == nullptr || t->state == DccTransfer::State::Cancelled) {
                socket->abort();
                socket->deleteLater();
                server->close();
                return;
            }
            // First-connection-wins is an attack surface: verify the peer is
            // the host from the offer before trusting the stream as the file.
            if (t->host != 0 && socket->peerAddress().toIPv4Address() != t->host) {
                socket->abort();
                socket->deleteLater();
                return; // keep listening for the real peer
            }
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
    r.lastProgressMs = clock_.elapsed();
    // Download into <final>.part; finishTransfer renames on success — resume
    // can then never target an unrelated pre-existing file.
    r.file = new QFile(t->localPath + QStringLiteral(".part"), socket);
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
        // A short/failed write (disk full, quota) must abort — otherwise we ACK
        // bytes that never hit disk, leaving a silently truncated file.
        const qint64 written = rr.file->write(data);
        if (written != data.size()) {
            emit status(QStringLiteral("DCC: write failed for %1 (disk full?) - aborting.")
                            .arg(tr->fileName));
            finishTransfer(id, false);
            return;
        }
        tr->transferred += data.size();
        rr.lastProgressMs = clock_.elapsed();
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
    quint16 port = 0;
    if (!parsePort(toks.at(2), &port, /*allowZero=*/true)) {
        return;
    }
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
    // Only the offer's peer may resume, only before the transfer started, and
    // only to a sane offset. A third party guessing the port could otherwise
    // reset an ACTIVE transfer mid-stream; pos past EOF made "success" out of
    // sending nothing; negative pos corrupted all progress accounting.
    if (!sameNick(sender, t->peer) || t->direction != DccTransfer::Direction::Send ||
        t->state != DccTransfer::State::Offered || pos <= 0 || pos >= t->size) {
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
    quint16 port = 0;
    if (!parsePort(toks.at(2), &port, /*allowZero=*/true)) {
        return;
    }
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
    // Sender must match, we must actually be resuming, and the offset must be
    // within [0, what we asked for]. A forged negative pos defeated the
    // disk-write cap (remaining = size - pos > size → near-unbounded write).
    if (!sameNick(sender, t->peer) || t->state != DccTransfer::State::Resuming ||
        pos < 0 || pos > rt(id).startOffset || pos > t->size) {
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
    if (ok && t->direction == DccTransfer::Direction::Receive) {
        // Promote the .part to its final name (path was reserved at offer time).
        const QString partPath = t->localPath + QStringLiteral(".part");
        if (QFile::exists(partPath) && !QFile::rename(partPath, t->localPath)) {
            emit status(QStringLiteral("DCC: saved as %1 (could not rename)").arg(partPath));
        }
    }
    if (r.socket != nullptr) {
        r.socket->disconnectFromHost();
        r.socket->deleteLater(); // also frees r.file (parented to the socket)
        r.socket = nullptr;
        r.file = nullptr;
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
    // Drop the (now empty) runtime entry — sockets/server/file were freed above,
    // and later signals early-return on the terminal state, so they won't touch
    // it. Leaving it accumulates one map slot per transfer over a long session.
    runtimes_.remove(id);
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
    if (r.file != nullptr) {
        r.file->close();
        r.file = nullptr; // freed with the socket below (parented)
    }
    if (r.socket != nullptr) {
        r.socket->abort();
        r.socket->deleteLater();
        r.socket = nullptr;
    }
    if (r.server != nullptr) {
        r.server->close();
        r.server->deleteLater();
        r.server = nullptr;
    }
    t->state = DccTransfer::State::Cancelled;
    // Cancelled negotiation state must die too: a stale passive token or
    // resume key could otherwise be replayed against this id later.
    sendByPort_.remove(t->port);
    for (const QString& k : awaitingTokens_.keys(id)) {
        awaitingTokens_.remove(k);
    }
    for (const QString& k : resuming_.keys(id)) {
        resuming_.remove(k);
    }
    runtimes_.remove(id); // empty after the cleanup above; don't accumulate
    emitChanged();
}

// ---- DCC CHAT ------------------------------------------------------------

void DccManager::offerChat(const QString& peer) {
    if (!enabled_) { emit status(QStringLiteral("DCC: file transfers are disabled.")); return; }
    const QString key = peer.trimmed().toLower();
    // If this peer already offered US a chat (held for the accept policy),
    // /dcc chat <nick> accepts it instead of crossing offers.
    if (pendingChatOffers_.contains(key)) {
        const PendingChatOffer offer = pendingChatOffers_.take(key);
        if (clock_.elapsed() - offer.atMs <= 120000) {
            acceptIncomingChat(peer, offer.host, offer.port, offer.token);
            return;
        }
        emit status(QStringLiteral("DCC: the chat offer from %1 expired - offering a new "
                                   "chat instead.")
                        .arg(peer));
    }
    // Re-offering to a peer that already has a session must not orphan the old
    // ChatRuntime (its server/socket would leak and a stale peer could still be
    // wired). Tear it down first, same as acceptIncomingChat does.
    if (ChatRuntime* existing = chats_.value(key); existing != nullptr) {
        teardownChat(existing);
        delete chats_.take(key);
    }

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

void DccManager::teardownChat(ChatRuntime* chat) {
    if (chat == nullptr) {
        return;
    }
    // The old replace-path deleted the struct but left socket/server alive
    // with lambdas keyed by nick — bytes from the OLD TCP peer landed in the
    // NEW session (chat hijack/poisoning).
    if (chat->socket != nullptr) {
        chat->socket->disconnect(this);
        chat->socket->abort();
        chat->socket->deleteLater();
        chat->socket = nullptr;
    }
    if (chat->server != nullptr) {
        chat->server->disconnect(this);
        chat->server->close();
        chat->server->deleteLater();
        chat->server = nullptr;
    }
}

void DccManager::inChat(const QString& sender, const QStringList& toks) {
    if (toks.size() < 4) {
        return;
    }
    quint32 host = 0;
    quint16 port = 0;
    const bool hostOk = parseHost(toks.at(2), &host);
    if (!parsePort(toks.at(3), &port, /*allowZero=*/true)) {
        return;
    }
    const QString token = toks.size() >= 5 ? toks.at(4) : QString();
    const QString key = sender.trimmed().toLower();

    // Passive reply to our own offer.
    ChatRuntime* existing = chats_.value(key);
    if (existing != nullptr && !existing->pendingToken.isEmpty() &&
        existing->pendingToken == token) {
        if (port == 0 || !hostOk) {
            return;
        }
        connectChat(*existing, host, port);
        return;
    }

    if (port == 0 && token.isEmpty()) {
        return;
    }
    if (port != 0 && !hostOk) {
        return;
    }
    // An incoming chat offer follows the SAME accept policy as files. The old
    // code auto-connected for everyone: any user could CTCP a CHAT offer and
    // every client with DCC on connected back, disclosing its real IP.
    const QString low = sender.trimmed().toLower();
    const bool autoAccept = acceptPolicy_ == QStringLiteral("all") ||
                            (acceptPolicy_ == QStringLiteral("trusted") && trusted_.contains(low));
    if (!autoAccept) {
        if (pendingChatOffers_.size() >= MaxPendingOffers && !pendingChatOffers_.contains(low)) {
            return;
        }
        pendingChatOffers_.insert(low, {host, port, token, clock_.elapsed()});
        emit status(QStringLiteral("DCC: chat offer from %1 - use /dcc chat %1 to accept "
                                   "(expires in 2 minutes).")
                        .arg(sender));
        return;
    }
    acceptIncomingChat(sender, host, port, token);
}

void DccManager::acceptIncomingChat(const QString& sender, quint32 host, quint16 port,
                                    const QString& token) {
    const QString key = sender.trimmed().toLower();
    // Anti-flood: ignore new chat offers once too many are pending/connecting.
    int pendingChats = 0;
    for (const ChatRuntime* c : std::as_const(chats_)) {
        if (c != nullptr && c->info.state == DccChat::State::Connecting) {
            ++pendingChats;
        }
    }
    ChatRuntime* existing = chats_.value(key);
    if (pendingChats >= MaxPendingOffers && existing == nullptr) {
        emit status(
            QStringLiteral("DCC: ignoring chat offer from %1 (too many pending).").arg(sender));
        return;
    }
    auto* chat = new ChatRuntime();
    chat->info.id = nextId_++;
    chat->info.peer = sender;
    chat->expectedHost = host;
    if (existing != nullptr) {
        teardownChat(existing);
        delete chats_.take(key);
    }
    chats_.insert(key, chat);
    if (port != 0) {
        connectChat(*chat, host, port);
    } else {
        // Passive offer to us: listen and reply.
        chat->server = new QTcpServer(this);
        const quint16 myPort = openListenPort(chat->server);
        if (myPort == 0) {
            // Port range exhausted: advertising port 0 would strand the peer
            // and leave a dead Connecting entry consuming the flood budget.
            chat->info.state = DccChat::State::Closed;
            emit chatStateChanged(sender, static_cast<int>(chat->info.state));
            teardownChat(chat);
            delete chats_.take(key);
            emit status(QStringLiteral("DCC: could not open a listening port for chat."));
            return;
        }
        connect(chat->server, &QTcpServer::newConnection, this, [this, key]() {
            ChatRuntime* c = chats_.value(key);
            if (c == nullptr) {
                return;
            }
            QTcpSocket* socket = c->server->nextPendingConnection();
            // Verify the connecting peer is the host from the offer.
            if (c->expectedHost != 0 &&
                socket->peerAddress().toIPv4Address() != c->expectedHost) {
                socket->abort();
                socket->deleteLater();
                return; // keep listening
            }
            c->socket = socket;
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
    if (!enabled_) return;
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
    chat->info.state = DccChat::State::Closed;
    emit chatStateChanged(chat->info.peer, static_cast<int>(chat->info.state));
    teardownChat(chat);
    delete chats_.take(key);
}

} // namespace maxchat::ui
