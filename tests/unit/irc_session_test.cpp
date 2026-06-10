#include "irc/IrcSession.h"

#include "app/AppInfo.h"

#include <QtTest/QtTest>

#include <algorithm>

using maxchat::irc::IrcMaxWireBytes;
using maxchat::irc::IrcSession;

class IrcSessionTest final : public QObject {
  Q_OBJECT

private:
  static void configureNick(IrcSession &session,
                            const QString &nick = QStringLiteral("bob")) {
    IrcSession::Registration registration;
    registration.nick = nick;
    session.configureRegistration(registration);
  }

  static QStringList linesFromWrites(const QList<QByteArray> &writes) {
    QStringList lines;
    for (const QByteArray &write : writes) {
      QString line = QString::fromUtf8(write);
      if (line.endsWith(QStringLiteral("\r\n"))) {
        line.chop(2);
      }
      lines.append(line);
    }
    return lines;
  }

  static QString ctcpPayload(const QString &body) {
    return QString(QLatin1Char('\x01')) + body + QString(QLatin1Char('\x01'));
  }

private slots:
  void sendRawReportsDisconnected() {
    IrcSession session;
    QList<QByteArray> writes;
    QSignalSpy rawLines(&session, &IrcSession::rawLine);
    session.setWriter([&writes](const QByteArray &payload) {
      writes.append(payload);
      return payload.size();
    });

    QVERIFY(!session.sendRaw(QStringLiteral("PING x")));

    QCOMPARE(writes.size(), 0);
    QCOMPARE(rawLines.count(), 0);
  }

  void sendRawRejectsOversizedWireLine() {
    IrcSession session;
    QList<QByteArray> writes;
    QSignalSpy errors(&session, &IrcSession::errorOccurred);
    session.setConnected(true);
    session.setWriter([&writes](const QByteArray &payload) {
      writes.append(payload);
      return payload.size();
    });

    QVERIFY(!session.sendRaw(QStringLiteral("PRIVMSG #chan :") +
                             QString(IrcMaxWireBytes, QLatin1Char('x'))));

    QCOMPARE(writes.size(), 0);
    QCOMPARE(errors.count(), 1);
    QVERIFY(errors.takeFirst().at(0).toString().contains(
        QStringLiteral("line is too long")));
  }

  void sendRawReportsPartialSocketWrite() {
    IrcSession session;
    QList<QByteArray> writes;
    QSignalSpy errors(&session, &IrcSession::errorOccurred);
    QSignalSpy rawLines(&session, &IrcSession::rawLine);
    session.setConnected(true);
    session.setWriter([&writes](const QByteArray &payload) {
      writes.append(payload);
      return 1;
    });

    QVERIFY(!session.sendRaw(QStringLiteral("PING x")));

    QCOMPARE(linesFromWrites(writes), QStringList({QStringLiteral("PING x")}));
    QCOMPARE(rawLines.count(), 0);
    QCOMPARE(errors.count(), 1);
    QVERIFY(errors.takeFirst().at(0).toString().contains(
        QStringLiteral("Socket write failed")));
  }

  void helpersBuildExpectedCommands() {
    IrcSession session;
    QList<QByteArray> writes;
    session.setConnected(true);
    session.setWriter([&writes](const QByteArray &payload) {
      writes.append(payload);
      return payload.size();
    });

    QVERIFY(session.privmsg(QStringLiteral("#chan"), QStringLiteral("hello")));
    QVERIFY(session.action(QStringLiteral("#chan"), QStringLiteral("waves")));
    QVERIFY(session.ctcp(QStringLiteral("nick"), QStringLiteral("VERSION")));

    QCOMPARE(linesFromWrites(writes),
             QStringList({
                 QStringLiteral("PRIVMSG #chan :hello"),
                 QStringLiteral("PRIVMSG #chan :") +
                     ctcpPayload(QStringLiteral("ACTION waves")),
                 QStringLiteral("PRIVMSG nick :") +
                     ctcpPayload(QStringLiteral("VERSION")),
             }));

    session.setConnected(false);
    QVERIFY(!session.privmsg(QStringLiteral("#chan"), QStringLiteral("nope")));
  }

  void measureLagSendsPingAndEmitsOnMatchingPong() {
    IrcSession session;
    QList<QByteArray> writes;
    QSignalSpy lag(&session, &IrcSession::lagMeasured);
    session.setConnected(true);
    session.setWriter([&writes](const QByteArray &payload) {
      writes.append(payload);
      return payload.size();
    });

    QVERIFY(session.measureLag());
    QCOMPARE(writes.size(), 1);
    const QString ping = linesFromWrites(writes).first();
    QVERIFY(ping.startsWith(QStringLiteral("PING :MAXCHATLAG")));
    const QString token = ping.mid(QStringLiteral("PING :").size());

    session.handleLine(QStringLiteral(":srv PONG srv :wrong"));
    QCOMPARE(lag.count(), 0);
    session.handleLine(QStringLiteral(":srv PONG srv :%1").arg(token));

    QCOMPARE(lag.count(), 1);
    QVERIFY(lag.at(0).at(0).toDouble() >= 0.0);
  }

