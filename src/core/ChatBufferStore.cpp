#include "core/ChatBufferStore.h"

#include <QSet>

#include <algorithm>

namespace maxchat::core {
namespace {

QString serverTarget() { return QStringLiteral("Server"); }

QString nickWithoutStatusPrefix(QString nick) {
  static const QString prefixes = QStringLiteral("~&@%+");
  nick = nick.trimmed();
  while (!nick.isEmpty() && prefixes.contains(nick.front())) {
    nick.remove(0, 1);
  }
  return nick;
}

QString nickMatchKey(const QString &nick) {
  return nickWithoutStatusPrefix(nick).toCaseFolded();
}

bool containsNick(const QStringList &members, const QString &nick) {
  const QString needle = nickMatchKey(nick);
  if (needle.isEmpty()) {
    return false;
  }
  return std::any_of(members.cbegin(), members.cend(),
                     [&needle](const QString &member) {
                       return nickMatchKey(member) == needle;
                     });
}

QStringList sortedUniqueMembers(const QStringList &members) {
  QStringList out;
  out.reserve(members.size());
  // O(n) dedup by match-key (was O(n^2) via a linear containsNick per member —
  // it bit hard on a large channel's NAMES burst).
  QSet<QString> seen;
  seen.reserve(members.size());
  for (const QString &member : members) {
    const QString trimmed = member.trimmed();
    if (trimmed.isEmpty()) {
      continue;
    }
    const QString key = nickMatchKey(trimmed);
    if (!key.isEmpty() && !seen.contains(key)) {
      seen.insert(key);
      out.append(trimmed);
    }
  }
  out.sort(Qt::CaseInsensitive);
  return out;
}

} // namespace

bool ChatBufferId::isValid() const {
  return !network.trimmed().isEmpty() && !target.trimmed().isEmpty();
}

ChatBufferStore::ChatBufferStore(const int maxLinesPerBuffer) {
  setMaxLinesPerBuffer(maxLinesPerBuffer);
}

int ChatBufferStore::maxLinesPerBuffer() const { return maxLinesPerBuffer_; }

void ChatBufferStore::setMaxLinesPerBuffer(const int maxLinesPerBuffer) {
  maxLinesPerBuffer_ = std::max(1, maxLinesPerBuffer);
  for (Buffer &buffer : buffersByKey_) {
    // Drop the oldest excess in one shot: repeated removeFirst() shifts the
    // whole QList each call (O(n^2) when shrinking a big buffer).
    const qsizetype excess = buffer.snapshot.lines.size() - maxLinesPerBuffer_;
    if (excess > 0) {
      buffer.snapshot.lines.remove(0, excess);
    }
  }
}

ChatBufferId ChatBufferStore::ensureServerBuffer(const QString &network) {
  return ensureBuffer(network, serverTarget(), ChatBufferKind::Server);
}

ChatBufferId ChatBufferStore::ensureChannelBuffer(const QString &network,
                                                  const QString &channel) {
  return ensureBuffer(network, channel, ChatBufferKind::Channel);
}

ChatBufferId ChatBufferStore::ensureQueryBuffer(const QString &network,
                                                const QString &nick) {
  return ensureBuffer(network, nick, ChatBufferKind::Query);
}

bool ChatBufferStore::contains(const ChatBufferId &id) const {
  return buffersByKey_.contains(keyFor(id));
}

QList<ChatBufferId> ChatBufferStore::buffers() const {
  QList<ChatBufferId> ids;
  ids.reserve(order_.size());
  for (const QString &key : order_) {
    const auto it = buffersByKey_.constFind(key);
    if (it != buffersByKey_.cend()) {
      ids.append(it->snapshot.id);
    }
  }
  return ids;
}

QList<ChatBufferId>
ChatBufferStore::buffersForNetwork(const QString &network) const {
  QList<ChatBufferId> ids;
  const QString normalized = normalizedNetwork(network);
  for (const QString &key : order_) {
    const auto it = buffersByKey_.constFind(key);
    if (it != buffersByKey_.cend() &&
        normalizedNetwork(it->snapshot.id.network) == normalized) {
      ids.append(it->snapshot.id);
    }
  }
  return ids;
}

ChatBufferId ChatBufferStore::activeBuffer() const {
  const auto it = buffersByKey_.constFind(activeKey_);
  return it == buffersByKey_.cend() ? ChatBufferId{} : it->snapshot.id;
}

ChatBufferSnapshot ChatBufferStore::snapshot(const ChatBufferId &id) const {
  const QString key = keyFor(id);
  const auto it = buffersByKey_.constFind(key);
  if (it == buffersByKey_.cend()) {
    return {};
  }
  ChatBufferSnapshot snapshot = it->snapshot;
  snapshot.active = key == activeKey_;
  return snapshot;
}

bool ChatBufferStore::setActiveBuffer(const ChatBufferId &id) {
  const QString key = keyFor(id);
  auto it = buffersByKey_.find(key);
  if (it == buffersByKey_.end()) {
    return false;
  }

  activeKey_ = key;
  it->snapshot.unreadCount = 0;
  it->snapshot.highlightCount = 0;
  return true;
}

bool ChatBufferStore::markRead(const ChatBufferId &id) {
  auto it = buffersByKey_.find(keyFor(id));
  if (it == buffersByKey_.end()) {
    return false;
  }

  it->snapshot.unreadCount = 0;
  it->snapshot.highlightCount = 0;
  return true;
}

int ChatBufferStore::markAllReadForNetwork(const QString &network) {
  const QString normalized = normalizedNetwork(network);
  if (normalized.isEmpty()) {
    return 0;
  }

  int changed = 0;
  for (Buffer &buffer : buffersByKey_) {
    ChatBufferSnapshot &snapshot = buffer.snapshot;
    if (normalizedNetwork(snapshot.id.network) != normalized) {
      continue;
    }
    if (snapshot.unreadCount > 0 || snapshot.highlightCount > 0) {
      ++changed;
    }
    snapshot.unreadCount = 0;
    snapshot.highlightCount = 0;
  }
  return changed;
}

bool ChatBufferStore::clearLines(const ChatBufferId &id) {
  const auto it = buffersByKey_.find(keyFor(id));
  if (it == buffersByKey_.end()) {
    return false;
  }

  ChatBufferSnapshot &snapshot = it->snapshot;
  snapshot.lines.clear();
  snapshot.unreadCount = 0;
  snapshot.highlightCount = 0;
  return true;
}

bool ChatBufferStore::removeBuffer(const ChatBufferId &id) {
  const QString key = keyFor(id);
  if (!buffersByKey_.contains(key)) {
    return false;
  }

  buffersByKey_.remove(key);
  order_.removeAll(key);
  if (activeKey_ == key) {
    activeKey_ = order_.isEmpty() ? QString() : order_.first();
  }
  return true;
}

void ChatBufferStore::clear() {
  buffersByKey_.clear();
  order_.clear();
  activeKey_.clear();
}

bool ChatBufferStore::appendLine(const ChatBufferId &id, ChatBufferLine line) {
  const QString key = keyFor(id);
  auto it = buffersByKey_.find(key);
  if (it == buffersByKey_.end()) {
    return false;
  }

  if (!line.timestamp.isValid()) {
    line.timestamp = QDateTime::currentDateTimeUtc();
  }

  ChatBufferSnapshot &snapshot = it->snapshot;
  snapshot.lines.append(line);
  if (const qsizetype excess = snapshot.lines.size() - maxLinesPerBuffer_; excess > 0) {
    snapshot.lines.remove(0, excess);
  }

  const bool inactive = activeKey_ != key;
  if (inactive && !line.localEcho) {
    ++snapshot.unreadCount;
    if (line.highlight) {
      ++snapshot.highlightCount;
    }
  }
  return true;
}

bool ChatBufferStore::setTopic(const ChatBufferId &id, const QString &topic) {
  auto it = buffersByKey_.find(keyFor(id));
  if (it == buffersByKey_.end()) {
    return false;
  }
  it->snapshot.topic = topic;
  return true;
}

bool ChatBufferStore::setJoined(const ChatBufferId &id, const bool joined) {
  auto it = buffersByKey_.find(keyFor(id));
  if (it == buffersByKey_.end()) {
    return false;
  }
  it->snapshot.joined = joined;
  return true;
}

bool ChatBufferStore::setMembers(const ChatBufferId &id,
                                 const QStringList &members) {
  auto it = buffersByKey_.find(keyFor(id));
  if (it == buffersByKey_.end()) {
    return false;
  }
  it->snapshot.members = sortedUniqueMembers(members);
  return true;
}

bool ChatBufferStore::addMember(const ChatBufferId &id, const QString &nick) {
  auto it = buffersByKey_.find(keyFor(id));
  const QString trimmed = nick.trimmed();
  if (it == buffersByKey_.end() || trimmed.isEmpty()) {
    return false;
  }
  if (!containsNick(it->snapshot.members, trimmed)) {
    it->snapshot.members.append(trimmed);
    it->snapshot.members.sort(Qt::CaseInsensitive);
  }
  return true;
}

bool ChatBufferStore::removeMember(const ChatBufferId &id,
                                   const QString &nick) {
  auto it = buffersByKey_.find(keyFor(id));
  const QString needle = nickMatchKey(nick);
  if (it == buffersByKey_.end() || needle.isEmpty()) {
    return false;
  }

  bool removed = false;
  for (int index = it->snapshot.members.size() - 1; index >= 0; --index) {
    if (nickMatchKey(it->snapshot.members.at(index)) == needle) {
      it->snapshot.members.removeAt(index);
      removed = true;
    }
  }
  return removed;
}

bool ChatBufferStore::renameMember(const ChatBufferId &id,
                                   const QString &oldNick,
                                   const QString &newNick) {
  // A NICK change carries no channel status prefix, so capture the old entry's
  // prefix (~&@%+) and re-apply it — otherwise an op/voiced user silently loses
  // their mode on rename.
  static const QString prefixes = QStringLiteral("~&@%+");
  QString prefix;
  if (auto it = buffersByKey_.find(keyFor(id)); it != buffersByKey_.end()) {
    const QString needle = nickMatchKey(oldNick);
    for (const QString &member : it->snapshot.members) {
      if (nickMatchKey(member) == needle) {
        int i = 0;
        while (i < member.size() && prefixes.contains(member.at(i))) {
          ++i;
        }
        prefix = member.left(i);
        break;
      }
    }
  }
  if (!removeMember(id, oldNick)) {
    return false;
  }
  return addMember(id, prefix + newNick.trimmed());
}

int ChatBufferStore::totalUnreadCount() const {
  int count = 0;
  for (const Buffer &buffer : buffersByKey_) {
    count += buffer.snapshot.unreadCount;
  }
  return count;
}

int ChatBufferStore::totalHighlightCount() const {
  int count = 0;
  for (const Buffer &buffer : buffersByKey_) {
    count += buffer.snapshot.highlightCount;
  }
  return count;
}

ChatBufferId ChatBufferStore::ensureBuffer(const QString &network,
                                           const QString &target,
                                           const ChatBufferKind kind) {
  const QString cleanNetwork = network.trimmed();
  const QString cleanTarget =
      kind == ChatBufferKind::Server ? serverTarget() : target.trimmed();
  if (cleanNetwork.isEmpty() || cleanTarget.isEmpty()) {
    return {};
  }

  const QString key = keyFor(cleanNetwork, cleanTarget, kind);
  auto it = buffersByKey_.find(key);
  if (it != buffersByKey_.end()) {
    return it->snapshot.id;
  }

  Buffer buffer;
  buffer.snapshot.id = {cleanNetwork, cleanTarget, kind};
  buffer.snapshot.joined = kind == ChatBufferKind::Server;
  buffersByKey_.insert(key, buffer);
  order_.append(key);
  if (activeKey_.isEmpty()) {
    activeKey_ = key;
  }
  return buffer.snapshot.id;
}

QString ChatBufferStore::keyFor(const ChatBufferId &id) const {
  return keyFor(id.network, id.target, id.kind);
}

QString ChatBufferStore::keyFor(const QString &network, const QString &target,
                                const ChatBufferKind kind) const {
  return QStringLiteral("%1:%2:%3")
      .arg(static_cast<int>(kind))
      .arg(normalizedNetwork(network), normalizedTarget(target, kind));
}

QString ChatBufferStore::normalizedNetwork(const QString &network) {
  return network.trimmed().toCaseFolded();
}

QString ChatBufferStore::normalizedTarget(const QString &target,
                                          const ChatBufferKind kind) {
  if (kind == ChatBufferKind::Server) {
    return QStringLiteral("server");
  }
  return target.trimmed().toCaseFolded();
}

} // namespace maxchat::core
