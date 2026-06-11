#pragma once

#include "irc/IrcSession.h"

#include <QHash>
#include <QObject>
#include <QTimer>

class QSslSocket;

namespace maxchat::irc {

class IrcConnection final : public QObject {
  Q_OBJECT

public:
  struct ConnectConfig {
    QString host;
    int port = 6667;
    bool tls = false;
    QString nick = QStringLiteral("comicfan");
    QString username;
    QString realname;
    QString serverPassword;
    QString saslPassword;
    QString saslAccount;
    bool acceptInvalidCertificate = false;
    bool allowInsecureAuth = false;
    QStringList autojoin;
    int connectTimeoutMs = 15000;
    int registrationTimeoutMs = 15000;
    QString proxyType; // "", "none", "socks5", "http"
    QString proxyHost;
    int proxyPort = 1080;
    QString proxyUsername;
    QString proxyPassword;
  };

  explicit IrcConnection(QObject *parent = nullptr);
  ~IrcConnection() override;

  void connectTo(const ConnectConfig &config);
  void disconnectFromServer();
  void setIgnoreMasks(const QStringList &masks);
  void setCtcpVersion(bool hide, const QString &custom);

  [[nodiscard]] bool isConnected() const;
  [[nodiscard]] QString nick() const;
  [[nodiscard]] QString localAddress() const;
  [[nodiscard]] QHash<QString, QString> isupport() const;
  [[nodiscard]] const IrcSession &session() const;

  bool sendRaw(const QString &line);
  bool privmsg(const QString &target, const QString &text);
  bool action(const QString &target, const QString &text);
  bool ctcp(const QString &target, const QString &command,
            const QString &arg = {});
  bool measureLag();

signals:
  void connected();
  void registered();
  void disconnected(const QString &reason);
  void errorOccurred(const QString &message);
  void rawLine(const QString &direction, const QString &line);
  void systemText(const QString &line);
  void nickChanged(const QString &oldNick, const QString &newNick);
  void awayChanged(const QString &nick, bool away);
  void invited(const QString &sender, const QString &channel, const QString &mask);
  void dccRequest(const QString &sender, const QString &args);
  void ctcpSound(const QString &sender, const QString &target, const QString &file,
                 const QString &text);
  void userJoined(const QString &channel, const QString &nick);
  void userParted(const QString &channel, const QString &nick,
                  const QString &reason);
  void userQuit(const QString &nick, const QString &reason);
  void topicChanged(const QString &channel, const QString &topic);
  void namesReceived(const QString &channel, const QStringList &names);
  void namesEnd(const QString &channel);
  void listReply(const QString &channel, int users, const QString &topic);
  void listEnd();
  void kicked(const QString &channel, const QString &nick,
              const QString &reason);
  void replyText(const QString &line);
  void messageReceived(const QString &sender, const QString &target,
                       const QString &text, bool notice, bool action);
  void modeChanged(const QString &target, const QString &by,
                   const QString &modeLine);
  void channelModeIs(const QString &channel, const QString &modeLine);
  void isonReply(const QStringList &onlineNicks);
  void lagMeasured(double seconds);
  void banList(const QString &channel, const QString &mask,
               const QString &setter);
  void banListEnd(const QString &channel);

private:
  void retireSocket();
  void onSocketConnected(QSslSocket *socket);
  void onReadyRead(QSslSocket *socket);
  void onSocketDisconnected(QSslSocket *socket);
  void onSocketError(QSslSocket *socket);
  void onSslErrors(QSslSocket *socket);
  void onConnectTimeout();
  void onRegistrationTimeout();
  void queueIncomingLines(QSslSocket *socket);
  void emitTimedFailure(const QString &errorMessage,
                        const QString &disconnectReason);
  void emitTerminalDisconnect(const QString &reason);

  IrcSession session_;
  QSslSocket *socket_ = nullptr;
  QTimer connectTimer_;
  QTimer registrationTimer_;
  QByteArray buffer_;
  QString activeHost_;
  int activePort_ = 0;
  int activeRegistrationTimeoutMs_ = 15000;
  bool acceptInvalidCertificate_ = false;
  bool socketReady_ = false;
  bool terminalDisconnectEmitted_ = false;
};

} // namespace maxchat::irc
