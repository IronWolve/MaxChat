#include "irc/IrcConnection.h"

#include <QAbstractSocket>
#include <QHostAddress>
#include <QPointer>
#include <QSslError>
#include <QNetworkProxy>
#include <QSslSocket>
#include <QTimer>

namespace maxchat::irc {

namespace {

// An IRCv3 line is the 512-byte message plus up to ~8KB of message tags
// (server-time, account-tag, …), so the incoming cap must be far larger than
// the 512-byte *send* cap. Matches the Python client (8192 line / 65536 buffer).
// IRCv3 allows 8191 bytes of tags plus the 512-byte message — tag-heavy
// servers legitimately exceed the old 8 KB cap.
constexpr qsizetype MaxIncomingLineBytes = 10240;
constexpr qsizetype MaxPendingBytes = 65536;
// Process at most this many lines per event-loop turn; defer the rest so a flood
// or netsplit burst can't freeze the UI (Python drains 100 lines per tick).
constexpr int MaxLinesPerTick = 100;
// Unconditional ceiling for buffered-but-unprocessed bytes: a server flooding
// complete lines faster than the throttled drain must not grow memory forever.
constexpr qsizetype MaxBacklogBytes = 4 * 1024 * 1024;
constexpr int IdleProbeAfterMs = 90000; // silence before we PING
constexpr int IdleAbortAfterMs = 30000; // silence after the PING before abort

} // namespace

IrcConnection::IrcConnection(QObject *parent) : QObject(parent) {
  idleTimer_.setSingleShot(true);
  idleProbeTimer_.setSingleShot(true);
  connect(&idleTimer_, &QTimer::timeout, this, &IrcConnection::onIdleTimeout);
  connect(&idleProbeTimer_, &QTimer::timeout, this,
          &IrcConnection::onIdleProbeTimeout);
  connect(&session_, &IrcSession::connected, this, &IrcConnection::connected);
  connect(&session_, &IrcSession::registered, this, [this]() {
    registrationTimer_.stop();
    emit registered();
  });
  connect(&session_, &IrcSession::errorOccurred, this,
          &IrcConnection::errorOccurred);
  connect(&session_, &IrcSession::rawLine, this, &IrcConnection::rawLine);
  connect(&session_, &IrcSession::systemText, this, &IrcConnection::systemText);
  connect(&session_, &IrcSession::nickChanged, this,
          &IrcConnection::nickChanged);
  connect(&session_, &IrcSession::awayChanged, this,
          &IrcConnection::awayChanged);
  connect(&session_, &IrcSession::invited, this, &IrcConnection::invited);
  connect(&session_, &IrcSession::dccRequest, this, &IrcConnection::dccRequest);
  connect(&session_, &IrcSession::ctcpSound, this, &IrcConnection::ctcpSound);
  connect(&session_, &IrcSession::mcDataReceived, this,
          &IrcConnection::mcDataReceived);
  connect(&session_, &IrcSession::userJoined, this, &IrcConnection::userJoined);
  connect(&session_, &IrcSession::userParted, this, &IrcConnection::userParted);
  connect(&session_, &IrcSession::userQuit, this, &IrcConnection::userQuit);
  connect(&session_, &IrcSession::topicChanged, this,
          &IrcConnection::topicChanged);
  connect(&session_, &IrcSession::namesReceived, this,
          &IrcConnection::namesReceived);
  connect(&session_, &IrcSession::namesEnd, this, &IrcConnection::namesEnd);
  connect(&session_, &IrcSession::listReply, this, &IrcConnection::listReply);
  connect(&session_, &IrcSession::listEnd, this, &IrcConnection::listEnd);
  connect(&session_, &IrcSession::kicked, this, &IrcConnection::kicked);
  connect(&session_, &IrcSession::replyText, this, &IrcConnection::replyText);
  connect(&session_, &IrcSession::messageReceived, this,
          &IrcConnection::messageReceived);
  connect(&session_, &IrcSession::modeChanged, this,
          &IrcConnection::modeChanged);
  connect(&session_, &IrcSession::channelModeIs, this,
          &IrcConnection::channelModeIs);
  connect(&session_, &IrcSession::isonReply, this, &IrcConnection::isonReply);
  connect(&session_, &IrcSession::lagMeasured, this,
          &IrcConnection::lagMeasured);
  connect(&session_, &IrcSession::banList, this, &IrcConnection::banList);
  connect(&session_, &IrcSession::banListEnd, this, &IrcConnection::banListEnd);

  connectTimer_.setSingleShot(true);
  connect(&connectTimer_, &QTimer::timeout, this,
          &IrcConnection::onConnectTimeout);
  registrationTimer_.setSingleShot(true);
  connect(&registrationTimer_, &QTimer::timeout, this,
          &IrcConnection::onRegistrationTimeout);
}

IrcConnection::~IrcConnection() { retireSocket(); }

void IrcConnection::connectTo(const ConnectConfig &config) {
  retireSocket();

  activeHost_ = config.host;
  activePort_ = config.port;
  activeRegistrationTimeoutMs_ = config.registrationTimeoutMs;
  acceptInvalidCertificate_ = config.acceptInvalidCertificate;
  socketReady_ = false;
  terminalDisconnectEmitted_ = false;
  buffer_.clear();

  IrcSession::Registration registration;
  registration.nick =
      config.nick.isEmpty() ? QStringLiteral("comicfan") : config.nick;
  registration.username = config.username;
  registration.realname = config.realname;
  registration.serverPassword = config.serverPassword;
  registration.saslPassword = config.saslPassword;
  registration.saslAccount = config.saslAccount;
  registration.tls = config.tls;
  registration.allowInsecureAuth = config.allowInsecureAuth;
  registration.autojoin = config.autojoin;
  session_.configureRegistration(registration);
  session_.setConnected(false);

  auto *socket = new QSslSocket(this);
  const QString proxyType = config.proxyType.trimmed().toLower();
  if (!proxyType.isEmpty() && proxyType != QStringLiteral("none")) {
    // A configured proxy must be valid — fail loudly rather than silently
    // connecting direct (Python's _proxy_from_config raises here).
    const QString proxyHost = config.proxyHost.trimmed();
    const bool knownType = proxyType == QStringLiteral("socks5") ||
                           proxyType == QStringLiteral("http");
    const bool validPort = config.proxyPort >= 1 && config.proxyPort <= 65535;
    if (!knownType || proxyHost.isEmpty() || !validPort) {
      socket->deleteLater();
      const QString why =
          !knownType ? QStringLiteral("unsupported proxy type '%1'")
                           .arg(config.proxyType.trimmed())
          : proxyHost.isEmpty() ? QStringLiteral("proxy host is required")
                                : QStringLiteral("proxy port must be 1-65535");
      emit errorOccurred(QStringLiteral("Proxy configuration error: %1").arg(why));
      emit disconnected(QStringLiteral("proxy configuration error"));
      return;
    }
    QNetworkProxy proxy(proxyType == QStringLiteral("socks5")
                            ? QNetworkProxy::Socks5Proxy
                            : QNetworkProxy::HttpProxy,
                        proxyHost, static_cast<quint16>(config.proxyPort));
    if (!config.proxyUsername.isEmpty()) {
      proxy.setUser(config.proxyUsername);
    }
    if (!config.proxyPassword.isEmpty()) {
      proxy.setPassword(config.proxyPassword);
    }
    socket->setProxy(proxy);
  }
  socket_ = socket;
  session_.setWriter([this](const QByteArray &payload) -> qsizetype {
    return socket_ == nullptr ? -1 : socket_->write(payload);
  });

  connect(socket, &QSslSocket::readyRead, this,
          [this, socket]() { onReadyRead(socket); });
  connect(socket, &QSslSocket::disconnected, this,
          [this, socket]() { onSocketDisconnected(socket); });
  connect(
      socket, &QAbstractSocket::errorOccurred, this,
      [this, socket](QAbstractSocket::SocketError) { onSocketError(socket); });
  connect(socket, &QSslSocket::sslErrors, this,
          [this, socket](const QList<QSslError> &) { onSslErrors(socket); });

  if (config.tls) {
    connect(socket, &QSslSocket::encrypted, this,
            [this, socket]() { onSocketConnected(socket); });
    socket->connectToHostEncrypted(config.host, config.port);
  } else {
    connect(socket, &QSslSocket::connected, this,
            [this, socket]() { onSocketConnected(socket); });
    socket->connectToHost(config.host, config.port);
  }

  if (config.connectTimeoutMs > 0) {
    connectTimer_.start(config.connectTimeoutMs);
  }
}

void IrcConnection::disconnectFromServer() {
  if (socket_ != nullptr) {
    socket_->abort();
  }
}

void IrcConnection::setIgnoreMasks(const QStringList &masks) {
  session_.setIgnoreMasks(masks);
}

void IrcConnection::setCtcpVersion(bool hide, const QString &custom) {
  session_.setCtcpVersion(hide, custom);
}

void IrcConnection::setCtcpOptions(bool respondPing, bool respondTime,
                                   bool respondClientInfo) {
  session_.setCtcpOptions(respondPing, respondTime, respondClientInfo);
}

bool IrcConnection::isConnected() const {
  return socket_ != nullptr &&
         socket_->state() == QAbstractSocket::ConnectedState;
}

QString IrcConnection::nick() const { return session_.nick(); }

QString IrcConnection::localAddress() const {
  if (socket_ != nullptr) {
    const QHostAddress addr = socket_->localAddress();
    if (addr.protocol() == QAbstractSocket::IPv4Protocol) {
      return addr.toString();
    }
  }
  return {};
}

QHash<QString, QString> IrcConnection::isupport() const {
  return session_.isupport();
}

const IrcSession &IrcConnection::session() const { return session_; }

bool IrcConnection::sendRaw(const QString &line) {
  return session_.sendRaw(line);
}

bool IrcConnection::privmsg(const QString &target, const QString &text) {
  return session_.privmsg(target, text);
}

bool IrcConnection::action(const QString &target, const QString &text) {
  return session_.action(target, text);
}

bool IrcConnection::ctcp(const QString &target, const QString &command,
                         const QString &arg) {
  return session_.ctcp(target, command, arg);
}

bool IrcConnection::mcData(const QString &target, const QString &service,
                           const QString &verb, const QString &payload,
                           const bool notice) {
  return session_.mcData(target, service, verb, payload, notice);
}

bool IrcConnection::measureLag() { return session_.measureLag(); }

void IrcConnection::retireSocket() {
  connectTimer_.stop();
  registrationTimer_.stop();
  idleTimer_.stop();
  idleProbeTimer_.stop();
  session_.setConnected(false);
  socketReady_ = false;
  buffer_.clear();

  if (socket_ == nullptr) {
    return;
  }

  QSslSocket *oldSocket = socket_;
  socket_ = nullptr;
  oldSocket->disconnect(this);
  oldSocket->abort();
  oldSocket->deleteLater();
}

void IrcConnection::onSocketConnected(QSslSocket *socket) {
  if (socket != socket_) {
    return;
  }
  connectTimer_.stop();
  socketReady_ = true;
  session_.setConnected(true);
  session_.onConnected();
  idleTimer_.start(IdleProbeAfterMs);
  if (!session_.isRegistered() && activeRegistrationTimeoutMs_ > 0) {
    registrationTimer_.start(activeRegistrationTimeoutMs_);
  }
}

void IrcConnection::onReadyRead(QSslSocket *socket) {
  if (socket != socket_) {
    return;
  }
  // Any data at all feeds the read-idle watchdog.
  idleProbeTimer_.stop();
  idleTimer_.start(IdleProbeAfterMs);
  buffer_.append(socket->readAll());
  if (buffer_.size() > MaxBacklogBytes) {
    // Complete lines arriving faster than the throttled drain forever =
    // protocol flood; cap memory and drop the connection.
    buffer_.clear();
    emit errorOccurred(
        QStringLiteral("IRC server is flooding; disconnecting"));
    socket->abort();
    return;
  }
  queueIncomingLines(socket);
  // Only abort on a single oversized *incomplete* line — not on a backlog of
  // complete lines still being drained in throttled batches (see MaxLinesPerTick).
  if (buffer_.size() > MaxPendingBytes && !buffer_.contains("\r\n")) {
    buffer_.clear();
    emit errorOccurred(
        QStringLiteral("IRC server sent an oversized line; disconnecting"));
    socket->abort();
  }
}

void IrcConnection::onIdleTimeout() {
  if (socket_ == nullptr || !socketReady_) {
    return;
  }
  // Quiet link — ask for proof of life. Any inbound data cancels the probe.
  session_.sendRaw(QStringLiteral("PING :maxchat-keepalive"));
  idleProbeTimer_.start(IdleAbortAfterMs);
}

void IrcConnection::onIdleProbeTimeout() {
  if (socket_ == nullptr || !socketReady_) {
    return;
  }
  emitTimedFailure(
      QStringLiteral("no data from server for %1 seconds")
          .arg((IdleProbeAfterMs + IdleAbortAfterMs) / 1000),
      QStringLiteral("connection timed out"));
}

void IrcConnection::onSocketDisconnected(QSslSocket *socket) {
  if (socket != socket_) {
    return;
  }
  connectTimer_.stop();
  registrationTimer_.stop();
  session_.setConnected(false);
  socketReady_ = false;
  const QString reason = socket->error() == QAbstractSocket::UnknownSocketError
                             ? QStringLiteral("connection closed")
                             : socket->errorString();
  emitTerminalDisconnect(reason);
}

void IrcConnection::onSocketError(QSslSocket *socket) {
  if (socket != socket_) {
    return;
  }
  if (terminalDisconnectEmitted_) {
    return;
  }
  const QString message = socket->errorString();
  emit errorOccurred(message);
  if (!socketReady_ || socket->state() != QAbstractSocket::ConnectedState) {
    connectTimer_.stop();
    registrationTimer_.stop();
    session_.setConnected(false);
    socketReady_ = false;
    emitTerminalDisconnect(message);
  }
}

void IrcConnection::onSslErrors(QSslSocket *socket) {
  if (socket != socket_) {
    return;
  }
  if (acceptInvalidCertificate_) {
    socket->ignoreSslErrors();
    emit systemText(
        QStringLiteral("Accepted the server certificate despite TLS errors."));
  } else {
    emit systemText(QStringLiteral("TLS certificate rejected."));
  }
}

void IrcConnection::onConnectTimeout() {
  if (socket_ == nullptr || socketReady_) {
    return;
  }
  const QString message =
      QStringLiteral("connection timed out while connecting to %1:%2")
          .arg(activeHost_)
          .arg(activePort_);
  emitTimedFailure(message, QStringLiteral("connection timed out"));
}

void IrcConnection::onRegistrationTimeout() {
  if (socket_ == nullptr || session_.isRegistered()) {
    return;
  }
  const QString message =
      QStringLiteral("registration timed out after connecting to %1:%2")
          .arg(activeHost_)
          .arg(activePort_);
  emitTimedFailure(message, QStringLiteral("registration timed out"));
}

void IrcConnection::queueIncomingLines(QSslSocket *socket) {
  // Re-entrancy guard: handleLine can spin a nested event loop (e.g. a script
  // doing a synchronous fetch), during which readyRead fires again. A nested
  // drain would invalidate the outer pass's read position — let the nested
  // call just append bytes; this pass (or the re-arm below) picks them up.
  if (draining_) {
    return;
  }
  draining_ = true;
  int handled = 0;
  qsizetype pos = 0; // consume via an index; one remove() at the end is O(n)
  while (socket == socket_ && handled < MaxLinesPerTick) {
    const qsizetype lineEnd = buffer_.indexOf("\r\n", pos);
    if (lineEnd < 0) {
      break;
    }
    const QByteArray chunk = buffer_.mid(pos, lineEnd - pos);
    pos = lineEnd + 2;
    if (chunk.isEmpty()) {
      continue;
    }
    if (chunk.size() > MaxIncomingLineBytes) {
      emit systemText(QStringLiteral("Dropped oversized IRC line from server"));
      continue;
    }
    ++handled;
    session_.handleLine(QString::fromUtf8(chunk));
  }
  if (socket == socket_ && pos > 0) {
    buffer_.remove(0, pos);
  }
  draining_ = false;
  // More complete lines still buffered → keep draining after yielding so a big
  // burst doesn't block the UI thread.
  if (socket == socket_ && buffer_.contains("\r\n")) {
    const QPointer<QSslSocket> guarded(socket);
    QTimer::singleShot(0, this, [this, guarded]() {
      if (!guarded.isNull() && guarded == socket_) {
        queueIncomingLines(guarded);
      }
    });
  }
}

void IrcConnection::emitTerminalDisconnect(const QString &reason) {
  if (terminalDisconnectEmitted_) {
    return;
  }
  terminalDisconnectEmitted_ = true;
  emit disconnected(reason);
}

void IrcConnection::emitTimedFailure(const QString &errorMessage,
                                     const QString &disconnectReason) {
  emit errorOccurred(errorMessage);
  terminalDisconnectEmitted_ = true;
  // Fully retire the socket (stop timers, disconnect signals, abort, delete) —
  // leaving an aborted socket in socket_ keeps dead-object hazards alive.
  retireSocket();
  emit disconnected(disconnectReason);
}

} // namespace maxchat::irc