  void isupportTokensAreStoredByKey() {
    IrcSession session;

    session.handleLine(QStringLiteral(
        ":srv 005 bob NETWORK=Libera.Chat CHANTYPES=#& PREFIX=(ov)@+ "
        "CASEMAPPING=rfc1459 EXCEPTS :are supported"));

    const QHash<QString, QString> isupport = session.isupport();
    QCOMPARE(isupport.value(QStringLiteral("NETWORK")),
             QStringLiteral("Libera.Chat"));
    QCOMPARE(isupport.value(QStringLiteral("CHANTYPES")), QStringLiteral("#&"));
    QCOMPARE(isupport.value(QStringLiteral("PREFIX")),
             QStringLiteral("(ov)@+"));
    QCOMPARE(isupport.value(QStringLiteral("CASEMAPPING")),
             QStringLiteral("rfc1459"));
    QVERIFY(isupport.contains(QStringLiteral("EXCEPTS")));
    QCOMPARE(isupport.value(QStringLiteral("EXCEPTS")), QString());
  }

  void plaintextPassIsSkippedWithoutOptIn() {
    IrcSession session;
    IrcSession::Registration registration;
    registration.nick = QStringLiteral("bob");
    registration.username = QStringLiteral("bob");
    registration.realname = QStringLiteral("Bob");
    registration.serverPassword = QStringLiteral("secret");
    registration.tls = false;
    registration.allowInsecureAuth = false;
    session.configureRegistration(registration);

    QList<QByteArray> writes;
    QSignalSpy warnings(&session, &IrcSession::systemText);
    session.setConnected(true);
    session.setWriter([&writes](const QByteArray &payload) {
      writes.append(payload);
      return payload.size();
    });

    session.onConnected();

    const QStringList lines = linesFromWrites(writes);
    QVERIFY(!lines.contains(QStringLiteral("PASS secret")));
    QCOMPARE(lines, QStringList({
                        QStringLiteral("CAP LS 302"),
                        QStringLiteral("NICK bob"),
                        QStringLiteral("USER bob 0 * :Bob"),
                    }));
    QCOMPARE(warnings.count(), 1);
    QVERIFY(warnings.takeFirst().at(0).toString().contains(
        QStringLiteral("Password authentication skipped")));
  }

  void plaintextSaslIsNotRequestedWithoutOptIn() {
    IrcSession session;
    IrcSession::Registration registration;
    registration.nick = QStringLiteral("bob");
    registration.saslPassword = QStringLiteral("secret");
    registration.tls = false;
    registration.allowInsecureAuth = false;
    session.configureRegistration(registration);

    QList<QByteArray> writes;
    session.setConnected(true);
    session.setWriter([&writes](const QByteArray &payload) {
      writes.append(payload);
      return payload.size();
    });

    session.handleLine(QStringLiteral(":srv CAP * LS :multi-prefix sasl"));

    QCOMPARE(linesFromWrites(writes),
             QStringList({QStringLiteral("CAP REQ :multi-prefix")}));
  }

  void plaintextNickServFallbackIsSkippedWithoutOptIn() {
    IrcSession session;
    IrcSession::Registration registration;
    registration.nick = QStringLiteral("bob");
    registration.saslAccount = QStringLiteral("bob");
    registration.saslPassword = QStringLiteral("secret");
    registration.tls = false;
    registration.allowInsecureAuth = false;
    session.configureRegistration(registration);

    QList<QByteArray> writes;
    session.setConnected(true);
    session.setWriter([&writes](const QByteArray &payload) {
      writes.append(payload);
      return payload.size();
    });

    session.handleLine(QStringLiteral(":srv 001 bob :Welcome"));

    QVERIFY(std::none_of(writes.cbegin(), writes.cend(),
                         [](const QByteArray &payload) {
                           return payload.contains("NickServ");
                         }));
  }

  void plaintextAuthOptInAllowsSasl() {
    IrcSession session;
    IrcSession::Registration registration;
    registration.nick = QStringLiteral("bob");
    registration.saslPassword = QStringLiteral("secret");
    registration.tls = false;
    registration.allowInsecureAuth = true;
    session.configureRegistration(registration);

    QList<QByteArray> writes;
    session.setConnected(true);
    session.setWriter([&writes](const QByteArray &payload) {
      writes.append(payload);
      return payload.size();
    });

    session.handleLine(QStringLiteral(":srv CAP * ACK :sasl"));

    QCOMPARE(linesFromWrites(writes),
             QStringList({QStringLiteral("AUTHENTICATE PLAIN")}));
  }

