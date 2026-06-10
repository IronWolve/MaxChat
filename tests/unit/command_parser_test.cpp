#include "irc/CommandParser.h"

#include <QtTest/QtTest>

using maxchat::irc::normalizeChannelTargets;
using maxchat::irc::parseUserCommand;
using maxchat::irc::UserCommandType;

class CommandParserTest final : public QObject {
  Q_OBJECT

private slots:
  void textUsesCurrentTarget() {
    const auto parsed = parseUserCommand(QStringLiteral("hello there"),
                                         QStringLiteral("#chat"));

    QCOMPARE(parsed.type, UserCommandType::Text);
    QCOMPARE(parsed.targets, QStringList({QStringLiteral("#chat")}));
    QCOMPARE(parsed.text, QStringLiteral("hello there"));
  }

  void joinNormalizesMultipleChannels() {
    const auto parsed =
        parseUserCommand(QStringLiteral("/join chat,#linux &local"));

    QCOMPARE(parsed.type, UserCommandType::Join);
    QCOMPARE(parsed.targets,
             QStringList({QStringLiteral("#chat"), QStringLiteral("#linux"),
                          QStringLiteral("&local")}));
  }

  void partDefaultsToCurrentTargetAndKeepsReason() {
    const auto parsed = parseUserCommand(QStringLiteral("/part leaving now"),
                                         QStringLiteral("#chat"));

    QCOMPARE(parsed.type, UserCommandType::Part);
    QCOMPARE(parsed.targets, QStringList({QStringLiteral("#chat")}));
    QCOMPARE(parsed.text, QStringLiteral("leaving now"));

    const auto defaulted =
        parseUserCommand(QStringLiteral("/part"), QStringLiteral("#chat"));
    QCOMPARE(defaulted.type, UserCommandType::Part);
    QCOMPARE(defaulted.targets, QStringList({QStringLiteral("#chat")}));

    const auto leave = parseUserCommand(QStringLiteral("/leave #other done"),
                                        QStringLiteral("#chat"));
    QCOMPARE(leave.type, UserCommandType::Part);
    QCOMPARE(leave.command, QStringLiteral("leave"));
    QCOMPARE(leave.targets, QStringList({QStringLiteral("#other")}));
    QCOMPARE(leave.text, QStringLiteral("done"));
  }

  void cycleDefaultsToCurrentChannelOrUsesExplicitChannel() {
    const auto current =
        parseUserCommand(QStringLiteral("/cycle brb"), QStringLiteral("#chat"));
    QCOMPARE(current.type, UserCommandType::Cycle);
    QCOMPARE(current.targets, QStringList({QStringLiteral("#chat")}));
    QCOMPARE(current.text, QStringLiteral("brb"));

    const auto explicitTarget = parseUserCommand(
        QStringLiteral("/hop #other reconnecting"), QStringLiteral("#chat"));
    QCOMPARE(explicitTarget.type, UserCommandType::Cycle);
    QCOMPARE(explicitTarget.command, QStringLiteral("hop"));
    QCOMPARE(explicitTarget.targets, QStringList({QStringLiteral("#other")}));
    QCOMPARE(explicitTarget.text, QStringLiteral("reconnecting"));
  }

