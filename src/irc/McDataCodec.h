#pragma once

#include <QString>

#include <optional>

namespace maxchat::irc {

struct McDataMessage {
  QString service;
  QString verb;
  QString payload;
};

[[nodiscard]] std::optional<McDataMessage>
parseMcDataCtcp(const QString &command, const QString &args);

[[nodiscard]] QString mcDataCtcpBody(const QString &service, const QString &verb,
                                     const QString &payload = {});

} // namespace maxchat::irc