  void saslAuthenticatePayloadUsesConfiguredAccount() {
    IrcSession session;
    IrcSession::Registration registration;
    registration.nick = QStringLiteral("bob");
    registration.saslAccount = QStringLiteral("account");
    registration.saslPassword = QStringLiteral("secret");
    registration.tls = true;
    session.configureRegistration(registration);

    QList<QByteArray> writes;
    session.setConnected(true);
    session.setWriter([&writes](const QByteArray &payload) {
      writes.append(payload);
      return payload.size();
    });

    session.handleLine(QStringLiteral("AUTHENTICATE +"));

    QCOMPARE(linesFromWrites(writes),
             QStringList({QStringLiteral(
                 "AUTHENTICATE YWNjb3VudABhY2NvdW50AHNlY3JldA==")}));
  }

  void altNickOnCollisionBeforeRegistration() {
    IrcSession session;
    configureNick(session);
    QList<QByteArray> writes;
    session.setConnected(true);
    session.setWriter([&writes](const QByteArray &payload) {
      writes.append(payload);
      return payload.size();
    });

    session.handleLine(
        QStringLiteral(":srv 433 * bob :Nickname is already in use"));
    QCOMPARE(session.nick(), QStringLiteral("bob_"));
    session.handleLine(
        QStringLiteral(":srv 433 * bob_ :Nickname is already in use"));
    QCOMPARE(session.nick(), QStringLiteral("bob__"));

    session.handleLine(QStringLiteral(":srv 001 bob__ :Welcome"));
    session.handleLine(QStringLiteral(":srv 433 * whatever :in use"));

    QCOMPARE(session.nick(), QStringLiteral("bob__"));
    QCOMPARE(linesFromWrites(writes).filter(QStringLiteral("NICK ")),
             QStringList(
                 {QStringLiteral("NICK bob_"), QStringLiteral("NICK bob__")}));
  }

  void nickSyncedOnWelcome() {
    IrcSession session;
    configureNick(session);

    session.handleLine(QStringLiteral(":srv 001 bob_ :Welcome to the network"));

    QCOMPARE(session.nick(), QStringLiteral("bob_"));
    QVERIFY(session.isRegistered());
  }

  void ownNickChangeUpdatesSelf() {
    IrcSession session;
    configureNick(session);
    QSignalSpy changes(&session, &IrcSession::nickChanged);

    session.handleLine(QStringLiteral(":bob!u@h NICK :bobby"));
    session.handleLine(QStringLiteral(":alice!u@h NICK :ally"));

    QCOMPARE(session.nick(), QStringLiteral("bobby"));
    QCOMPARE(changes.count(), 2);
    QCOMPARE(changes.at(0).at(0).toString(), QStringLiteral("bob"));
    QCOMPARE(changes.at(0).at(1).toString(), QStringLiteral("bobby"));
    QCOMPARE(changes.at(1).at(0).toString(), QStringLiteral("alice"));
    QCOMPARE(changes.at(1).at(1).toString(), QStringLiteral("ally"));
  }

  void awayNotifyEmitsReadableText() {
    IrcSession session;
    QSignalSpy replies(&session, &IrcSession::replyText);

    session.handleLine(QStringLiteral(":alice!u@h AWAY :Out for lunch"));
    session.handleLine(QStringLiteral(":alice!u@h AWAY"));

    QStringList lines;
    for (const QList<QVariant> &reply : replies) {
      lines.append(reply.at(0).toString());
    }
    QCOMPARE(lines, QStringList({
                        QStringLiteral("[away] alice is away: Out for lunch"),
                        QStringLiteral("[away] alice is back."),
                    }));
  }

  void privmsgNoticeAndActionsEmitMessageSignal() {
    IrcSession session;
    QSignalSpy messages(&session, &IrcSession::messageReceived);

    session.handleLine(QStringLiteral(":alice!u@h PRIVMSG #chan :hello there"));
    session.handleLine(QStringLiteral(":bob!u@h PRIVMSG #chan :") +
                       ctcpPayload(QStringLiteral("ACTION waves")));
    session.handleLine(
        QStringLiteral(":service!u@h NOTICE bob :maintenance soon"));

    QCOMPARE(messages.count(), 3);
    QCOMPARE(messages.at(0).at(0).toString(), QStringLiteral("alice"));
    QCOMPARE(messages.at(0).at(1).toString(), QStringLiteral("#chan"));
    QCOMPARE(messages.at(0).at(2).toString(), QStringLiteral("hello there"));
    QCOMPARE(messages.at(0).at(3).toBool(), false);
    QCOMPARE(messages.at(0).at(4).toBool(), false);
    QCOMPARE(messages.at(1).at(0).toString(), QStringLiteral("bob"));
    QCOMPARE(messages.at(1).at(2).toString(), QStringLiteral("waves"));
    QCOMPARE(messages.at(1).at(3).toBool(), false);
    QCOMPARE(messages.at(1).at(4).toBool(), true);
    QCOMPARE(messages.at(2).at(0).toString(), QStringLiteral("service"));
    QCOMPARE(messages.at(2).at(1).toString(), QStringLiteral("bob"));
    QCOMPARE(messages.at(2).at(2).toString(),
             QStringLiteral("maintenance soon"));
    QCOMPARE(messages.at(2).at(3).toBool(), true);
    QCOMPARE(messages.at(2).at(4).toBool(), false);
  }