  void privateMessageQueryNoticeAndActionParseTargets() {
    const auto msg = parseUserCommand(QStringLiteral("/msg alice hello there"));
    QCOMPARE(msg.type, UserCommandType::PrivateMessage);
    QCOMPARE(msg.targets, QStringList({QStringLiteral("alice")}));
    QCOMPARE(msg.text, QStringLiteral("hello there"));

    const auto privmsg =
        parseUserCommand(QStringLiteral("/privmsg #chat hello there"));
    QCOMPARE(privmsg.type, UserCommandType::PrivateMessage);
    QCOMPARE(privmsg.targets, QStringList({QStringLiteral("#chat")}));
    QCOMPARE(privmsg.text, QStringLiteral("hello there"));

    const auto query = parseUserCommand(QStringLiteral("/query alice"));
    QCOMPARE(query.type, UserCommandType::Query);
    QCOMPARE(query.targets, QStringList({QStringLiteral("alice")}));
    QCOMPARE(query.text, QString());

    const auto queryWithMessage =
        parseUserCommand(QStringLiteral("/query alice hello"));
    QCOMPARE(queryWithMessage.type, UserCommandType::Query);
    QCOMPARE(queryWithMessage.targets, QStringList({QStringLiteral("alice")}));
    QCOMPARE(queryWithMessage.text, QStringLiteral("hello"));

    const auto notice =
        parseUserCommand(QStringLiteral("/notice #chat server reboot soon"));
    QCOMPARE(notice.type, UserCommandType::Notice);
    QCOMPARE(notice.targets, QStringList({QStringLiteral("#chat")}));
    QCOMPARE(notice.text, QStringLiteral("server reboot soon"));

    const auto action =
        parseUserCommand(QStringLiteral("/me waves"), QStringLiteral("#chat"));
    QCOMPARE(action.type, UserCommandType::Action);
    QCOMPARE(action.targets, QStringList({QStringLiteral("#chat")}));
    QCOMPARE(action.text, QStringLiteral("waves"));

    const auto ctcp = parseUserCommand(QStringLiteral("/ctcp alice ping 123"));
    QCOMPARE(ctcp.type, UserCommandType::Ctcp);
    QCOMPARE(ctcp.targets, QStringList({QStringLiteral("alice")}));
    QCOMPARE(ctcp.rawLine, QStringLiteral("PING"));
    QCOMPARE(ctcp.text, QStringLiteral("123"));
  }

  void broadcastAndOpNoticeCommandsUseCurrentChannels() {
    const auto amsg = parseUserCommand(QStringLiteral("/amsg hello everyone"),
                                       QStringLiteral("#chat"));
    QCOMPARE(amsg.type, UserCommandType::BroadcastMessage);
    QCOMPARE(amsg.text, QStringLiteral("hello everyone"));

    const auto ame =
        parseUserCommand(QStringLiteral("/ame waves"), QStringLiteral("#chat"));
    QCOMPARE(ame.type, UserCommandType::BroadcastAction);
    QCOMPARE(ame.text, QStringLiteral("waves"));

    const auto onotice = parseUserCommand(QStringLiteral("/onotice ops only"),
                                          QStringLiteral("#chat"));
    QCOMPARE(onotice.type, UserCommandType::OpNotice);
    QCOMPARE(onotice.targets, QStringList({QStringLiteral("#chat")}));
    QCOMPARE(onotice.text, QStringLiteral("ops only"));
  }

  void serviceCommandsMapToCanonicalTargets() {
    const auto nickserv =
        parseUserCommand(QStringLiteral("/nickserv identify secret"));
    QCOMPARE(nickserv.type, UserCommandType::ServiceMessage);
    QCOMPARE(nickserv.targets, QStringList({QStringLiteral("NickServ")}));
    QCOMPARE(nickserv.text, QStringLiteral("identify secret"));

    const auto chanserv = parseUserCommand(QStringLiteral("/cs op #chat bob"));
    QCOMPARE(chanserv.type, UserCommandType::ServiceMessage);
    QCOMPARE(chanserv.targets, QStringList({QStringLiteral("ChanServ")}));
    QCOMPARE(chanserv.text, QStringLiteral("op #chat bob"));

    const auto memoserv = parseUserCommand(QStringLiteral("/ms list"));
    QCOMPARE(memoserv.type, UserCommandType::ServiceMessage);
    QCOMPARE(memoserv.targets, QStringList({QStringLiteral("MemoServ")}));
    QCOMPARE(memoserv.text, QStringLiteral("list"));

    const auto operserv = parseUserCommand(QStringLiteral("/os help"));
    QCOMPARE(operserv.type, UserCommandType::ServiceMessage);
    QCOMPARE(operserv.targets, QStringList({QStringLiteral("OperServ")}));
    QCOMPARE(operserv.text, QStringLiteral("help"));

    const auto hostserv = parseUserCommand(QStringLiteral("/hs on"));
    QCOMPARE(hostserv.type, UserCommandType::ServiceMessage);
    QCOMPARE(hostserv.targets, QStringList({QStringLiteral("HostServ")}));
    QCOMPARE(hostserv.text, QStringLiteral("on"));

    const auto identify = parseUserCommand(QStringLiteral("/identify secret"));
    QCOMPARE(identify.type, UserCommandType::ServiceMessage);
    QCOMPARE(identify.targets, QStringList({QStringLiteral("NickServ")}));
    QCOMPARE(identify.text, QStringLiteral("IDENTIFY secret"));

    const auto ghost =
        parseUserCommand(QStringLiteral("/ghost oldnick secret"));
    QCOMPARE(ghost.type, UserCommandType::ServiceMessage);
    QCOMPARE(ghost.targets, QStringList({QStringLiteral("NickServ")}));
    QCOMPARE(ghost.text, QStringLiteral("GHOST oldnick secret"));
  }

