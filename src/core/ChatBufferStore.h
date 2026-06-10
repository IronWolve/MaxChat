#pragma once

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace maxchat::core {

enum class ChatBufferKind {
  Server,
  Channel,
  Query,
};

struct ChatBufferId {
  QString network;
  QString target;
  ChatBufferKind kind = ChatBufferKind::Server;

  [[nodiscard]] bool isValid() const;
};

struct ChatBufferLine {
  QDateTime timestamp;
  QString plainText;
  QString htmlText;
  QString sourceText;
  bool localEcho = false;
  bool highlight = false;
  bool systemLine = false;
};

struct ChatBufferSnapshot {
  ChatBufferId id;
  QString topic;
  QStringList members;
  QList<ChatBufferLine> lines;
  int unreadCount = 0;
  int highlightCount = 0;
  bool joined = false;
  bool active = false;
};

class ChatBufferStore final {
public:
  explicit ChatBufferStore(int maxLinesPerBuffer = 1000);

  [[nodiscard]] int maxLinesPerBuffer() const;
  void setMaxLinesPerBuffer(int maxLinesPerBuffer);

  [[nodiscard]] ChatBufferId ensureServerBuffer(const QString &network);
  [[nodiscard]] ChatBufferId ensureChannelBuffer(const QString &network,
                                                 const QString &channel);
  [[nodiscard]] ChatBufferId ensureQueryBuffer(const QString &network,
                                               const QString &nick);
  [[nodiscard]] bool contains(const ChatBufferId &id) const;

  [[nodiscard]] QList<ChatBufferId> buffers() const;
  [[nodiscard]] QList<ChatBufferId>
  buffersForNetwork(const QString &network) const;
  [[nodiscard]] ChatBufferId activeBuffer() const;
  [[nodiscard]] ChatBufferSnapshot snapshot(const ChatBufferId &id) const;

  [[nodiscard]] bool setActiveBuffer(const ChatBufferId &id);
  [[nodiscard]] bool markRead(const ChatBufferId &id);
  [[nodiscard]] int markAllReadForNetwork(const QString &network);
  [[nodiscard]] bool clearLines(const ChatBufferId &id);
  [[nodiscard]] bool removeBuffer(const ChatBufferId &id);
  void clear();

  [[nodiscard]] bool appendLine(const ChatBufferId &id, ChatBufferLine line);
  [[nodiscard]] bool setTopic(const ChatBufferId &id, const QString &topic);
  [[nodiscard]] bool setJoined(const ChatBufferId &id, bool joined);
  [[nodiscard]] bool setMembers(const ChatBufferId &id,
                                const QStringList &members);
  [[nodiscard]] bool addMember(const ChatBufferId &id, const QString &nick);
  [[nodiscard]] bool removeMember(const ChatBufferId &id, const QString &nick);
  [[nodiscard]] bool renameMember(const ChatBufferId &id,
                                  const QString &oldNick,
                                  const QString &newNick);

  [[nodiscard]] int totalUnreadCount() const;
  [[nodiscard]] int totalHighlightCount() const;

private:
  struct Buffer {
    ChatBufferSnapshot snapshot;
  };

  [[nodiscard]] ChatBufferId ensureBuffer(const QString &network,
                                          const QString &target,
                                          ChatBufferKind kind);
  [[nodiscard]] QString keyFor(const ChatBufferId &id) const;
  [[nodiscard]] QString keyFor(const QString &network, const QString &target,
                               ChatBufferKind kind) const;
  [[nodiscard]] static QString normalizedNetwork(const QString &network);
  [[nodiscard]] static QString normalizedTarget(const QString &target,
                                                ChatBufferKind kind);

  QHash<QString, Buffer> buffersByKey_;
  QStringList order_;
  QString activeKey_;
  int maxLinesPerBuffer_ = 1000;
};

} // namespace maxchat::core