  void ctcpVersionRequestRepliesWithoutChatMessage() {
    IrcSession session;
    QList<QByteArray> writes;
    QSignalSpy messages(&session, &IrcSession::messageReceived);
    QSignalSpy replies(&session, &IrcSession::replyText);
    session.setConnected(true);
    session.setWriter([&writes](const QByteArray &payload) {
      writes.append(payload);
      return payload.size();
    });

    session.handleLine(QStringLiteral(":alice!u@h PRIVMSG bob :") +
                       ctcpPayload(QStringLiteral("VERSION")));

    QCOMPARE(messages.count(), 0);
    QCOMPARE(replies.count(), 1);
    QCOMPARE(replies.at(0).at(0).toString(),
             QStringLiteral("[ctcp] VERSION request from alice"));
    const QString expectedVersion =
        QStringLiteral("VERSION %1 %2")
            .arg(maxchat::app::displayName(), maxchat::app::version());
    QCOMPARE(linesFromWrites(writes),
             QStringList({QStringLiteral("NOTICE alice :") +
                          ctcpPayload(expectedVersion)}));
  }

  void ctcpPingRequestEchoesPayload() {
    IrcSession session;
    QList<QByteArray> writes;
    QSignalSpy replies(&session, &IrcSession::replyText);
    session.setConnected(true);
    session.setWriter([&writes](const QByteArray &payload) {
      writes.append(payload);
      return payload.size();
    });

    session.handleLine(QStringLiteral(":alice!u@h PRIVMSG bob :") +
                       ctcpPayload(QStringLiteral("PING 12345")));

    QCOMPARE(replies.count(), 1);
    QCOMPARE(replies.at(0).at(0).toString(),
             QStringLiteral("[ctcp] PING request from alice: 12345"));
    QCOMPARE(linesFromWrites(writes),
             QStringList({QStringLiteral("NOTICE alice :") +
                          ctcpPayload(QStringLiteral("PING 12345"))}));
  }

  void ctcpNoticeReplyIsReadableOnly() {
    IrcSession session;
    QSignalSpy messages(&session, &IrcSession::messageReceived);
    QSignalSpy replies(&session, &IrcSession::replyText);

    session.handleLine(QStringLiteral(":alice!u@h NOTICE bob :") +
                       ctcpPayload(QStringLiteral("VERSION client 1.2")));

    QCOMPARE(messages.count(), 0);
    QCOMPARE(replies.count(), 1);
    QCOMPARE(replies.at(0).at(0).toString(),
             QStringLiteral("[ctcp] VERSION reply from alice: client 1.2"));
  }

  void unknownCtcpRequestDoesNotBecomeChatText() {
    IrcSession session;
    QList<QByteArray> writes;
    QSignalSpy messages(&session, &IrcSession::messageReceived);
    QSignalSpy replies(&session, &IrcSession::replyText);
    session.setConnected(true);
    session.setWriter([&writes](const QByteArray &payload) {
      writes.append(payload);
      return payload.size();
    });

    session.handleLine(QStringLiteral(":alice!u@h PRIVMSG bob :") +
                       ctcpPayload(QStringLiteral("DCC SEND file.txt")));

    QCOMPARE(messages.count(), 0);
    QCOMPARE(replies.count(), 1);
    QCOMPARE(replies.at(0).at(0).toString(),
             QStringLiteral("[ctcp] DCC request from alice: SEND file.txt"));
    QCOMPARE(writes.count(), 0);
  }

  void ignoredPrefixesDropMessagesAndNotices() {
    IrcSession session;
    session.setIgnoreMasks({QStringLiteral("alice!*@*")});
    QSignalSpy messages(&session, &IrcSession::messageReceived);

    session.handleLine(QStringLiteral(":alice!u@host PRIVMSG #chan :hidden"));
    session.handleLine(
        QStringLiteral(":alice!u@host NOTICE #chan :also hidden"));
    session.handleLine(QStringLiteral(":bob!u@host PRIVMSG #chan :visible"));

    QCOMPARE(messages.count(), 1);
    QCOMPARE(messages.at(0).at(0).toString(), QStringLiteral("bob"));
    QCOMPARE(messages.at(0).at(2).toString(), QStringLiteral("visible"));
  }

  void isonRepliesEmitOnlineNickList() {
    IrcSession session;
    QSignalSpy replies(&session, &IrcSession::isonReply);

    session.handleLine(QStringLiteral(":srv 303 me :alice bob"));

    QCOMPARE(replies.count(), 1);
    QCOMPARE(replies.at(0).at(0).toStringList(),
             QStringList({QStringLiteral("alice"), QStringLiteral("bob")}));
  }