  void nickWhoisRawQuitAndUnknownCommands() {
    const auto nick = parseUserCommand(QStringLiteral("/nick newnick"));
    QCOMPARE(nick.type, UserCommandType::Nick);
    QCOMPARE(nick.text, QStringLiteral("newnick"));

    const auto whois = parseUserCommand(QStringLiteral("/whois alice extra"));
    QCOMPARE(whois.type, UserCommandType::Whois);
    QCOMPARE(whois.targets, QStringList({QStringLiteral("alice")}));

    const auto who = parseUserCommand(QStringLiteral("/who #chat"));
    QCOMPARE(who.type, UserCommandType::Who);
    QCOMPARE(who.targets, QStringList({QStringLiteral("#chat")}));

    const auto whowas = parseUserCommand(QStringLiteral("/whowas oldnick"));
    QCOMPARE(whowas.type, UserCommandType::Whowas);
    QCOMPARE(whowas.targets, QStringList({QStringLiteral("oldnick")}));

    const auto raw = parseUserCommand(QStringLiteral("/raw MODE #chat +b"));
    QCOMPARE(raw.type, UserCommandType::Raw);
    QCOMPARE(raw.rawLine, QStringLiteral("MODE #chat +b"));

    const auto quit = parseUserCommand(QStringLiteral("/quit later"));
    QCOMPARE(quit.type, UserCommandType::Quit);
    QCOMPARE(quit.text, QStringLiteral("later"));

    const auto away = parseUserCommand(QStringLiteral("/away lunch"));
    QCOMPARE(away.type, UserCommandType::Away);
    QCOMPARE(away.text, QStringLiteral("lunch"));

    const auto back = parseUserCommand(QStringLiteral("/back"));
    QCOMPARE(back.type, UserCommandType::Away);
    QCOMPARE(back.text, QString());

    const auto clear = parseUserCommand(QStringLiteral("/clear"));
    QCOMPARE(clear.type, UserCommandType::Clear);

    const auto clearAll = parseUserCommand(QStringLiteral("/clearall"));
    QCOMPARE(clearAll.type, UserCommandType::ClearAll);

    const auto close = parseUserCommand(QStringLiteral("/close"));
    QCOMPARE(close.type, UserCommandType::Close);

    const auto disconnect = parseUserCommand(QStringLiteral("/disconnect"));
    QCOMPARE(disconnect.type, UserCommandType::Disconnect);

    const auto reconnect = parseUserCommand(QStringLiteral("/reconnect"));
    QCOMPARE(reconnect.type, UserCommandType::Reconnect);

    const auto connect =
        parseUserCommand(QStringLiteral("/connect Libera.Chat"));
    QCOMPARE(connect.type, UserCommandType::Connect);
    QCOMPARE(connect.command, QStringLiteral("connect"));
    QCOMPARE(connect.targets, QStringList({QStringLiteral("Libera.Chat")}));
    QCOMPARE(connect.text, QString());

    const auto server = parseUserCommand(
        QStringLiteral("/server irc.example.net +6697 server-secret"));
    QCOMPARE(server.type, UserCommandType::Connect);
    QCOMPARE(server.command, QStringLiteral("server"));
    QCOMPARE(server.targets, QStringList({QStringLiteral("irc.example.net")}));
    QCOMPARE(server.text, QStringLiteral("+6697 server-secret"));

    const auto lag = parseUserCommand(QStringLiteral("/lag"));
    QCOMPARE(lag.type, UserCommandType::Lag);

    const auto uptime = parseUserCommand(QStringLiteral("/uptime"));
    QCOMPARE(uptime.type, UserCommandType::Uptime);

    const auto netinfo = parseUserCommand(QStringLiteral("/netinfo"));
    QCOMPARE(netinfo.type, UserCommandType::NetInfo);

    const auto list = parseUserCommand(QStringLiteral("/list *linux*"));
    QCOMPARE(list.type, UserCommandType::ChannelList);
    QCOMPARE(list.text, QStringLiteral("*linux*"));

    const auto wallops =
        parseUserCommand(QStringLiteral("/wallops network notice"));
    QCOMPARE(wallops.type, UserCommandType::Raw);
    QCOMPARE(wallops.rawLine, QStringLiteral("WALLOPS :network notice"));

    const auto oper = parseUserCommand(QStringLiteral("/oper user pass"));
    QCOMPARE(oper.type, UserCommandType::Raw);
    QCOMPARE(oper.rawLine, QStringLiteral("OPER user pass"));

    const auto kill = parseUserCommand(QStringLiteral("/kill badnick reason"));
    QCOMPARE(kill.type, UserCommandType::Raw);
    QCOMPARE(kill.rawLine, QStringLiteral("KILL badnick :reason"));
  }

