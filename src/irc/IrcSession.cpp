#include "irc/IrcSession.h"

#include "irc/IrcMessage.h"
#include "irc/IrcRedaction.h"

#include <QByteArray>
#include <QDateTime>

#include <algorithm>

namespace maxchat::irc {

namespace {

QString ctcpWrap(const QString &body) {
  return QString(QLatin1Char('\x01')) + body + QString(QLatin1Char('\x01'));
}

struct CtcpMessage {
  bool valid = false;
  QString command;
  QString args;
};

CtcpMessage parseCtcpMessage(const QString &text) {
  if (text.size() < 2 || !text.startsWith(QLatin1Char('\x01')) ||
      !text.endsWith(QLatin1Char('\x01'))) {
    return {};
  }

  const QString body = text.mid(1, text.size() - 2);
  const int space = body.indexOf(QLatin1Char(' '));
  const QString command =
      (space < 0 ? body : body.left(space)).trimmed().toUpper();
  if (command.isEmpty()) {
    return {};
  }

  return {.valid = true,
          .command = command,
          .args = space < 0 ? QString() : body.mid(space + 1)};
}

QString ctcpBody(const QString &command, const QString &args = {}) {
  return args.isEmpty() ? command : QStringLiteral("%1 %2").arg(command, args);
}

QString ctcpSummary(const QString &kind, const QString &from,
                    const CtcpMessage &ctcp) {
  const QString suffix =
      ctcp.args.isEmpty() ? QString() : QStringLiteral(": %1").arg(ctcp.args);
  return QStringLiteral("[ctcp] %1 %2 from %3%4")
      .arg(ctcp.command, kind, from, suffix);
}

} // namespace

IrcSession::IrcSession(QObject *parent) : QObject(parent) {}

void IrcSession::configureRegistration(const Registration &registration) {
  nick_ = registration.nick;
  baseNick_ = registration.nick;
  nickTry_ = 0;
  registered_ = false;
  username_ = registration.username.isEmpty() ? registration.nick
                                              : registration.username;
  realname_ = registration.realname.isEmpty() ? registration.nick
                                              : registration.realname;
  serverPassword_ = registration.serverPassword;
  saslPassword_ = registration.saslPassword;
  saslAccount_ = registration.saslAccount.isEmpty() ? registration.nick
                                                    : registration.saslAccount;
  tls_ = registration.tls;
  allowInsecureAuth_ = registration.allowInsecureAuth;
  insecureAuthWarned_ = false;
  authed_ = false;
  autojoin_ = registration.autojoin;
  serverCaps_.clear();
  isupport_.clear();
  lagToken_.clear();
}

void IrcSession::setConnected(bool connected) { socketConnected_ = connected; }

void IrcSession::setWriter(Writer writer) { writer_ = std::move(writer); }

void IrcSession::setIgnoreMasks(const QStringList &masks) {
  ignorePatterns_.clear();
  for (const QString &mask : masks) {
    const QString trimmed = mask.trimmed();
    if (trimmed.isEmpty()) {
      continue;
    }
    ignorePatterns_.append(QRegularExpression(
        QRegularExpression::wildcardToRegularExpression(trimmed.toLower()),
        QRegularExpression::CaseInsensitiveOption));
  }
}

void IrcSession::setCtcpVersion(bool hide, const QString &custom) {
  hideVersion_ = hide;
  ctcpVersion_ = custom.trimmed();
}

QString IrcSession::nick() const { return nick_; }

bool IrcSession::isRegistered() const { return registered_; }

QHash<QString, QString> IrcSession::isupport() const { return isupport_; }

bool IrcSession::sendRaw(const QString &line) {
  if (!socketConnected_ || !writer_) {
    return false;
  }

  const QByteArray payload = (line + QStringLiteral("\r\n")).toUtf8();
  if (payload.size() > IrcMaxWireBytes) {
    emit errorOccurred(
        QStringLiteral("IRC line is too long; message not sent"));
    return false;
  }

  const qsizetype written = writer_(payload);
  if (written != payload.size()) {
    emit errorOccurred(QStringLiteral("Socket write failed; message not sent"));
    return false;
  }

  emit rawLine(QStringLiteral(">>"), redactLine(line));
  return true;
}

bool IrcSession::privmsg(const QString &target, const QString &text) {
  return sendRaw(QStringLiteral("PRIVMSG %1 :%2").arg(target, text));
}

bool IrcSession::action(const QString &target, const QString &text) {
  return privmsg(target, ctcpWrap(QStringLiteral("ACTION %1").arg(text)));
}

bool IrcSession::ctcp(const QString &target, const QString &command,
                      const QString &arg) {
  const QString upperCommand = command.toUpper();
  const QString body = arg.isEmpty()
                           ? upperCommand
                           : QStringLiteral("%1 %2").arg(upperCommand, arg);
  return sendRaw(QStringLiteral("PRIVMSG %1 :%2").arg(target, ctcpWrap(body)));
}

bool IrcSession::join(const QString &channel) {
  return sendRaw(QStringLiteral("JOIN %1").arg(channel));
}

bool IrcSession::measureLag() {
  lagToken_ =
      QStringLiteral("MAXCHATLAG%1").arg(QDateTime::currentMSecsSinceEpoch());
  lagTimer_.restart();
  if (!sendRaw(QStringLiteral("PING :%1").arg(lagToken_))) {
    lagToken_.clear();
    return false;
  }
  return true;
}

void IrcSession::onConnected() {
  emit connected();
  sendRaw(QStringLiteral("CAP LS 302"));
  if (!serverPassword_.isEmpty() && passwordAuthAllowed()) {
    sendRaw(QStringLiteral("PASS %1").arg(serverPassword_));
  }
  sendRaw(QStringLiteral("NICK %1").arg(nick_));
  sendRaw(QStringLiteral("USER %1 0 * :%2").arg(username_, realname_));
}

void IrcSession::handleLine(const QString &line) {
  emit rawLine(QStringLiteral("<<"), redactLine(line));

  const IrcMessage msg = parseMessage(line);
  const QString &command = msg.command;
  const QStringList &params = msg.params;

  if (command == QStringLiteral("PING")) {
    sendRaw(QStringLiteral("PONG :%1").arg(msg.trailing()));
    return;
  }

  if (command == QStringLiteral("PONG")) {
    const QString token = msg.trailing().isEmpty() && !params.isEmpty()
                              ? params.last()
                              : msg.trailing();
    if (!lagToken_.isEmpty() && token == lagToken_) {
      emit lagMeasured(double(lagTimer_.elapsed()) / 1000.0);
      lagToken_.clear();
    }
    return;
  }

  if (command == QStringLiteral("CAP")) {
    handleCap(params, msg.trailing());
    return;
  }

  if (command == QStringLiteral("AUTHENTICATE") && !params.isEmpty() &&
      params.first() == QStringLiteral("+")) {
    if (!passwordAuthAllowed()) {
      sendRaw(QStringLiteral("CAP END"));
      return;
    }
    const QString account = saslAccount_.isEmpty() ? nick_ : saslAccount_;
    QByteArray payload;
    payload.append(account.toUtf8());
    payload.append('\0');
    payload.append(account.toUtf8());
    payload.append('\0');
    payload.append(saslPassword_.toUtf8());
    sendRaw(QStringLiteral("AUTHENTICATE %1")
                .arg(QString::fromLatin1(payload.toBase64())));
    return;
  }

  if (command == QStringLiteral("900") || command == QStringLiteral("903")) {
    authed_ = true;
    sendRaw(QStringLiteral("CAP END"));
    return;
  }

  if (command == QStringLiteral("902") || command == QStringLiteral("904") ||
      command == QStringLiteral("905") || command == QStringLiteral("906") ||
      command == QStringLiteral("907")) {
    emit systemText(
        QStringLiteral("SASL authentication failed - continuing without it"));
    sendRaw(QStringLiteral("CAP END"));
    return;
  }

  if (command == QStringLiteral("001")) {
    registered_ = true;
    if (!params.isEmpty()) {
      nick_ = params.first();
    }
    emit registered();
    if (!saslPassword_.isEmpty() && !authed_ && passwordAuthAllowed()) {
      const QString account = saslAccount_.isEmpty() ? nick_ : saslAccount_;
      const QString identify =
          account == nick_
              ? saslPassword_
              : QStringLiteral("%1 %2").arg(account, saslPassword_);
      sendRaw(QStringLiteral("PRIVMSG NickServ :IDENTIFY %1").arg(identify));
    }
    for (const QString &channel : autojoin_) {
      join(channel);
    }
    return;
  }

  if (command == QStringLiteral("005")) {
    handleIsupport(params);
    return;
  }

  if (command == QStringLiteral("433") || command == QStringLiteral("436")) {
    if (!registered_) {
      ++nickTry_;
      nick_ = alternateNick(nickTry_);
      sendRaw(QStringLiteral("NICK %1").arg(nick_));
      emit systemText(QStringLiteral("Nick in use - trying %1").arg(nick_));
    } else {
      emit systemText(QStringLiteral("That nickname is already in use."));
    }
    return;
  }

  if (command == QStringLiteral("NICK")) {
    const QString newNick = params.isEmpty() ? msg.trailing() : params.first();
    const QString oldNick = msg.nick();
    if (oldNick == nick_) {
      nick_ = newNick;
    }
    emit nickChanged(oldNick, newNick);
    return;
  }

  if (command == QStringLiteral("AWAY")) {
    const QString sender = msg.nick().isEmpty() ? msg.prefix : msg.nick();
    if (!sender.isEmpty()) {
      emit awayChanged(sender, !msg.trailing().trimmed().isEmpty());
    }
    if (msg.trailing().trimmed().isEmpty()) {
      emit replyText(sender.isEmpty()
                         ? QStringLiteral("[away] Away status cleared.")
                         : QStringLiteral("[away] %1 is back.").arg(sender));
    } else {
      emit replyText(sender.isEmpty()
                         ? QStringLiteral("[away] %1").arg(msg.trailing())
                         : QStringLiteral("[away] %1 is away: %2")
                               .arg(sender, msg.trailing()));
    }
    return;
  }

  if (command == QStringLiteral("JOIN")) {
    const QString channel = params.isEmpty() ? msg.trailing() : params.first();
    if (!channel.isEmpty()) {
      emit userJoined(channel, msg.nick());
    }
    return;
  }

  if (command == QStringLiteral("PART")) {
    if (!params.isEmpty()) {
      emit userParted(params.first(), msg.nick(), msg.trailing());
    }
    return;
  }

  if (command == QStringLiteral("QUIT")) {
    emit userQuit(msg.nick(), msg.trailing());
    return;
  }

  if (command == QStringLiteral("TOPIC")) {
    if (!params.isEmpty()) {
      emit topicChanged(params.first(), msg.trailing());
    }
    return;
  }

  if ((command == QStringLiteral("PRIVMSG") ||
       command == QStringLiteral("NOTICE")) &&
      !params.isEmpty()) {
    if (isIgnoredPrefix(msg.prefix)) {
      return;
    }

    const QString text = msg.trailing();
    const CtcpMessage ctcp = parseCtcpMessage(text);
    if (ctcp.valid) {
      if (command == QStringLiteral("NOTICE")) {
        emit replyText(ctcpSummary(QStringLiteral("reply"), msg.nick(), ctcp));
        return;
      }

      if (ctcp.command == QStringLiteral("ACTION")) {
        emit messageReceived(msg.nick(), params.first(), ctcp.args, false,
                             true);
        return;
      }

      if (ctcp.command == QStringLiteral("DCC")) {
        // DCC <type> <argument> <address> <port> [size]; pass the raw args up
        // so the UI's DCC manager can negotiate the transfer.
        emit dccRequest(msg.nick(), ctcp.args);
        emit replyText(ctcpSummary(QStringLiteral("request"), msg.nick(), ctcp));
        return;
      }

      QString response;
      if (ctcp.command == QStringLiteral("PING")) {
        response = ctcpBody(QStringLiteral("PING"), ctcp.args);
      } else if (ctcp.command == QStringLiteral("VERSION")) {
        // hide_version suppresses the reply; ctcp_version overrides the text.
        if (!hideVersion_) {
          response = QStringLiteral("VERSION %1")
                         .arg(ctcpVersion_.isEmpty() ? QStringLiteral("MaxChat C++")
                                                     : ctcpVersion_);
        }
      } else if (ctcp.command == QStringLiteral("TIME")) {
        response =
            ctcpBody(QStringLiteral("TIME"),
                     QDateTime::currentDateTime().toString(Qt::TextDate));
      } else if (ctcp.command == QStringLiteral("CLIENTINFO")) {
        response = QStringLiteral("CLIENTINFO ACTION CLIENTINFO PING TIME "
                                  "VERSION");
      }

      if (!response.isEmpty() && !msg.nick().isEmpty()) {
        sendRaw(QStringLiteral("NOTICE %1 :%2")
                    .arg(msg.nick(), ctcpWrap(response)));
      }

      emit replyText(ctcpSummary(QStringLiteral("request"), msg.nick(), ctcp));
      return;
    }

    emit messageReceived(msg.nick(), params.first(), text,
                         command == QStringLiteral("NOTICE"), false);
    return;
  }

  if (command == QStringLiteral("303")) {
    emit isonReply(msg.trailing().split(QLatin1Char(' '), Qt::SkipEmptyParts));
    return;
  }

  if (command == QStringLiteral("ERROR")) {
    emit replyText(msg.trailing().trimmed().isEmpty()
                       ? QStringLiteral("[error] Server closed the connection.")
                       : QStringLiteral("[error] %1").arg(msg.trailing()));
    return;
  }

  if (command == QStringLiteral("WALLOPS")) {
    const QString sender = msg.nick().isEmpty() ? msg.prefix : msg.nick();
    emit replyText(sender.isEmpty()
                       ? QStringLiteral("[wallops] %1").arg(msg.trailing())
                       : QStringLiteral("[wallops] %1: %2")
                             .arg(sender, msg.trailing()));
    return;
  }

  if (command == QStringLiteral("INVITE") && params.size() >= 2) {
    const QString sender = msg.nick().isEmpty() ? msg.prefix : msg.nick();
    // Structured signal so the UI can apply ignore-invites / invite-protect;
    // the prefix carries the full nick!user@host for masking.
    emit invited(sender, params.at(1), msg.prefix);
    emit replyText(QStringLiteral("[invite] %1 invited %2 to %3")
                       .arg(sender, params.at(0), params.at(1)));
    return;
  }

  if (command == QStringLiteral("305") || command == QStringLiteral("306")) {
    emit replyText(msg.trailing().trimmed().isEmpty()
                       ? QStringLiteral("[away] Away status updated.")
                       : QStringLiteral("[away] %1").arg(msg.trailing()));
    return;
  }

  if (command == QStringLiteral("341") && params.size() >= 3) {
    emit replyText(QStringLiteral("[invite] Inviting %1 to %2.")
                       .arg(params.at(1), params.at(2)));
    return;
  }

  if (command == QStringLiteral("372") || command == QStringLiteral("375")) {
    emit replyText(QStringLiteral("[motd] %1").arg(msg.trailing()));
    return;
  }

  if (command == QStringLiteral("376")) {
    emit replyText(QStringLiteral("[motd] End of MOTD."));
    return;
  }

  if (command == QStringLiteral("422")) {
    emit replyText(msg.trailing().trimmed().isEmpty()
                       ? QStringLiteral("[motd] No MOTD is available.")
                       : QStringLiteral("[motd] %1").arg(msg.trailing()));
    return;
  }

  if (command == QStringLiteral("401") && params.size() >= 2) {
    emit replyText(QStringLiteral("[error] No such nick: %1").arg(params.at(1)));
    return;
  }

  if (command == QStringLiteral("403") && params.size() >= 2) {
    emit replyText(
        QStringLiteral("[error] No such channel: %1").arg(params.at(1)));
    return;
  }

  if (command == QStringLiteral("404") && params.size() >= 2) {
    emit replyText(QStringLiteral("[error] Cannot send to %1: %2")
                       .arg(params.at(1), msg.trailing()));
    return;
  }

  if (command == QStringLiteral("405") && params.size() >= 2) {
    emit replyText(QStringLiteral("[error] Cannot join %1: %2")
                       .arg(params.at(1), msg.trailing()));
    return;
  }

  if (command == QStringLiteral("421") && params.size() >= 2) {
    emit replyText(QStringLiteral("[error] Unknown command %1: %2")
                       .arg(params.at(1), msg.trailing()));
    return;
  }

  if (command == QStringLiteral("431")) {
    emit replyText(msg.trailing().trimmed().isEmpty()
                       ? QStringLiteral("[error] No nickname given.")
                       : QStringLiteral("[error] %1").arg(msg.trailing()));
    return;
  }

  if (command == QStringLiteral("432") && params.size() >= 2) {
    emit replyText(QStringLiteral("[error] Bad nickname %1: %2")
                       .arg(params.at(1), msg.trailing()));
    return;
  }

  if (command == QStringLiteral("437") && params.size() >= 2) {
    emit replyText(QStringLiteral("[error] %1 is unavailable: %2")
                       .arg(params.at(1), msg.trailing()));
    return;
  }

  if (command == QStringLiteral("441") && params.size() >= 3) {
    emit replyText(QStringLiteral("[error] %1 is not on %2: %3")
                       .arg(params.at(1), params.at(2), msg.trailing()));
    return;
  }

  if (command == QStringLiteral("442") && params.size() >= 2) {
    emit replyText(QStringLiteral("[error] You are not on %1: %2")
                       .arg(params.at(1), msg.trailing()));
    return;
  }

  if (command == QStringLiteral("443") && params.size() >= 3) {
    emit replyText(QStringLiteral("[error] %1 is already on %2: %3")
                       .arg(params.at(1), params.at(2), msg.trailing()));
    return;
  }

  if (command == QStringLiteral("451")) {
    emit replyText(msg.trailing().trimmed().isEmpty()
                       ? QStringLiteral("[error] You have not registered.")
                       : QStringLiteral("[error] %1").arg(msg.trailing()));
    return;
  }

  if (command == QStringLiteral("461") && params.size() >= 2) {
    emit replyText(QStringLiteral("[error] %1 needs more parameters: %2")
                       .arg(params.at(1), msg.trailing()));
    return;
  }

  if (command == QStringLiteral("464")) {
    emit replyText(msg.trailing().trimmed().isEmpty()
                       ? QStringLiteral("[error] Password incorrect.")
                       : QStringLiteral("[error] %1").arg(msg.trailing()));
    return;
  }

  if ((command == QStringLiteral("471") || command == QStringLiteral("473") ||
       command == QStringLiteral("474") || command == QStringLiteral("475") ||
       command == QStringLiteral("477") || command == QStringLiteral("482")) &&
      params.size() >= 2) {
    emit replyText(QStringLiteral("[error] %1: %2")
                       .arg(params.at(1), msg.trailing()));
    return;
  }

  if (command == QStringLiteral("472") && params.size() >= 2) {
    emit replyText(QStringLiteral("[error] Unknown mode %1: %2")
                       .arg(params.at(1), msg.trailing()));
    return;
  }

  if (command == QStringLiteral("481") || command == QStringLiteral("501") ||
      command == QStringLiteral("502")) {
    emit replyText(msg.trailing().trimmed().isEmpty()
                       ? QStringLiteral("[error] Server rejected the command.")
                       : QStringLiteral("[error] %1").arg(msg.trailing()));
    return;
  }

  if (command == QStringLiteral("KICK")) {
    if (params.size() >= 2) {
      emit kicked(params.at(0), params.at(1), msg.trailing());
    }
    return;
  }

  if (command == QStringLiteral("MODE")) {
    if (!params.isEmpty()) {
      emit modeChanged(params.first(), msg.nick(),
                       params.mid(1).join(QLatin1Char(' ')));
    }
    return;
  }

  if (command == QStringLiteral("331")) {
    if (params.size() >= 2) {
      emit topicChanged(params.at(1), QString());
      emit replyText(QStringLiteral("[topic] %1 has no topic.")
                         .arg(params.at(1)));
    }
    return;
  }

  if (command == QStringLiteral("332")) {
    if (params.size() >= 2) {
      emit topicChanged(params.at(1), msg.trailing());
      emit replyText(QStringLiteral("[topic] %1: %2")
                         .arg(params.at(1), msg.trailing()));
    }
    return;
  }

  if (command == QStringLiteral("333") && params.size() >= 4) {
    emit replyText(QStringLiteral("[topic] %1 set by %2 at %3")
                       .arg(params.at(1), params.at(2), params.at(3)));
    return;
  }

  if (command == QStringLiteral("328") && params.size() >= 2) {
    const QString url = msg.trailing().trimmed().isEmpty() && params.size() >= 3
                            ? params.at(2)
                            : msg.trailing();
    emit replyText(QStringLiteral("[channel] %1 URL: %2")
                       .arg(params.at(1), url));
    return;
  }

  if (command == QStringLiteral("324")) {
    if (params.size() >= 3) {
      emit channelModeIs(params.at(1), params.mid(2).join(QLatin1Char(' ')));
    }
    return;
  }

  if (command == QStringLiteral("353")) {
    if (params.size() >= 3) {
      emit namesReceived(
          params.at(2),
          msg.trailing().split(QLatin1Char(' '), Qt::SkipEmptyParts));
    }
    return;
  }

  if (command == QStringLiteral("366")) {
    if (params.size() >= 2) {
      emit namesEnd(params.at(1));
    }
    return;
  }

  if (command == QStringLiteral("329") && params.size() >= 3) {
    emit replyText(QStringLiteral("[channel] %1 created at %2")
                       .arg(params.at(1), params.at(2)));
    return;
  }

  if (command == QStringLiteral("322")) {
    if (params.size() >= 3) {
      bool ok = false;
      const int users = params.at(2).toInt(&ok);
      emit listReply(params.at(1), ok ? users : 0, msg.trailing());
    }
    return;
  }

  if (command == QStringLiteral("323")) {
    emit listEnd();
    return;
  }

  if (command == QStringLiteral("352") && params.size() >= 7) {
    const QString trailing = msg.trailing();
    const int space = trailing.indexOf(QLatin1Char(' '));
    const QString real = space >= 0 ? trailing.mid(space + 1) : trailing;
    emit replyText(QStringLiteral("[who] %1 (%2@%3) %4 on %5 - %6")
                       .arg(params.at(5), params.at(2), params.at(3),
                            params.at(6), params.at(1), real));
    return;
  }

  if (command == QStringLiteral("315")) {
    emit replyText(QStringLiteral("[who] End of /WHO."));
    return;
  }

  if (command == QStringLiteral("301") && params.size() >= 2) {
    emit replyText(QStringLiteral("[whois] %1 is away: %2")
                       .arg(params.at(1), msg.trailing()));
    return;
  }

  if (command == QStringLiteral("307") && params.size() >= 2) {
    emit replyText(QStringLiteral("[whois] %1 is registered.").arg(params.at(1)));
    return;
  }

  if (command == QStringLiteral("311") && params.size() >= 4) {
    emit replyText(QStringLiteral("[whois] %1 is %2@%3 (%4)")
                       .arg(params.at(1), params.at(2), params.at(3),
                            msg.trailing()));
    return;
  }

  if (command == QStringLiteral("312") && params.size() >= 3) {
    const QString detail = msg.trailing().trimmed();
    emit replyText(detail.isEmpty()
                       ? QStringLiteral("[whois] %1 is on server %2")
                             .arg(params.at(1), params.at(2))
                       : QStringLiteral("[whois] %1 is on server %2 (%3)")
                             .arg(params.at(1), params.at(2), detail));
    return;
  }

  if (command == QStringLiteral("313") && params.size() >= 2) {
    emit replyText(msg.trailing().trimmed().isEmpty()
                       ? QStringLiteral("[whois] %1 is an IRC operator")
                             .arg(params.at(1))
                       : QStringLiteral("[whois] %1 %2")
                             .arg(params.at(1), msg.trailing()));
    return;
  }

  if (command == QStringLiteral("317") && params.size() >= 3) {
    QString line = QStringLiteral("[whois] %1 has been idle %2 seconds")
                       .arg(params.at(1), params.at(2));
    if (params.size() >= 4) {
      line += QStringLiteral(", signed on %1").arg(params.at(3));
    }
    emit replyText(line);
    return;
  }

  if (command == QStringLiteral("318")) {
    emit replyText(QStringLiteral("[whois] End of /WHOIS."));
    return;
  }

  if (command == QStringLiteral("319") && params.size() >= 2) {
    emit replyText(QStringLiteral("[whois] %1 channels: %2")
                       .arg(params.at(1), msg.trailing()));
    return;
  }

  if (command == QStringLiteral("330") && params.size() >= 3) {
    emit replyText(QStringLiteral("[whois] %1 is logged in as %2")
                       .arg(params.at(1), params.at(2)));
    return;
  }

  if (command == QStringLiteral("671") && params.size() >= 2) {
    emit replyText(msg.trailing().trimmed().isEmpty()
                       ? QStringLiteral("[whois] %1 is using a secure connection")
                             .arg(params.at(1))
                       : QStringLiteral("[whois] %1 %2")
                             .arg(params.at(1), msg.trailing()));
    return;
  }

  if (command == QStringLiteral("314") && params.size() >= 4) {
    emit replyText(
        QStringLiteral("[whowas] %1 was %2@%3 (%4)")
            .arg(params.at(1), params.at(2), params.at(3), msg.trailing()));
    return;
  }

  if (command == QStringLiteral("369")) {
    emit replyText(params.size() >= 2
                       ? QStringLiteral("[whowas] End of /WHOWAS for %1.")
                             .arg(params.at(1))
                       : QStringLiteral("[whowas] End of /WHOWAS."));
    return;
  }

  if (command == QStringLiteral("406") && params.size() >= 2) {
    emit replyText(msg.trailing().trimmed().isEmpty()
                       ? QStringLiteral("[whowas] No history for %1.")
                             .arg(params.at(1))
                       : QStringLiteral("[whowas] %1: %2")
                             .arg(params.at(1), msg.trailing()));
    return;
  }

  if (command == QStringLiteral("367") && params.size() >= 3) {
    emit banList(params.at(1), params.at(2),
                 params.size() >= 4 ? params.at(3) : QString());
    return;
  }

  if (command == QStringLiteral("368") && params.size() >= 2) {
    emit banListEnd(params.at(1));
    return;
  }
}

bool IrcSession::passwordAuthAllowed() {
  if (tls_ || allowInsecureAuth_) {
    return true;
  }
  if (!insecureAuthWarned_) {
    insecureAuthWarned_ = true;
    emit systemText(QStringLiteral("Password authentication skipped on "
                                   "plaintext IRC. Enable SSL/TLS or opt in to "
                                   "plaintext auth for this saved network."));
  }
  return false;
}

QString IrcSession::alternateNick(int attempt) const {
  const QString base =
      baseNick_.isEmpty() ? QStringLiteral("guest") : baseNick_;
  static const QStringList suffixes = {
      QStringLiteral("_"), QStringLiteral("__"), QStringLiteral("___"),
      QStringLiteral("^"), QStringLiteral("`"),
  };
  return attempt <= suffixes.size()
             ? base + suffixes.at(attempt - 1)
             : QStringLiteral("%1%2").arg(base).arg(attempt);
}

bool IrcSession::isIgnoredPrefix(const QString &prefix) const {
  if (prefix.trimmed().isEmpty()) {
    return false;
  }

  const QString folded = prefix.toLower();
  return std::any_of(ignorePatterns_.cbegin(), ignorePatterns_.cend(),
                     [&folded](const QRegularExpression &pattern) {
                       return pattern.match(folded).hasMatch();
                     });
}

void IrcSession::handleIsupport(const QStringList &params) {
  if (params.size() < 2) {
    return;
  }

  for (int index = 1; index < params.size(); ++index) {
    const QString token = params.at(index).trimmed();
    if (token.isEmpty()) {
      continue;
    }
    const int equals = token.indexOf(QLatin1Char('='));
    const QString key =
        (equals < 0 ? token : token.left(equals)).trimmed().toUpper();
    if (key.isEmpty()) {
      continue;
    }
    isupport_.insert(key, equals < 0 ? QString() : token.mid(equals + 1));
  }
}

void IrcSession::handleCap(const QStringList &params, const QString &trailing) {
  const QString subCommand = params.size() >= 2 ? params.at(1) : QString();

  if (subCommand == QStringLiteral("LS")) {
    const QStringList caps =
        trailing.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &cap : caps) {
      serverCaps_.insert(cap.section(QLatin1Char('='), 0, 0));
    }
    if (params.size() >= 3 && params.at(2) == QStringLiteral("*")) {
      return;
    }

    QStringList wanted;
    for (const QString &cap :
         {QStringLiteral("multi-prefix"), QStringLiteral("server-time"),
          QStringLiteral("away-notify")}) {
      if (serverCaps_.contains(cap)) {
        wanted.append(cap);
      }
    }
    if (!saslPassword_.isEmpty() &&
        serverCaps_.contains(QStringLiteral("sasl")) && passwordAuthAllowed()) {
      wanted.append(QStringLiteral("sasl"));
    }

    sendRaw(
        wanted.isEmpty()
            ? QStringLiteral("CAP END")
            : QStringLiteral("CAP REQ :%1").arg(wanted.join(QLatin1Char(' '))));
    return;
  }

  if (subCommand == QStringLiteral("ACK")) {
    if (trailing.split(QLatin1Char(' '), Qt::SkipEmptyParts)
            .contains(QStringLiteral("sasl")) &&
        !saslPassword_.isEmpty() && passwordAuthAllowed()) {
      sendRaw(QStringLiteral("AUTHENTICATE PLAIN"));
    } else {
      sendRaw(QStringLiteral("CAP END"));
    }
    return;
  }

  if (subCommand == QStringLiteral("NAK")) {
    sendRaw(QStringLiteral("CAP END"));
  }
}

} // namespace maxchat::irc