  void whoisRepliesEmitReadableText() {
    IrcSession session;
    QSignalSpy replies(&session, &IrcSession::replyText);

    session.handleLine(QStringLiteral(":srv 311 me bob ~user host.example * "
                                      ":Bob Smith"));
    session.handleLine(
        QStringLiteral(":srv 312 me bob irc.example.net :Example server"));
    session.handleLine(
        QStringLiteral(":srv 319 me bob :#chat @#ops +#voice"));
    session.handleLine(QStringLiteral(":srv 330 me bob account :logged in"));
    session.handleLine(
        QStringLiteral(":srv 301 me bob :Away grabbing coffee"));
    session.handleLine(
        QStringLiteral(":srv 313 me bob :is an IRC operator"));
    session.handleLine(QStringLiteral(":srv 317 me bob 42 1700000000 "
                                      ":seconds idle, signon time"));
    session.handleLine(
        QStringLiteral(":srv 671 me bob :is using a secure connection"));
    session.handleLine(QStringLiteral(":srv 318 me bob :End of /WHOIS list"));

    QStringList lines;
    for (const QList<QVariant> &reply : replies) {
      lines.append(reply.at(0).toString());
    }
    QCOMPARE(lines,
             QStringList({
                 QStringLiteral("[whois] bob is ~user@host.example (Bob Smith)"),
                 QStringLiteral(
                     "[whois] bob is on server irc.example.net (Example server)"),
                 QStringLiteral("[whois] bob channels: #chat @#ops +#voice"),
                 QStringLiteral("[whois] bob is logged in as account"),
                 QStringLiteral("[whois] bob is away: Away grabbing coffee"),
                 QStringLiteral("[whois] bob is an IRC operator"),
                 QStringLiteral(
                     "[whois] bob has been idle 42 seconds, signed on 1700000000"),
                 QStringLiteral("[whois] bob is using a secure connection"),
                 QStringLiteral("[whois] End of /WHOIS."),
             }));
  }

  void whowasRepliesEmitReadableText() {
    IrcSession session;
    QSignalSpy replies(&session, &IrcSession::replyText);

    session.handleLine(
        QStringLiteral(":srv 314 me oldbob ~user old.host * :Bob Was Here"));
    session.handleLine(
        QStringLiteral(":srv 406 me missing :There was no such nickname"));
    session.handleLine(QStringLiteral(":srv 369 me oldbob :End of WHOWAS"));

    QStringList lines;
    for (const QList<QVariant> &reply : replies) {
      lines.append(reply.at(0).toString());
    }
    QCOMPARE(lines,
             QStringList({
                 QStringLiteral(
                     "[whowas] oldbob was ~user@old.host (Bob Was Here)"),
                 QStringLiteral("[whowas] missing: There was no such nickname"),
                 QStringLiteral("[whowas] End of /WHOWAS for oldbob."),
             }));
  }

  void motdAndErrorsEmitReadableText() {
    IrcSession session;
    QSignalSpy replies(&session, &IrcSession::replyText);

    session.handleLine(QStringLiteral(":srv 375 me :- server message"));
    session.handleLine(QStringLiteral(":srv 372 me :- Welcome to IRC"));
    session.handleLine(QStringLiteral(":srv 376 me :End of /MOTD command"));
    session.handleLine(QStringLiteral(":srv 422 me :MOTD File is missing"));
    session.handleLine(QStringLiteral(":srv 401 me badnick :No such nick"));
    session.handleLine(QStringLiteral(":srv 403 me #lost :No such channel"));
    session.handleLine(
        QStringLiteral(":srv 404 me #chat :Cannot send to channel"));
    session.handleLine(QStringLiteral(":srv 421 me WAT :Unknown command"));
    session.handleLine(QStringLiteral(":srv 464 me :Password incorrect"));
    session.handleLine(QStringLiteral(":srv 471 me #full :Channel is full"));
    session.handleLine(QStringLiteral(":srv 473 me #invite :Invite only"));
    session.handleLine(QStringLiteral(":srv 474 me #banned :Banned"));
    session.handleLine(QStringLiteral(":srv 475 me #keyed :Bad key"));
    session.handleLine(
        QStringLiteral(":srv 477 me #registered :Registration required"));
    session.handleLine(
        QStringLiteral(":srv 482 me #chat :You're not channel operator"));

    QStringList lines;
    for (const QList<QVariant> &reply : replies) {
      lines.append(reply.at(0).toString());
    }
    QCOMPARE(lines,
             QStringList({
                 QStringLiteral("[motd] - server message"),
                 QStringLiteral("[motd] - Welcome to IRC"),
                 QStringLiteral("[motd] End of MOTD."),
                 QStringLiteral("[motd] MOTD File is missing"),
                 QStringLiteral("[error] No such nick: badnick"),
                 QStringLiteral("[error] No such channel: #lost"),
                 QStringLiteral("[error] Cannot send to #chat: Cannot send to channel"),
                 QStringLiteral("[error] Unknown command WAT: Unknown command"),
                 QStringLiteral("[error] Password incorrect"),
                 QStringLiteral("[error] #full: Channel is full"),
                 QStringLiteral("[error] #invite: Invite only"),
                 QStringLiteral("[error] #banned: Banned"),
                 QStringLiteral("[error] #keyed: Bad key"),
                 QStringLiteral("[error] #registered: Registration required"),
                 QStringLiteral("[error] #chat: You're not channel operator"),
             }));
  }