  void namesInviteAndKickUseCurrentChannelWhenNeeded() {
    const auto names =
        parseUserCommand(QStringLiteral("/names"), QStringLiteral("#chat"));
    QCOMPARE(names.type, UserCommandType::Names);
    QCOMPARE(names.targets, QStringList({QStringLiteral("#chat")}));

    const auto invite = parseUserCommand(QStringLiteral("/invite bob"),
                                         QStringLiteral("#chat"));
    QCOMPARE(invite.type, UserCommandType::Invite);
    QCOMPARE(invite.targets,
             QStringList({QStringLiteral("bob"), QStringLiteral("#chat")}));

    const auto explicitInvite = parseUserCommand(
        QStringLiteral("/invite bob #other"), QStringLiteral("#chat"));
    QCOMPARE(explicitInvite.type, UserCommandType::Invite);
    QCOMPARE(explicitInvite.targets,
             QStringList({QStringLiteral("bob"), QStringLiteral("#other")}));

    const auto kick = parseUserCommand(QStringLiteral("/kick bob take five"),
                                       QStringLiteral("#chat"));
    QCOMPARE(kick.type, UserCommandType::Kick);
    QCOMPARE(kick.targets,
             QStringList({QStringLiteral("#chat"), QStringLiteral("bob")}));
    QCOMPARE(kick.text, QStringLiteral("take five"));

    const auto explicitKick = parseUserCommand(
        QStringLiteral("/kick #other bob take five"), QStringLiteral("#chat"));
    QCOMPARE(explicitKick.type, UserCommandType::Kick);
    QCOMPARE(explicitKick.targets,
             QStringList({QStringLiteral("#other"), QStringLiteral("bob")}));
    QCOMPARE(explicitKick.text, QStringLiteral("take five"));

    const auto ban =
        parseUserCommand(QStringLiteral("/ban bob"), QStringLiteral("#chat"));
    QCOMPARE(ban.type, UserCommandType::Ban);
    QCOMPARE(ban.targets,
             QStringList({QStringLiteral("#chat"), QStringLiteral("bob"),
                          QStringLiteral("bob!*@*")}));

    const auto explicitBan = parseUserCommand(
        QStringLiteral("/ban #other *!*@bad.host"), QStringLiteral("#chat"));
    QCOMPARE(explicitBan.type, UserCommandType::Ban);
    QCOMPARE(
        explicitBan.targets,
        QStringList({QStringLiteral("#other"), QStringLiteral("*!*@bad.host"),
                     QStringLiteral("*!*@bad.host")}));

    const auto kickBan = parseUserCommand(QStringLiteral("/kb bob enough"),
                                          QStringLiteral("#chat"));
    QCOMPARE(kickBan.type, UserCommandType::KickBan);
    QCOMPARE(kickBan.targets,
             QStringList({QStringLiteral("#chat"), QStringLiteral("bob"),
                          QStringLiteral("bob!*@*")}));
    QCOMPARE(kickBan.text, QStringLiteral("enough"));
  }

