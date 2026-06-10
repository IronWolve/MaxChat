#pragma once

#include <QString>
#include <QStringList>

namespace maxchat::irc {

enum class UserCommandType {
  Empty,
  Text,
  Raw,
  Join,
  Part,
  Cycle,
  PrivateMessage,
  Query,
  Notice,
  Action,
  BroadcastMessage,
  BroadcastAction,
  OpNotice,
  Ctcp,
  ServiceMessage,
  Nick,
  Whois,
  Who,
  Whowas,
  Names,
  ChannelList,
  Topic,
  Mode,
  Invite,
  Kick,
  Ban,
  KickBan,
  Away,
  Alias,
  Unalias,
  Ignore,
  Unignore,
  Mute,
  Unmute,
  Notify,
  Unnotify,
  Lag,
  Uptime,
  NetInfo,
  SysInfo,
  Scripts,
  Help,
  Clear,
  ClearAll,
  Close,
  Disconnect,
  Reconnect,
  Connect,
  Quit,
  Error,
};

struct UserCommand {
  UserCommandType type = UserCommandType::Empty;
  QString command;
  QStringList targets;
  QString text;
  QString rawLine;
  QString errorText;
};

[[nodiscard]] QStringList supportedCommandNames();
[[nodiscard]] QStringList normalizeChannelTargets(const QString &text);
[[nodiscard]] UserCommand parseUserCommand(const QString &input,
                                           const QString &currentTarget = {});

} // namespace maxchat::irc