  void commandStatusAndErrorsEmitReadableText() {
    IrcSession session;
    QSignalSpy replies(&session, &IrcSession::replyText);

    session.handleLine(QStringLiteral(":srv 305 me :You are no longer away"));
    session.handleLine(QStringLiteral(":srv 306 me :You have been marked away"));
    session.handleLine(QStringLiteral(":srv 341 me alice #chat :Inviting"));
    session.handleLine(
        QStringLiteral(":srv 405 me #extra :You have joined too many channels"));
    session.handleLine(QStringLiteral(":srv 431 me :No nickname given"));
    session.handleLine(
        QStringLiteral(":srv 432 me bad*nick :Erroneous nickname"));
    session.handleLine(
        QStringLiteral(":srv 437 me reserved :Temporarily unavailable"));
    session.handleLine(
        QStringLiteral(":srv 441 me alice #chat :They aren't on that channel"));
    session.handleLine(
        QStringLiteral(":srv 442 me #chat :You're not on that channel"));
    session.handleLine(
        QStringLiteral(":srv 443 me alice #chat :is already on channel"));
    session.handleLine(
        QStringLiteral(":srv 461 me MODE :Not enough parameters"));
    session.handleLine(QStringLiteral(":srv 472 me z :is unknown mode char"));
    session.handleLine(
        QStringLiteral(":srv 481 me :Permission denied - IRC operator only"));
    session.handleLine(QStringLiteral(":srv 501 me :Unknown MODE flag"));
    session.handleLine(
        QStringLiteral(":srv 502 me :Cannot change mode for other users"));

    QStringList lines;
    for (const QList<QVariant> &reply : replies) {
      lines.append(reply.at(0).toString());
    }
    QCOMPARE(lines,
             QStringList({
                 QStringLiteral("[away] You are no longer away"),
                 QStringLiteral("[away] You have been marked away"),
                 QStringLiteral("[invite] Inviting alice to #chat."),
                 QStringLiteral(
                     "[error] Cannot join #extra: You have joined too many channels"),
                 QStringLiteral("[error] No nickname given"),
                 QStringLiteral("[error] Bad nickname bad*nick: Erroneous nickname"),
                 QStringLiteral(
                     "[error] reserved is unavailable: Temporarily unavailable"),
                 QStringLiteral(
                     "[error] alice is not on #chat: They aren't on that channel"),
                 QStringLiteral(
                     "[error] You are not on #chat: You're not on that channel"),
                 QStringLiteral(
                     "[error] alice is already on #chat: is already on channel"),
                 QStringLiteral("[error] MODE needs more parameters: Not enough parameters"),
                 QStringLiteral("[error] Unknown mode z: is unknown mode char"),
                 QStringLiteral("[error] Permission denied - IRC operator only"),
                 QStringLiteral("[error] Unknown MODE flag"),
                 QStringLiteral("[error] Cannot change mode for other users"),
             }));
  }

  void topicNumericsEmitReadableText() {
    IrcSession session;
    QSignalSpy replies(&session, &IrcSession::replyText);
    QSignalSpy topics(&session, &IrcSession::topicChanged);

    session.handleLine(QStringLiteral(":srv 331 me #empty :No topic is set"));
    session.handleLine(QStringLiteral(":srv 332 me #chat :Current topic"));
    session.handleLine(
        QStringLiteral(":srv 328 me #chat :https://example.org/chat"));

    QStringList lines;
    for (const QList<QVariant> &reply : replies) {
      lines.append(reply.at(0).toString());
    }
    QCOMPARE(lines,
             QStringList({
                 QStringLiteral("[topic] #empty has no topic."),
                 QStringLiteral("[topic] #chat: Current topic"),
                 QStringLiteral("[channel] #chat URL: https://example.org/chat"),
             }));
    QCOMPARE(topics.count(), 2);
    QCOMPARE(topics.at(0).at(0).toString(), QStringLiteral("#empty"));
    QCOMPARE(topics.at(0).at(1).toString(), QString());
    QCOMPARE(topics.at(1).at(0).toString(), QStringLiteral("#chat"));
    QCOMPARE(topics.at(1).at(1).toString(), QStringLiteral("Current topic"));
  }