  void topicUsesExplicitOrCurrentTarget() {
    const auto query =
        parseUserCommand(QStringLiteral("/topic"), QStringLiteral("#chat"));
    QCOMPARE(query.type, UserCommandType::Topic);
    QCOMPARE(query.targets, QStringList({QStringLiteral("#chat")}));
    QCOMPARE(query.text, QString());

    const auto setCurrent = parseUserCommand(
        QStringLiteral("/topic New topic here"), QStringLiteral("#chat"));
    QCOMPARE(setCurrent.type, UserCommandType::Topic);
    QCOMPARE(setCurrent.targets, QStringList({QStringLiteral("#chat")}));
    QCOMPARE(setCurrent.text, QStringLiteral("New topic here"));

    const auto setOther = parseUserCommand(
        QStringLiteral("/topic #other Other topic"), QStringLiteral("#chat"));
    QCOMPARE(setOther.type, UserCommandType::Topic);
    QCOMPARE(setOther.targets, QStringList({QStringLiteral("#other")}));
    QCOMPARE(setOther.text, QStringLiteral("Other topic"));
  }

  void modeDefaultsToCurrentChannelWhenNeeded() {
    const auto currentChannel =
        parseUserCommand(QStringLiteral("/mode +m"), QStringLiteral("#chat"));
    QCOMPARE(currentChannel.type, UserCommandType::Mode);
    QCOMPARE(currentChannel.rawLine, QStringLiteral("MODE #chat +m"));

    const auto explicitChannel = parseUserCommand(
        QStringLiteral("/mode #other +o alice"), QStringLiteral("#chat"));
    QCOMPARE(explicitChannel.type, UserCommandType::Mode);
    QCOMPARE(explicitChannel.rawLine, QStringLiteral("MODE #other +o alice"));

    const auto userMode = parseUserCommand(QStringLiteral("/mode alice +i"));
    QCOMPARE(userMode.type, UserCommandType::Mode);
    QCOMPARE(userMode.rawLine, QStringLiteral("MODE alice +i"));

    const auto op = parseUserCommand(QStringLiteral("/op alice bob"),
                                     QStringLiteral("#chat"));
    QCOMPARE(op.type, UserCommandType::Mode);
    QCOMPARE(op.rawLine, QStringLiteral("MODE #chat +oo alice bob"));

    const auto devoice = parseUserCommand(QStringLiteral("/devoice alice"),
                                          QStringLiteral("#chat"));
    QCOMPARE(devoice.type, UserCommandType::Mode);
    QCOMPARE(devoice.rawLine, QStringLiteral("MODE #chat -v alice"));
  }

