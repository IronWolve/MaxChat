#include "ui/DccManager.h"

#include <QDir>
#include <QFileInfo>
#include <QtEndian>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QTcpServer>
#include <QTcpSocket>

namespace maxchat::ui {

namespace {

constexpr qint64 ChunkSize = 64 * 1024;

// Best-effort non-loopback IPv4 for outgoing DCC offers.
quint32 localIpv4() {
    const QList<QHostAddress> addresses = QNetworkInterface::allAddresses();
    for (const QHostAddress& address : addresses) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol && !address.isLoopback()) {
            return address.toIPv4Address();
        }
    }
    return QHostAddress(QHostAddress::LocalHost).toIPv4Address();
}

QString quoteName(const QString& name) {
    return name.contains(QLatin1Char(' ')) ? QStringLiteral("\"%1\"").arg(name) : name;
}

} // namespace

DccManager::DccManager(QObject* parent) : QObject(parent) {}

void DccManager::setDownloadDir(const QString& dir) {
    downloadDir_ = dir;
}

DccTransfer* DccManager::findById(int id) {
    for (DccTransfer& transfer : transfers_) {
        if (transfer.id == id) {
            return &transfer;
        }
    }
    return nullptr;
}

void DccManager::offerSend(const QString& peer, const QString& filePath) {
    const QFileInfo info(filePath);
    if (!info.isFile()) {
        emit status(QStringLiteral("DCC: file not found: %1").arg(filePath));
        return;
    }

    auto* server = new QTcpServer(this);
    if (!server->listen(QHostAddress::Any, 0)) {
        emit status(QStringLiteral("DCC: could not open a listening port."));
        server->deleteLater();
        return;
    }

    DccTransfer transfer;
    transfer.id = nextId_++;
    transfer.direction = DccTransfer::Direction::Send;
    transfer.state = DccTransfer::State::Pending;
    transfer.peer = peer;
    transfer.fileName = info.fileName();
    transfer.localPath = info.absoluteFilePath();
    transfer.port = server->serverPort();
    transfer.size = info.size();
    transfers_.append(transfer);
    const int id = transfer.id;
    emitChanged();

    connect(server, &QTcpServer::newConnection, this, [this, server, id]() {
        QTcpSocket* socket = server->nextPendingConnection();
        server->close();
        server->deleteLater();
        DccTransfer* t = findById(id);
        if (t == nullptr) {
            socket->deleteLater();
            return;
        }
        auto* file = new QFile(t->localPath, socket);
        if (!file->open(QIODevice::ReadOnly)) {
            t->state = DccTransfer::State::Failed;
            emitChanged();
            socket->deleteLater();
            return;
        }
        t->state = DccTransfer::State::Active;
        emitChanged();

        auto sendChunk = [this, socket, file, id]() {
            DccTransfer* tr = findById(id);
            if (tr == nullptr) {
                return;
            }
            while (socket->bytesToWrite() < ChunkSize && !file->atEnd()) {
                const QByteArray chunk = file->read(ChunkSize);
                socket->write(chunk);
                tr->transferred += chunk.size();
            }
            emitChanged();
            if (file->atEnd() && socket->bytesToWrite() == 0) {
                tr->state = DccTransfer::State::Done;
                emitChanged();
                emit status(QStringLiteral("DCC: sent %1").arg(tr->fileName));
                socket->disconnectFromHost();
            }
        };
        connect(socket, &QTcpSocket::bytesWritten, this, [sendChunk](qint64) { sendChunk(); });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        sendChunk();
    });

    // CTCP: DCC SEND <name> <ip-as-uint32> <port> <size>
    emit ctcpToSend(peer, QStringLiteral("SEND %1 %2 %3 %4")
                              .arg(quoteName(transfer.fileName))
                              .arg(localIpv4())
                              .arg(transfer.port)
                              .arg(transfer.size));
    emit status(QStringLiteral("DCC: offered %1 to %2").arg(transfer.fileName, peer));
}

void DccManager::handleIncoming(const QString& sender, const QString& args) {
    const QStringList parts = args.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.size() < 4 || parts.first().compare(QStringLiteral("SEND"), Qt::CaseInsensitive) != 0) {
        return; // only SEND offers are handled in this pass
    }
    DccTransfer transfer;
    transfer.id = nextId_++;
    transfer.direction = DccTransfer::Direction::Receive;
    transfer.state = DccTransfer::State::Pending;
    transfer.peer = sender;
    transfer.fileName = QFileInfo(parts.at(1)).fileName();
    transfer.host = parts.at(2).toUInt();
    transfer.port = static_cast<quint16>(parts.at(3).toUShort());
    transfer.size = parts.size() >= 5 ? parts.at(4).toLongLong() : 0;
    transfers_.append(transfer);
    emitChanged();
    emit status(QStringLiteral("DCC: %1 offers %2 (%3 bytes) - accept in Transfers")
                    .arg(sender, transfer.fileName)
                    .arg(transfer.size));
}

void DccManager::acceptTransfer(int id) {
    DccTransfer* transfer = findById(id);
    if (transfer == nullptr || transfer->direction != DccTransfer::Direction::Receive ||
        transfer->state != DccTransfer::State::Pending) {
        return;
    }
    startReceive(*transfer);
}

void DccManager::startReceive(DccTransfer& transfer) {
    const QString dir = downloadDir_.isEmpty() ? QDir::homePath() : downloadDir_;
    QDir().mkpath(dir);
    transfer.localPath = QDir(dir).filePath(transfer.fileName);

    auto* socket = new QTcpSocket(this);
    auto* file = new QFile(transfer.localPath, socket);
    if (!file->open(QIODevice::WriteOnly)) {
        transfer.state = DccTransfer::State::Failed;
        emitChanged();
        socket->deleteLater();
        return;
    }
    const int id = transfer.id;

    connect(socket, &QTcpSocket::readyRead, this, [this, socket, file, id]() {
        DccTransfer* t = findById(id);
        if (t == nullptr) {
            return;
        }
        const QByteArray data = socket->readAll();
        file->write(data);
        t->transferred += data.size();
        // DCC acks the running byte count as a big-endian uint32.
        const quint32 ack = qToBigEndian(static_cast<quint32>(t->transferred));
        socket->write(reinterpret_cast<const char*>(&ack), sizeof(ack));
        if (t->size > 0 && t->transferred >= t->size) {
            t->state = DccTransfer::State::Done;
            emit status(QStringLiteral("DCC: received %1").arg(t->fileName));
            socket->disconnectFromHost();
        }
        emitChanged();
    });
    connect(socket, &QTcpSocket::disconnected, this, [this, file, id]() {
        DccTransfer* t = findById(id);
        if (t != nullptr && t->state == DccTransfer::State::Active) {
            t->state = (t->size == 0 || t->transferred >= t->size) ? DccTransfer::State::Done
                                                                   : DccTransfer::State::Failed;
            emitChanged();
        }
        file->close();
    });

    transfer.state = DccTransfer::State::Active;
    emitChanged();
    socket->connectToHost(QHostAddress(transfer.host), transfer.port);
}

void DccManager::cancelTransfer(int id) {
    DccTransfer* transfer = findById(id);
    if (transfer == nullptr) {
        return;
    }
    if (transfer->state == DccTransfer::State::Pending ||
        transfer->state == DccTransfer::State::Active) {
        transfer->state = DccTransfer::State::Cancelled;
        emitChanged();
    }
}

} // namespace maxchat::ui
