#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>

namespace maxchat::irc {

constexpr qsizetype IrcMaxWireBytes = 512;

class IrcSession final : public QObject {
  Q_OBJECT

public:
  struct Registration {
    QString nick;
    QString username;
    QString realname;
    QString serverPassword;
    QString saslPassword;
    QString saslAccount;
    bool tls = false;
    bool allowInsecureAuth = false;
    QStringList autojoin;
  };

  using Writer = std::function<qsizetype(const QByteArray &)>;

  explicit IrcSession(QObject *parent = nullptr);

  void configureRegistration(const Registration &registration);
  void setConnected(bool connected);
  void setWriter(Writer writer);
  void setIgnoreMasks(const QStringList &masks);
  void setCtcpVersion(bool hide, const QString &custom);

  [[nodiscard]] QString nick() const;
  [[nodiscard]] bool isRegistered() const;
  [[nodiscard]] QHash<QString, QString> isupport() const;

  bool sendRaw(const QString &line);
  bool privmsg(const QString &target, const QString &text);
  bool action(const QString &target, const QString &text);
  bool ctcp(const QString &target, const QString &command,
            const QString &arg = {});
  bool join(const QString &channel);
  bool measureLag();

  void onConnected();
  void handleLine(const QString &line);

signals:
  void connected();
  void registered();
  void errorOccurred(const QString &message);
  void rawLine(const QString &direction, const QString &line);
  void systemText(const QString &line);
  void nickChanged(const QString &oldNick, const QString &newNick);
  void awayChanged(const QString &nick, bool away);
  void invited(const QString &sender, const QString &channel, const QString &mask);
  void dccRequest(const QString &sender, const QString &args);
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
  [[nodiscard]] bool passwordAuthAllowed();
  [[nodiscard]] QString alternateNick(int attempt) const;
  [[nodiscard]] bool isIgnoredPrefix(const QString &prefix) const;
  void handleIsupport(const QStringList &params);
  void handleCap(const QStringList &params, const QString &trailing);

  Writer writer_;
  bool socketConnected_ = false;
  QString nick_;
  QString baseNick_;
  int nickTry_ = 0;
  bool registered_ = false;
  QString username_;
  QString realname_;
  QString serverPassword_;
  QString saslPassword_;
  QString saslAccount_;
  bool tls_ = false;
  bool allowInsecureAuth_ = false;
  bool insecureAuthWarned_ = false;
  bool authed_ = false;
  bool hideVersion_ = false;
  QString ctcpVersion_;
  QStringList autojoin_;
  QSet<QString> serverCaps_;
  QHash<QString, QString> isupport_;
  QString lagToken_;
  QElapsedTimer lagTimer_;
  QList<QRegularExpression> ignorePatterns_;
};

} // namespace maxchat::irc