  void localUtilityCommandsStayLocal() {
    const auto listAliases = parseUserCommand(QStringLiteral("/alias"));
    QCOMPARE(listAliases.type, UserCommandType::Alias);
    QCOMPARE(listAliases.targets, QStringList());

    const auto alias =
        parseUserCommand(QStringLiteral("/alias j join $1-"));
    QCOMPARE(alias.type, UserCommandType::Alias);
    QCOMPARE(alias.targets, QStringList({QStringLiteral("j")}));
    QCOMPARE(alias.text, QStringLiteral("join $1-"));

    const auto showAlias = parseUserCommand(QStringLiteral("/alias j"));
    QCOMPARE(showAlias.type, UserCommandType::Alias);
    QCOMPARE(showAlias.targets, QStringList({QStringLiteral("j")}));
    QCOMPARE(showAlias.text, QString());

    const auto unalias = parseUserCommand(QStringLiteral("/unalias j"));
    QCOMPARE(unalias.type, UserCommandType::Unalias);
    QCOMPARE(unalias.targets, QStringList({QStringLiteral("j")}));

    const auto ignore = parseUserCommand(QStringLiteral("/ignore spammer"));
    QCOMPARE(ignore.type, UserCommandType::Ignore);
    QCOMPARE(ignore.text, QStringLiteral("spammer"));

    const auto listIgnores = parseUserCommand(QStringLiteral("/ignore"));
    QCOMPARE(listIgnores.type, UserCommandType::Ignore);
    QCOMPARE(listIgnores.text, QString());

    const auto unignore =
        parseUserCommand(QStringLiteral("/unignore spammer!*@*"));
    QCOMPARE(unignore.type, UserCommandType::Unignore);
    QCOMPARE(unignore.text, QStringLiteral("spammer!*@*"));

    const auto mute =
        parseUserCommand(QStringLiteral("/mute"), QStringLiteral("#chat"));
    QCOMPARE(mute.type, UserCommandType::Mute);
    QCOMPARE(mute.targets, QStringList({QStringLiteral("#chat")}));

    const auto explicitMute =
        parseUserCommand(QStringLiteral("/mute support"));
    QCOMPARE(explicitMute.type, UserCommandType::Mute);
    QCOMPARE(explicitMute.targets, QStringList({QStringLiteral("#support")}));

    const auto unmute =
        parseUserCommand(QStringLiteral("/unmute #chat"));
    QCOMPARE(unmute.type, UserCommandType::Unmute);
    QCOMPARE(unmute.targets, QStringList({QStringLiteral("#chat")}));

    const auto unmuteTypo =
        parseUserCommand(QStringLiteral("/umite #chat"));
    QCOMPARE(unmuteTypo.type, UserCommandType::Unmute);
    QCOMPARE(unmuteTypo.targets, QStringList({QStringLiteral("#chat")}));

    const auto notify = parseUserCommand(QStringLiteral("/notify alice"));
    QCOMPARE(notify.type, UserCommandType::Notify);
    QCOMPARE(notify.text, QStringLiteral("alice"));

    const auto listNotify = parseUserCommand(QStringLiteral("/notify"));
    QCOMPARE(listNotify.type, UserCommandType::Notify);
    QCOMPARE(listNotify.text, QString());

    const auto unnotify = parseUserCommand(QStringLiteral("/unnotify alice"));
    QCOMPARE(unnotify.type, UserCommandType::Unnotify);
    QCOMPARE(unnotify.text, QStringLiteral("alice"));

    const auto sysinfo = parseUserCommand(QStringLiteral("/sysinfo send"));
    QCOMPARE(sysinfo.type, UserCommandType::SysInfo);
    QCOMPARE(sysinfo.text, QStringLiteral("send"));

    const auto sys = parseUserCommand(QStringLiteral("/sys"));
    QCOMPARE(sys.type, UserCommandType::SysInfo);

    const auto scripts = parseUserCommand(QStringLiteral("/scripts"));
    QCOMPARE(scripts.type, UserCommandType::Scripts);

    const auto load = parseUserCommand(QStringLiteral("/load sysinfo"));
    QCOMPARE(load.type, UserCommandType::Scripts);
    QCOMPARE(load.command, QStringLiteral("load"));
    QCOMPARE(load.text, QStringLiteral("sysinfo"));

    const auto help = parseUserCommand(QStringLiteral("/help ops"));
    QCOMPARE(help.type, UserCommandType::Help);
    QCOMPARE(help.text, QStringLiteral("ops"));

    const auto question = parseUserCommand(QStringLiteral("/? local"));
    QCOMPARE(question.type, UserCommandType::Help);
    QCOMPARE(question.text, QStringLiteral("local"));
  }

