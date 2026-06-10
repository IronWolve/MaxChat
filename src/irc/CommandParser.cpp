#include "irc/CommandParser.h"

#include <QRegularExpression>

namespace maxchat::irc {

namespace {

struct SplitText {
  QString first;
  QString rest;
};

SplitText splitFirstToken(const QString &text) {
  const QString trimmed = text.trimmed();
  if (trimmed.isEmpty()) {
    return {};
  }

  const int firstSpace =
      trimmed.indexOf(QRegularExpression(QStringLiteral("\\s")));
  if (firstSpace < 0) {
    return {trimmed, QString()};
  }
  return {trimmed.left(firstSpace), trimmed.mid(firstSpace + 1).trimmed()};
}

UserCommand errorCommand(const QString &usage) {
  UserCommand parsed;
  parsed.type = UserCommandType::Error;
  parsed.errorText = usage;
  return parsed;
}

QString channelTarget(QString target) {
  target = target.trimmed();
  if (target.isEmpty()) {
    return {};
  }
  if (!target.startsWith(QLatin1Char('#')) &&
      !target.startsWith(QLatin1Char('&'))) {
    target.prepend(QLatin1Char('#'));
  }
  return target;
}

bool isExplicitChannel(const QString &target) {
  return target.startsWith(QLatin1Char('#')) ||
         target.startsWith(QLatin1Char('&'));
}

QString repeatedModeFlag(const QChar sign, const QChar mode, const int count) {
  if (count <= 0) {
    return {};
  }
  QString modes;
  modes.reserve(count + 1);
  modes.append(sign);
  for (int index = 0; index < count; ++index) {
    modes.append(mode);
  }
  return modes;
}

QString banMaskForNickOrMask(const QString &nickOrMask) {
  const QString trimmed = nickOrMask.trimmed();
  if (trimmed.contains(QLatin1Char('!')) ||
      trimmed.contains(QLatin1Char('@')) ||
      trimmed.contains(QLatin1Char('*'))) {
    return trimmed;
  }
  return QStringLiteral("%1!*@*").arg(trimmed);
}

QString serviceTargetForCommand(const QString &command) {
  if (command == QStringLiteral("nickserv") ||
      command == QStringLiteral("ns") ||
      command == QStringLiteral("identify") ||
      command == QStringLiteral("id") || command == QStringLiteral("ghost")) {
    return QStringLiteral("NickServ");
  }
  if (command == QStringLiteral("chanserv") ||
      command == QStringLiteral("cs")) {
    return QStringLiteral("ChanServ");
  }
  if (command == QStringLiteral("memoserv") ||
      command == QStringLiteral("ms")) {
    return QStringLiteral("MemoServ");
  }
  if (command == QStringLiteral("operserv") ||
      command == QStringLiteral("os")) {
    return QStringLiteral("OperServ");
  }
  if (command == QStringLiteral("hostserv") ||
      command == QStringLiteral("hs")) {
    return QStringLiteral("HostServ");
  }
  return {};
}

} // namespace

QStringList supportedCommandNames() {
  return {
      QStringLiteral("join"),       QStringLiteral("part"),
      QStringLiteral("leave"),      QStringLiteral("cycle"),
      QStringLiteral("hop"),        QStringLiteral("msg"),
      QStringLiteral("query"),      QStringLiteral("privmsg"),
      QStringLiteral("notice"),     QStringLiteral("me"),
      QStringLiteral("amsg"),       QStringLiteral("ame"),
      QStringLiteral("shrug"),      QStringLiteral("tableflip"),
      QStringLiteral("flip"),       QStringLiteral("unflip"),
      QStringLiteral("lenny"),      QStringLiteral("disapprove"),
      QStringLiteral("onotice"),    QStringLiteral("ctcp"),
      QStringLiteral("sound"),
      QStringLiteral("nickserv"),   QStringLiteral("ns"),
      QStringLiteral("chanserv"),   QStringLiteral("cs"),
      QStringLiteral("memoserv"),   QStringLiteral("ms"),
      QStringLiteral("operserv"),   QStringLiteral("os"),
      QStringLiteral("hostserv"),   QStringLiteral("hs"),
      QStringLiteral("identify"),   QStringLiteral("id"),
      QStringLiteral("ghost"),      QStringLiteral("nick"),
      QStringLiteral("whois"),      QStringLiteral("who"),
      QStringLiteral("whowas"),     QStringLiteral("names"),
      QStringLiteral("list"),       QStringLiteral("topic"),
      QStringLiteral("mode"),       QStringLiteral("invite"),
      QStringLiteral("kick"),       QStringLiteral("op"),
      QStringLiteral("deop"),       QStringLiteral("voice"),
      QStringLiteral("devoice"),    QStringLiteral("halfop"),
      QStringLiteral("dehalfop"),   QStringLiteral("ban"),
      QStringLiteral("kickban"),    QStringLiteral("kb"),
      QStringLiteral("wallops"),    QStringLiteral("oper"),
      QStringLiteral("kill"),       QStringLiteral("away"),
      QStringLiteral("back"),       QStringLiteral("raw"),
      QStringLiteral("alias"),      QStringLiteral("unalias"),
      QStringLiteral("ignore"),     QStringLiteral("unignore"),
      QStringLiteral("mute"),       QStringLiteral("unmute"),
      QStringLiteral("umite"),
      QStringLiteral("notify"),     QStringLiteral("unnotify"),
      QStringLiteral("lag"),        QStringLiteral("uptime"),
      QStringLiteral("netinfo"),    QStringLiteral("sysinfo"),
      QStringLiteral("sys"),        QStringLiteral("scripts"),
      QStringLiteral("load"),       QStringLiteral("unload"),
      QStringLiteral("reload"),     QStringLiteral("help"),
      QStringLiteral("?"),          QStringLiteral("clear"),
      QStringLiteral("clearall"),   QStringLiteral("close"),
      QStringLiteral("disconnect"), QStringLiteral("reconnect"),
      QStringLiteral("connect"),    QStringLiteral("server"),
      QStringLiteral("quit"),
  };
}

QStringList normalizeChannelTargets(const QString &text) {
  QString normalized = text;
  normalized.replace(QLatin1Char(','), QLatin1Char(' '));

  QStringList targets;
  const QStringList tokens = normalized.split(
      QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
  for (const QString &token : tokens) {
    const QString target = channelTarget(token);
    if (!target.isEmpty()) {
      targets.append(target);
    }
  }
  return targets;
}

UserCommand parseUserCommand(const QString &input,
                             const QString &currentTarget) {
  const QString trimmed = input.trimmed();
  if (trimmed.isEmpty()) {
    return {};
  }

  if (!trimmed.startsWith(QLatin1Char('/'))) {
    UserCommand parsed;
    parsed.type = UserCommandType::Text;
    parsed.targets =
        currentTarget.isEmpty() ? QStringList{} : QStringList{currentTarget};
    parsed.text = trimmed;
    return parsed;
  }

  const QString commandLine = trimmed.mid(1).trimmed();
  if (commandLine.isEmpty()) {
    return {};
  }

  const SplitText split = splitFirstToken(commandLine);
  const QString command = split.first.toLower();
  const QString rest = split.rest;

  if (command == QStringLiteral("join") || command == QStringLiteral("j")) {
    const QStringList targets = normalizeChannelTargets(rest);
    if (targets.isEmpty()) {
      return errorCommand(QStringLiteral("Usage: /join #channel"));
    }
    return {UserCommandType::Join, command, targets};
  }

  if (command == QStringLiteral("part") || command == QStringLiteral("leave")) {
    SplitText part = splitFirstToken(rest);
    if (part.first.isEmpty()) {
      part.first = currentTarget;
    } else if (!isExplicitChannel(part.first) &&
               isExplicitChannel(currentTarget)) {
      part.rest = rest;
      part.first = currentTarget;
    }
    if (!isExplicitChannel(part.first)) {
      return errorCommand(QStringLiteral("Usage: /part #channel"));
    }
    return {UserCommandType::Part, command, QStringList{part.first}, part.rest};
  }

  if (command == QStringLiteral("cycle") || command == QStringLiteral("hop")) {
    QString target = currentTarget;
    QString reason = rest;
    const SplitText explicitTarget = splitFirstToken(rest);
    if (!explicitTarget.first.isEmpty() &&
        isExplicitChannel(explicitTarget.first)) {
      target = explicitTarget.first;
      reason = explicitTarget.rest;
    }
    if (!isExplicitChannel(target)) {
      return errorCommand(QStringLiteral("Usage: /cycle [#channel] [reason]"));
    }
    return {UserCommandType::Cycle, command, QStringList{target}, reason};
  }

  if (command == QStringLiteral("msg") || command == QStringLiteral("m") ||
      command == QStringLiteral("privmsg")) {
    const SplitText message = splitFirstToken(rest);
    if (message.first.isEmpty() || message.rest.isEmpty()) {
      return errorCommand(QStringLiteral("Usage: /msg nick message"));
    }
    return {UserCommandType::PrivateMessage, command,
            QStringList{message.first}, message.rest};
  }

  if (command == QStringLiteral("query") || command == QStringLiteral("q")) {
    const SplitText query = splitFirstToken(rest);
    if (query.first.isEmpty()) {
      return errorCommand(QStringLiteral("Usage: /query nick [message]"));
    }
    return {UserCommandType::Query, command, QStringList{query.first},
            query.rest};
  }

  if (command == QStringLiteral("notice")) {
    const SplitText notice = splitFirstToken(rest);
    if (notice.first.isEmpty() || notice.rest.isEmpty()) {
      return errorCommand(
          QStringLiteral("Usage: /notice nick-or-channel message"));
    }
    return {UserCommandType::Notice, command, QStringList{notice.first},
            notice.rest};
  }

  if (command == QStringLiteral("shrug") || command == QStringLiteral("tableflip") ||
      command == QStringLiteral("flip") || command == QStringLiteral("unflip") ||
      command == QStringLiteral("lenny") || command == QStringLiteral("disapprove")) {
    QString decoration;
    if (command == QStringLiteral("shrug")) {
      decoration = QStringLiteral("¯\\_(ツ)_/¯");
    } else if (command == QStringLiteral("unflip")) {
      decoration = QStringLiteral("┬─┬ ノ( ゜-゜ノ)");
    } else if (command == QStringLiteral("lenny")) {
      decoration = QStringLiteral("( ͡° ͜ʖ ͡°)");
    } else if (command == QStringLiteral("disapprove")) {
      decoration = QStringLiteral("ಠ_ಠ");
    } else {
      decoration = QStringLiteral("(╯°□°)╯︵ ┻━┻");
    }
    const QString text =
        rest.isEmpty() ? decoration : QStringLiteral("%1 %2").arg(rest, decoration);
    return {UserCommandType::Text, command, {}, text};
  }

  if (command == QStringLiteral("amsg")) {
    if (rest.isEmpty()) {
      return errorCommand(QStringLiteral("Usage: /amsg message"));
    }
    return {UserCommandType::BroadcastMessage, command, {}, rest};
  }

  if (command == QStringLiteral("ame")) {
    if (rest.isEmpty()) {
      return errorCommand(QStringLiteral("Usage: /ame action"));
    }
    return {UserCommandType::BroadcastAction, command, {}, rest};
  }

  if (command == QStringLiteral("onotice")) {
    if (!isExplicitChannel(currentTarget) || rest.isEmpty()) {
      return errorCommand(QStringLiteral("Usage: /onotice message"));
    }
    return {UserCommandType::OpNotice, command, QStringList{currentTarget},
            rest};
  }

  if (command == QStringLiteral("me") || command == QStringLiteral("action")) {
    if (currentTarget.isEmpty() || rest.isEmpty()) {
      return errorCommand(QStringLiteral("Usage: /me action"));
    }
    return {UserCommandType::Action, command, QStringList{currentTarget}, rest};
  }

  if (command == QStringLiteral("ctcp")) {
    const SplitText target = splitFirstToken(rest);
    const SplitText ctcp = splitFirstToken(target.rest);
    if (target.first.isEmpty() || ctcp.first.isEmpty()) {
      return errorCommand(QStringLiteral("Usage: /ctcp nick command [args]"));
    }
    UserCommand parsed;
    parsed.type = UserCommandType::Ctcp;
    parsed.command = command;
    parsed.targets = QStringList{target.first};
    parsed.rawLine = ctcp.first.toUpper();
    parsed.text = ctcp.rest;
    return parsed;
  }

  if (command == QStringLiteral("sound")) {
    const SplitText sound = splitFirstToken(rest);
    if (currentTarget.isEmpty() || sound.first.isEmpty()) {
      return errorCommand(QStringLiteral("Usage: /sound <file.wav> [text]"));
    }
    // targets=current buffer, rawLine=sound file, text=optional message.
    return {UserCommandType::Sound, command, QStringList{currentTarget},
            sound.rest, sound.first};
  }

  const QString serviceTarget = serviceTargetForCommand(command);
  if (!serviceTarget.isEmpty()) {
    if (rest.isEmpty()) {
      return errorCommand(QStringLiteral("Usage: /%1 command").arg(command));
    }
    if (command == QStringLiteral("identify") ||
        command == QStringLiteral("id")) {
      return {UserCommandType::ServiceMessage, command,
              QStringList{serviceTarget},
              QStringLiteral("IDENTIFY %1").arg(rest)};
    }
    if (command == QStringLiteral("ghost")) {
      return {UserCommandType::ServiceMessage, command,
              QStringList{serviceTarget}, QStringLiteral("GHOST %1").arg(rest)};
    }
    return {UserCommandType::ServiceMessage, command,
            QStringList{serviceTarget}, rest};
  }

  if (command == QStringLiteral("nick")) {
    const SplitText nick = splitFirstToken(rest);
    if (nick.first.isEmpty()) {
      return errorCommand(QStringLiteral("Usage: /nick newnick"));
    }
    return {UserCommandType::Nick, command, {}, nick.first};
  }

  if (command == QStringLiteral("whois")) {
    const SplitText whois = splitFirstToken(rest);
    if (whois.first.isEmpty()) {
      return errorCommand(QStringLiteral("Usage: /whois nick"));
    }
    return {UserCommandType::Whois, command, QStringList{whois.first}};
  }

  if (command == QStringLiteral("who")) {
    const SplitText who = splitFirstToken(rest);
    if (who.first.isEmpty()) {
      return errorCommand(QStringLiteral("Usage: /who nick-or-channel"));
    }
    return {UserCommandType::Who, command, QStringList{who.first}};
  }

  if (command == QStringLiteral("whowas")) {
    const SplitText whowas = splitFirstToken(rest);
    if (whowas.first.isEmpty()) {
      return errorCommand(QStringLiteral("Usage: /whowas nick"));
    }
    return {UserCommandType::Whowas, command, QStringList{whowas.first}};
  }

  if (command == QStringLiteral("names")) {
    SplitText names = splitFirstToken(rest);
    if (names.first.isEmpty()) {
      names.first = currentTarget;
    }
    const QString target = channelTarget(names.first);
    if (target.isEmpty()) {
      return errorCommand(QStringLiteral("Usage: /names [#channel]"));
    }
    return {UserCommandType::Names, command, QStringList{target}};
  }

  if (command == QStringLiteral("list")) {
    return {UserCommandType::ChannelList, command, {}, rest};
  }

  if (command == QStringLiteral("topic")) {
    QString target = currentTarget;
    QString topicText = rest;
    const SplitText topic = splitFirstToken(rest);
    if (!topic.first.isEmpty() && isExplicitChannel(topic.first)) {
      target = topic.first;
      topicText = topic.rest;
    }
    if (target.isEmpty() || !isExplicitChannel(target)) {
      return errorCommand(QStringLiteral("Usage: /topic [#channel] [topic]"));
    }
    return {UserCommandType::Topic, command, QStringList{target}, topicText};
  }

  if (command == QStringLiteral("mode")) {
    if (rest.isEmpty()) {
      return errorCommand(
          QStringLiteral("Usage: /mode [#channel] modes [args]"));
    }
    const SplitText mode = splitFirstToken(rest);
    const QString rawArgs =
        !isExplicitChannel(mode.first) && isExplicitChannel(currentTarget)
            ? QStringLiteral("%1 %2").arg(currentTarget, rest)
            : rest;
    UserCommand parsed;
    parsed.type = UserCommandType::Mode;
    parsed.command = command;
    parsed.rawLine = QStringLiteral("MODE %1").arg(rawArgs);
    return parsed;
  }

  if (command == QStringLiteral("op") || command == QStringLiteral("deop") ||
      command == QStringLiteral("voice") ||
      command == QStringLiteral("devoice") ||
      command == QStringLiteral("halfop") ||
      command == QStringLiteral("dehalfop")) {
    if (!isExplicitChannel(currentTarget) || rest.isEmpty()) {
      return errorCommand(
          QStringLiteral("Usage: /%1 nick [nick...]").arg(command));
    }

    const QStringList nicks = rest.split(
        QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    const bool removing = command.startsWith(QStringLiteral("de"));
    const QChar mode =
        command.endsWith(QStringLiteral("voice"))    ? QLatin1Char('v')
        : command.endsWith(QStringLiteral("halfop")) ? QLatin1Char('h')
                                                     : QLatin1Char('o');
    UserCommand parsed;
    parsed.type = UserCommandType::Mode;
    parsed.command = command;
    parsed.rawLine = QStringLiteral("MODE %1 %2 %3")
                         .arg(currentTarget,
                              repeatedModeFlag(removing ? QLatin1Char('-')
                                                        : QLatin1Char('+'),
                                               mode, nicks.size()),
                              nicks.join(QLatin1Char(' ')));
    return parsed;
  }

  if (command == QStringLiteral("invite")) {
    const SplitText invite = splitFirstToken(rest);
    if (invite.first.isEmpty()) {
      return errorCommand(QStringLiteral("Usage: /invite nick [#channel]"));
    }
    const SplitText channel = splitFirstToken(invite.rest);
    QString target = channel.first;
    if (target.isEmpty()) {
      target = currentTarget;
    }
    if (!isExplicitChannel(target)) {
      return errorCommand(QStringLiteral("Usage: /invite nick [#channel]"));
    }
    return {UserCommandType::Invite, command,
            QStringList{invite.first, target}};
  }

  if (command == QStringLiteral("kick")) {
    SplitText first = splitFirstToken(rest);
    if (first.first.isEmpty()) {
      return errorCommand(
          QStringLiteral("Usage: /kick [#channel] nick [reason]"));
    }

    QString channel = currentTarget;
    QString nick = first.first;
    QString reason = first.rest;
    if (isExplicitChannel(first.first)) {
      const SplitText kicked = splitFirstToken(first.rest);
      channel = first.first;
      nick = kicked.first;
      reason = kicked.rest;
    }
    if (!isExplicitChannel(channel) || nick.isEmpty()) {
      return errorCommand(
          QStringLiteral("Usage: /kick [#channel] nick [reason]"));
    }
    return {UserCommandType::Kick, command, QStringList{channel, nick}, reason};
  }

  if (command == QStringLiteral("ban") ||
      command == QStringLiteral("kickban") || command == QStringLiteral("kb")) {
    SplitText first = splitFirstToken(rest);
    if (first.first.isEmpty()) {
      return errorCommand(
          command == QStringLiteral("ban")
              ? QStringLiteral("Usage: /ban [#channel] nick-or-mask")
              : QStringLiteral("Usage: /kickban [#channel] nick [reason]"));
    }

    QString channel = currentTarget;
    QString nickOrMask = first.first;
    QString reason = first.rest;
    if (isExplicitChannel(first.first)) {
      const SplitText banned = splitFirstToken(first.rest);
      channel = first.first;
      nickOrMask = banned.first;
      reason = banned.rest;
    }
    if (!isExplicitChannel(channel) || nickOrMask.isEmpty()) {
      return errorCommand(
          command == QStringLiteral("ban")
              ? QStringLiteral("Usage: /ban [#channel] nick-or-mask")
              : QStringLiteral("Usage: /kickban [#channel] nick [reason]"));
    }
    return {command == QStringLiteral("ban") ? UserCommandType::Ban
                                             : UserCommandType::KickBan,
            command,
            QStringList{channel, nickOrMask, banMaskForNickOrMask(nickOrMask)},
            reason};
  }

  if (command == QStringLiteral("away") || command == QStringLiteral("back")) {
    if (command == QStringLiteral("back")) {
      return {UserCommandType::Away, command};
    }
    return {UserCommandType::Away, command, {}, rest};
  }

  if (command == QStringLiteral("alias")) {
    const SplitText alias = splitFirstToken(rest);
    return alias.first.isEmpty()
               ? UserCommand{UserCommandType::Alias, command}
               : UserCommand{UserCommandType::Alias, command,
                             QStringList{alias.first}, alias.rest};
  }

  if (command == QStringLiteral("unalias")) {
    const SplitText alias = splitFirstToken(rest);
    if (alias.first.isEmpty()) {
      return errorCommand(QStringLiteral("Usage: /unalias name"));
    }
    return {UserCommandType::Unalias, command, QStringList{alias.first}};
  }

  if (command == QStringLiteral("ignore")) {
    return {UserCommandType::Ignore, command, {}, rest};
  }

  if (command == QStringLiteral("unignore")) {
    if (rest.isEmpty()) {
      return errorCommand(QStringLiteral("Usage: /unignore nick-or-mask"));
    }
    return {UserCommandType::Unignore, command, {}, rest};
  }

  if (command == QStringLiteral("mute")) {
    QString target = rest.trimmed();
    if (target.isEmpty()) {
      target = currentTarget;
    } else {
      target = channelTarget(target);
    }
    if (!isExplicitChannel(target)) {
      return errorCommand(QStringLiteral("Usage: /mute [#channel]"));
    }
    return {UserCommandType::Mute, command, QStringList{target}};
  }

  if (command == QStringLiteral("unmute") || command == QStringLiteral("umite")) {
    QString target = rest.trimmed();
    if (target.isEmpty()) {
      target = currentTarget;
    } else {
      target = channelTarget(target);
    }
    if (!isExplicitChannel(target)) {
      return errorCommand(QStringLiteral("Usage: /unmute [#channel]"));
    }
    return {UserCommandType::Unmute, command, QStringList{target}};
  }

  if (command == QStringLiteral("notify")) {
    return {UserCommandType::Notify, command, {}, rest};
  }

  if (command == QStringLiteral("unnotify")) {
    if (rest.isEmpty()) {
      return errorCommand(QStringLiteral("Usage: /unnotify nick"));
    }
    return {UserCommandType::Unnotify, command, {}, rest};
  }

  if (command == QStringLiteral("lag")) {
    return {UserCommandType::Lag, command};
  }

  if (command == QStringLiteral("uptime")) {
    return {UserCommandType::Uptime, command};
  }

  if (command == QStringLiteral("netinfo")) {
    return {UserCommandType::NetInfo, command};
  }

  if (command == QStringLiteral("sysinfo") || command == QStringLiteral("sys")) {
    return {UserCommandType::SysInfo, command, {}, rest};
  }

  if (command == QStringLiteral("scripts") || command == QStringLiteral("load") ||
      command == QStringLiteral("unload") ||
      command == QStringLiteral("reload")) {
    return {UserCommandType::Scripts, command, {}, rest};
  }

  if (command == QStringLiteral("help") || command == QStringLiteral("?")) {
    return {UserCommandType::Help, command, {}, rest};
  }

  if (command == QStringLiteral("clear")) {
    return {UserCommandType::Clear, command};
  }

  if (command == QStringLiteral("clearall")) {
    return {UserCommandType::ClearAll, command};
  }

  if (command == QStringLiteral("close")) {
    return {UserCommandType::Close, command};
  }

  if (command == QStringLiteral("disconnect")) {
    return {UserCommandType::Disconnect, command};
  }

  if (command == QStringLiteral("reconnect")) {
    return {UserCommandType::Reconnect, command};
  }

  if (command == QStringLiteral("connect") ||
      command == QStringLiteral("server")) {
    const SplitText target = splitFirstToken(rest);
    if (target.first.isEmpty()) {
      return errorCommand(
          command == QStringLiteral("connect")
              ? QStringLiteral("Usage: /connect network-or-server [port] "
                               "[server-password]")
              : QStringLiteral("Usage: /server server[:port] [server-password]"));
    }
    return {UserCommandType::Connect, command, QStringList{target.first},
            target.rest};
  }

  if (command == QStringLiteral("raw") || command == QStringLiteral("quote")) {
    if (rest.isEmpty()) {
      return errorCommand(QStringLiteral("Usage: /raw IRC COMMAND"));
    }
    UserCommand parsed;
    parsed.type = UserCommandType::Raw;
    parsed.command = command;
    parsed.rawLine = rest;
    return parsed;
  }

  if (command == QStringLiteral("wallops")) {
    if (rest.isEmpty()) {
      return errorCommand(QStringLiteral("Usage: /wallops message"));
    }
    UserCommand parsed;
    parsed.type = UserCommandType::Raw;
    parsed.command = command;
    parsed.rawLine = QStringLiteral("WALLOPS :%1").arg(rest);
    return parsed;
  }

  if (command == QStringLiteral("oper")) {
    if (rest.isEmpty()) {
      return errorCommand(QStringLiteral("Usage: /oper user password"));
    }
    UserCommand parsed;
    parsed.type = UserCommandType::Raw;
    parsed.command = command;
    parsed.rawLine = QStringLiteral("OPER %1").arg(rest);
    return parsed;
  }

  if (command == QStringLiteral("kill")) {
    const SplitText kill = splitFirstToken(rest);
    if (kill.first.isEmpty()) {
      return errorCommand(QStringLiteral("Usage: /kill nick [reason]"));
    }
    UserCommand parsed;
    parsed.type = UserCommandType::Raw;
    parsed.command = command;
    parsed.rawLine =
        QStringLiteral("KILL %1 :%2")
            .arg(kill.first,
                 kill.rest.isEmpty() ? QStringLiteral("killed") : kill.rest);
    return parsed;
  }

  if (command == QStringLiteral("quit")) {
    return {UserCommandType::Quit, command, {}, rest};
  }

  UserCommand parsed;
  parsed.type = UserCommandType::Raw;
  parsed.command = command;
  parsed.rawLine = commandLine;
  return parsed;
}

} // namespace maxchat::irc