  void miscellaneousServerEventsEmitReadableText() {
    IrcSession session;
    QSignalSpy replies(&session, &IrcSession::replyText);

    session.handleLine(QStringLiteral(":srv ERROR :Closing Link: ping timeout"));
    session.handleLine(QStringLiteral(":oper!u@h WALLOPS :Network notice"));
    session.handleLine(QStringLiteral(":alice!u@h INVITE bob #secret"));
    session.handleLine(
        QStringLiteral(":srv 333 me #chat alice 1700000000"));
    session.handleLine(QStringLiteral(":srv 329 me #chat 1600000000"));

    QStringList lines;
    for (const QList<QVariant> &reply : replies) {
      lines.append(reply.at(0).toString());
    }
    QCOMPARE(lines,
             QStringList({
                 QStringLiteral("[error] Closing Link: ping timeout"),
                 QStringLiteral("[wallops] oper: Network notice"),
                 QStringLiteral("[invite] alice invited bob to #secret"),
                 QStringLiteral("[topic] #chat set by alice at 1700000000"),
                 QStringLiteral("[channel] #chat created at 1600000000"),
             }));
  }

  void commonRepliesEmitUiTextSignals() {
    IrcSession session;
    QSignalSpy replies(&session, &IrcSession::replyText);
    QSignalSpy bans(&session, &IrcSession::banList);
    QSignalSpy banEnds(&session, &IrcSession::banListEnd);
    QSignalSpy namesEnd(&session, &IrcSession::namesEnd);
    QSignalSpy names(&session, &IrcSession::namesReceived);
    QSignalSpy listReplies(&session, &IrcSession::listReply);
    QSignalSpy listEnd(&session, &IrcSession::listEnd);
    QSignalSpy topics(&session, &IrcSession::topicChanged);
    QSignalSpy joins(&session, &IrcSession::userJoined);
    QSignalSpy parts(&session, &IrcSession::userParted);
    QSignalSpy quits(&session, &IrcSession::userQuit);
    QSignalSpy kicks(&session, &IrcSession::kicked);
    QSignalSpy modeChanges(&session, &IrcSession::modeChanged);
    QSignalSpy channelModes(&session, &IrcSession::channelModeIs);

    session.handleLine(QStringLiteral(":srv 332 me #chan :Current topic"));
    session.handleLine(QStringLiteral(":alice!u@h TOPIC #chan :New topic"));
    session.handleLine(QStringLiteral(":srv 324 me #chan +ntkl secret 50"));
    session.handleLine(QStringLiteral(":alice!u@h MODE #chan +o bob"));
    session.handleLine(
        QStringLiteral(":srv 353 me = #chan :@alice +bob carol"));
    session.handleLine(QStringLiteral(":alice!u@h JOIN #chan"));
    session.handleLine(QStringLiteral(":bob!u@h JOIN :#chan"));
    session.handleLine(QStringLiteral(":bob!u@h PART #chan :bye"));
    session.handleLine(QStringLiteral(":carol!u@h QUIT :gone"));
    session.handleLine(QStringLiteral(
        ":srv 352 me #chan ~user host.isp srv bob H@ :2 Bob Smith"));
    session.handleLine(
        QStringLiteral(":srv 314 me oldbob ~user old.host * :Bob Was Here"));
    session.handleLine(
        QStringLiteral(":srv 367 me #chan *!*@bad.host setter 1700000000"));
    session.handleLine(
        QStringLiteral(":srv 368 me #chan :End of channel ban list"));
    session.handleLine(QStringLiteral(":srv 322 me #linux 42 :Linux talk"));
    session.handleLine(QStringLiteral(":srv 323 me :End of /LIST"));
    session.handleLine(QStringLiteral(":srv 366 me #chan :End of /NAMES list"));
    session.handleLine(QStringLiteral(":op!u@h KICK #chan bob :because"));

    QCOMPARE(replies.count(), 3);
    QCOMPARE(replies.at(0).at(0).toString(),
             QStringLiteral("[topic] #chan: Current topic"));
    QCOMPARE(
        replies.at(1).at(0).toString(),
        QStringLiteral("[who] bob (~user@host.isp) H@ on #chan - Bob Smith"));
    QCOMPARE(
        replies.at(2).at(0).toString(),
        QStringLiteral("[whowas] oldbob was ~user@old.host (Bob Was Here)"));
    QCOMPARE(bans.count(), 1);
    QCOMPARE(bans.at(0).at(0).toString(), QStringLiteral("#chan"));
    QCOMPARE(bans.at(0).at(1).toString(), QStringLiteral("*!*@bad.host"));
    QCOMPARE(bans.at(0).at(2).toString(), QStringLiteral("setter"));
    QCOMPARE(banEnds.at(0).at(0).toString(), QStringLiteral("#chan"));
    QCOMPARE(topics.count(), 2);
    QCOMPARE(topics.at(0).at(0).toString(), QStringLiteral("#chan"));
    QCOMPARE(topics.at(0).at(1).toString(), QStringLiteral("Current topic"));
    QCOMPARE(topics.at(1).at(1).toString(), QStringLiteral("New topic"));
    QCOMPARE(names.count(), 1);
    QCOMPARE(names.at(0).at(0).toString(), QStringLiteral("#chan"));
    QCOMPARE(names.at(0).at(1).toStringList(),
             QStringList({QStringLiteral("@alice"), QStringLiteral("+bob"),
                          QStringLiteral("carol")}));
    QCOMPARE(listReplies.count(), 1);
    QCOMPARE(listReplies.at(0).at(0).toString(), QStringLiteral("#linux"));
    QCOMPARE(listReplies.at(0).at(1).toInt(), 42);
    QCOMPARE(listReplies.at(0).at(2).toString(), QStringLiteral("Linux talk"));
    QCOMPARE(listEnd.count(), 1);
    QCOMPARE(joins.count(), 2);
    QCOMPARE(joins.at(0).at(0).toString(), QStringLiteral("#chan"));
    QCOMPARE(joins.at(0).at(1).toString(), QStringLiteral("alice"));
    QCOMPARE(joins.at(1).at(0).toString(), QStringLiteral("#chan"));
    QCOMPARE(joins.at(1).at(1).toString(), QStringLiteral("bob"));
    QCOMPARE(parts.count(), 1);
    QCOMPARE(parts.at(0).at(0).toString(), QStringLiteral("#chan"));
    QCOMPARE(parts.at(0).at(1).toString(), QStringLiteral("bob"));
    QCOMPARE(parts.at(0).at(2).toString(), QStringLiteral("bye"));
    QCOMPARE(quits.count(), 1);
    QCOMPARE(quits.at(0).at(0).toString(), QStringLiteral("carol"));
    QCOMPARE(quits.at(0).at(1).toString(), QStringLiteral("gone"));
    QCOMPARE(namesEnd.at(0).at(0).toString(), QStringLiteral("#chan"));
    QCOMPARE(kicks.at(0).at(0).toString(), QStringLiteral("#chan"));
    QCOMPARE(kicks.at(0).at(1).toString(), QStringLiteral("bob"));
    QCOMPARE(kicks.at(0).at(2).toString(), QStringLiteral("because"));
    QCOMPARE(channelModes.count(), 1);
    QCOMPARE(channelModes.at(0).at(0).toString(), QStringLiteral("#chan"));
    QCOMPARE(channelModes.at(0).at(1).toString(),
             QStringLiteral("+ntkl secret 50"));
    QCOMPARE(modeChanges.count(), 1);
    QCOMPARE(modeChanges.at(0).at(0).toString(), QStringLiteral("#chan"));
    QCOMPARE(modeChanges.at(0).at(1).toString(), QStringLiteral("alice"));
    QCOMPARE(modeChanges.at(0).at(2).toString(), QStringLiteral("+o bob"));
  }