  void missingRequiredArgumentsReportUsage() {
    for (const QString &input :
         {QStringLiteral("/join"),         QStringLiteral("/msg alice"),
          QStringLiteral("/query"),        QStringLiteral("/notice alice"),
          QStringLiteral("/me"),           QStringLiteral("/nick"),
          QStringLiteral("/whois"),        QStringLiteral("/who"),
          QStringLiteral("/whowas"),       QStringLiteral("/names"),
          QStringLiteral("/topic"),        QStringLiteral("/mode"),
          QStringLiteral("/invite"),       QStringLiteral("/invite bob"),
          QStringLiteral("/kick"),         QStringLiteral("/kick bob"),
          QStringLiteral("/amsg"),         QStringLiteral("/ame"),
          QStringLiteral("/onotice"),      QStringLiteral("/onotice ops"),
          QStringLiteral("/op alice"),     QStringLiteral("/deop alice"),
          QStringLiteral("/voice alice"),  QStringLiteral("/devoice alice"),
          QStringLiteral("/halfop alice"), QStringLiteral("/dehalfop alice"),
          QStringLiteral("/ban"),          QStringLiteral("/ban bob"),
          QStringLiteral("/kickban"),      QStringLiteral("/kickban bob"),
          QStringLiteral("/kb"),           QStringLiteral("/kb bob"),
          QStringLiteral("/unalias"),      QStringLiteral("/unignore"),
          QStringLiteral("/mute"),         QStringLiteral("/unmute"),
          QStringLiteral("/unnotify"),
          QStringLiteral("/ctcp alice"),   QStringLiteral("/ctcp"),
          QStringLiteral("/cycle"),        QStringLiteral("/hop"),
          QStringLiteral("/nickserv"),     QStringLiteral("/ns"),
          QStringLiteral("/chanserv"),     QStringLiteral("/cs"),
          QStringLiteral("/memoserv"),     QStringLiteral("/ms"),
          QStringLiteral("/operserv"),     QStringLiteral("/os"),
          QStringLiteral("/hostserv"),     QStringLiteral("/hs"),
          QStringLiteral("/identify"),     QStringLiteral("/id"),
          QStringLiteral("/ghost"),        QStringLiteral("/wallops"),
          QStringLiteral("/oper"),         QStringLiteral("/kill"),
          QStringLiteral("/raw")}) {
      const auto parsed = parseUserCommand(input);
      QCOMPARE(parsed.type, UserCommandType::Error);
      QVERIFY2(parsed.errorText.startsWith(QStringLiteral("Usage:")),
               qPrintable(parsed.errorText));
    }
  }

  void channelTargetHelperAddsPrefixes() {
    QCOMPARE(normalizeChannelTargets(QStringLiteral("one,#two &three")),
             QStringList({QStringLiteral("#one"), QStringLiteral("#two"),
                          QStringLiteral("&three")}));
  }

  void soundCommandTargetsCurrentBufferAndNeedsFile() {
    const auto parsed = parseUserCommand(
        QStringLiteral("/sound horn.wav hey all"), QStringLiteral("#chat"));
    QCOMPARE(parsed.type, UserCommandType::Sound);
    QCOMPARE(parsed.targets, QStringList({QStringLiteral("#chat")}));
    QCOMPARE(parsed.rawLine, QStringLiteral("horn.wav"));
    QCOMPARE(parsed.text, QStringLiteral("hey all"));

    // No file, or no current buffer → usage error (not a raw passthrough).
    QCOMPARE(parseUserCommand(QStringLiteral("/sound"), QStringLiteral("#chat"))
                 .type,
             UserCommandType::Error);
    QCOMPARE(
        parseUserCommand(QStringLiteral("/sound horn.wav"), QString()).type,
        UserCommandType::Error);
  }
};

QTEST_APPLESS_MAIN(CommandParserTest)

#include "command_parser_test.moc"
