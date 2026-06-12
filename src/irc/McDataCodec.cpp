#include "irc/McDataCodec.h"

namespace maxchat::irc {

namespace {

struct SplitToken {
  QString first;
  QString rest;
};

SplitToken splitFirstToken(const QString &text) {
  const QString trimmed = text.trimmed();
  const int space = trimmed.indexOf(QLatin1Char(' '));
  if (space < 0) {
    return {.first = trimmed, .rest = {}};
  }
  return {.first = trimmed.left(space),
          .rest = trimmed.mid(space + 1).trimmed()};
}

bool validToken(const QString &token) {
  if (token.isEmpty()) {
    return false;
  }
  for (const QChar ch : token) {
    if (ch.isLetterOrNumber() || ch == QLatin1Char('_') ||
        ch == QLatin1Char('-') || ch == QLatin1Char('.')) {
      continue;
    }
    return false;
  }
  return true;
}

} // namespace

std::optional<McDataMessage> parseMcDataCtcp(const QString &command,
                                            const QString &args) {
  if (command.compare(QStringLiteral("MC"), Qt::CaseInsensitive) != 0) {
    return std::nullopt;
  }

  const SplitToken kind = splitFirstToken(args);
  if (kind.first.compare(QStringLiteral("DATA"), Qt::CaseInsensitive) != 0) {
    return std::nullopt;
  }

  const SplitToken service = splitFirstToken(kind.rest);
  const SplitToken verb = splitFirstToken(service.rest);
  if (!validToken(service.first) || !validToken(verb.first)) {
    return std::nullopt;
  }

  return McDataMessage{.service = service.first.toLower(),
                       .verb = verb.first.toUpper(), .payload = verb.rest};
}

QString mcDataCtcpBody(const QString &service, const QString &verb,
                       const QString &payload) {
  const QString body =
      QStringLiteral("MC DATA %1 %2").arg(service.toLower(), verb.toUpper());
  return payload.isEmpty() ? body : QStringLiteral("%1 %2").arg(body, payload);
}

} // namespace maxchat::irc