  void unhandledNumericsAreSurfacedAsStatusText() {
    // Numerics without an explicit handler (LUSERS, server errors, …) must not
    // vanish — the Python client shows them as status text.
    IrcSession session;
    QSignalSpy status(&session, &IrcSession::systemText);

    session.handleLine(
        QStringLiteral(":srv 251 bob :There are 42 users on 3 servers"));
    session.handleLine(
        QStringLiteral(":srv 465 bob :You are banned from this server"));

    QCOMPARE(status.count(), 2);
    QCOMPARE(status.at(0).at(0).toString(),
             QStringLiteral("There are 42 users on 3 servers"));
    QCOMPARE(status.at(1).at(0).toString(),
             QStringLiteral("You are banned from this server"));
  }

  void isupportLineIsAlsoShownAsStatusText() {
    // 005 tokens are stored AND the line is surfaced (Python falls through).
    IrcSession session;
    QSignalSpy status(&session, &IrcSession::systemText);

    session.handleLine(
        QStringLiteral(":srv 005 bob NETWORK=Libera :are supported"));

    QCOMPARE(session.isupport().value(QStringLiteral("NETWORK")),
             QStringLiteral("Libera"));
    QCOMPARE(status.count(), 1);
    QCOMPARE(status.at(0).at(0).toString(), QStringLiteral("are supported"));
  }

  void ctcpAutoRepliesAreRateLimited() {
    // A burst of CTCP requests must not produce a 1:1 flood of replies.
    IrcSession session;
    QList<QByteArray> writes;
    session.setConnected(true);
    session.setWriter([&writes](const QByteArray &payload) {
      writes.append(payload);
      return payload.size();
    });

    session.handleLine(QStringLiteral(":alice!u@h PRIVMSG bob :") +
                       ctcpPayload(QStringLiteral("VERSION")));
    session.handleLine(QStringLiteral(":mallory!u@h PRIVMSG bob :") +
                       ctcpPayload(QStringLiteral("VERSION")));
    session.handleLine(QStringLiteral(":mallory!u@h PRIVMSG bob :") +
                       ctcpPayload(QStringLiteral("TIME")));

    // Only the first request inside the 1s window draws an auto-reply.
    QCOMPARE(writes.size(), 1);
    QVERIFY(QString::fromUtf8(writes.first()).startsWith(
        QStringLiteral("NOTICE alice :")));
  }
};

QTEST_MAIN(IrcSessionTest)

#include "irc_session_test.moc"
