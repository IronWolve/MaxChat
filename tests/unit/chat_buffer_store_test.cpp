#include "core/ChatBufferStore.h"

#include <QtTest/QtTest>

using maxchat::core::ChatBufferKind;
using maxchat::core::ChatBufferLine;
using maxchat::core::ChatBufferStore;

class ChatBufferStoreTest final : public QObject {
  Q_OBJECT

private slots:
  void preservesDisplayNamesWhileMatchingCaseInsensitively() {
    ChatBufferStore store;

    const auto first = store.ensureChannelBuffer(QStringLiteral("Libera.Chat"),
                                                 QStringLiteral("#MaxChat"));
    const auto second = store.ensureChannelBuffer(QStringLiteral("libera.chat"),
                                                  QStringLiteral("#maxchat"));

    QCOMPARE(first.network, QStringLiteral("Libera.Chat"));
    QCOMPARE(first.target, QStringLiteral("#MaxChat"));
    QCOMPARE(second.network, first.network);
    QCOMPARE(second.target, first.target);
    QCOMPARE(store.buffers().size(), 1);
    QCOMPARE(store.buffersForNetwork(QStringLiteral("LIBERA.CHAT")).size(), 1);
  }

  void tracksActiveUnreadAndHighlights() {
    ChatBufferStore store;
    const auto server = store.ensureServerBuffer(QStringLiteral("Libera.Chat"));
    const auto channel = store.ensureChannelBuffer(
        QStringLiteral("Libera.Chat"), QStringLiteral("#maxchat"));

    QVERIFY(store.setActiveBuffer(server));
    ChatBufferLine normal;
    normal.plainText = QStringLiteral("<nick> hello");
    QVERIFY(store.appendLine(channel, normal));

    ChatBufferLine highlight;
    highlight.plainText = QStringLiteral("<nick> MaxChat: ping");
    highlight.highlight = true;
    QVERIFY(store.appendLine(channel, highlight));

    auto snapshot = store.snapshot(channel);
    QCOMPARE(snapshot.unreadCount, 2);
    QCOMPARE(snapshot.highlightCount, 1);
    QCOMPARE(store.totalUnreadCount(), 2);
    QCOMPARE(store.totalHighlightCount(), 1);

    QVERIFY(store.setActiveBuffer(channel));
    snapshot = store.snapshot(channel);
    QVERIFY(snapshot.active);
    QCOMPARE(snapshot.unreadCount, 0);
    QCOMPARE(snapshot.highlightCount, 0);
  }

  void marksBuffersReadWithoutChangingActiveBuffer() {
    ChatBufferStore store;
    const auto server = store.ensureServerBuffer(QStringLiteral("Libera.Chat"));
    const auto one = store.ensureChannelBuffer(QStringLiteral("Libera.Chat"),
                                               QStringLiteral("#one"));
    const auto two = store.ensureChannelBuffer(QStringLiteral("Libera.Chat"),
                                               QStringLiteral("#two"));
    const auto other = store.ensureChannelBuffer(QStringLiteral("EFnet"),
                                                 QStringLiteral("#other"));

    QVERIFY(store.setActiveBuffer(server));
    ChatBufferLine normal;
    normal.plainText = QStringLiteral("<nick> hello");
    QVERIFY(store.appendLine(one, normal));

    ChatBufferLine highlight;
    highlight.plainText = QStringLiteral("<nick> MaxChat: ping");
    highlight.highlight = true;
    QVERIFY(store.appendLine(two, highlight));
    QVERIFY(store.appendLine(other, highlight));

    QCOMPARE(store.snapshot(one).unreadCount, 1);
    QCOMPARE(store.snapshot(two).highlightCount, 1);
    QCOMPARE(store.snapshot(other).highlightCount, 1);

    QCOMPARE(store.markAllReadForNetwork(QStringLiteral("libera.chat")), 2);
    QCOMPARE(store.activeBuffer().target, server.target);
    QCOMPARE(store.snapshot(one).unreadCount, 0);
    QCOMPARE(store.snapshot(two).highlightCount, 0);
    QCOMPARE(store.snapshot(other).highlightCount, 1);
  }

  void clearsLinesWithoutClearingMetadata() {
    ChatBufferStore store;
    const auto server = store.ensureServerBuffer(QStringLiteral("Libera.Chat"));
    const auto channel = store.ensureChannelBuffer(
        QStringLiteral("Libera.Chat"), QStringLiteral("#chat"));
    QVERIFY(store.setActiveBuffer(server));
    QVERIFY(store.setTopic(channel, QStringLiteral("Chat topic")));
    QVERIFY(store.setJoined(channel, true));
    QVERIFY(store.setMembers(
        channel, {QStringLiteral("@alice"), QStringLiteral("+bob")}));

    ChatBufferLine line;
    line.plainText = QStringLiteral("<alice> later");
    line.highlight = true;
    QVERIFY(store.appendLine(channel, line));

    QVERIFY(store.clearLines(channel));
    const auto snapshot = store.snapshot(channel);
    QCOMPARE(snapshot.lines.size(), 0);
    QCOMPARE(snapshot.unreadCount, 0);
    QCOMPARE(snapshot.highlightCount, 0);
    QCOMPARE(snapshot.topic, QStringLiteral("Chat topic"));
    QCOMPARE(snapshot.joined, true);
    QStringList expectedMembers{QStringLiteral("@alice"),
                                QStringLiteral("+bob")};
    expectedMembers.sort(Qt::CaseInsensitive);
    QCOMPARE(snapshot.members, expectedMembers);
  }

  void localEchoDoesNotIncrementUnread() {
    ChatBufferStore store;
    const auto server = store.ensureServerBuffer(QStringLiteral("EFnet"));
    const auto query = store.ensureQueryBuffer(QStringLiteral("EFnet"),
                                               QStringLiteral("Friend"));
    QVERIFY(store.setActiveBuffer(server));

    ChatBufferLine line;
    line.plainText = QStringLiteral("<me> hi");
    line.localEcho = true;
    QVERIFY(store.appendLine(query, line));

    QCOMPARE(store.snapshot(query).unreadCount, 0);
  }

  void boundsLineHistory() {
    ChatBufferStore store(2);
    const auto channel = store.ensureChannelBuffer(QStringLiteral("Undernet"),
                                                   QStringLiteral("#chat"));

    for (int index = 1; index <= 3; ++index) {
      ChatBufferLine line;
      line.plainText = QStringLiteral("line %1").arg(index);
      QVERIFY(store.appendLine(channel, line));
    }

    const auto snapshot = store.snapshot(channel);
    QCOMPARE(snapshot.lines.size(), 2);
    QCOMPARE(snapshot.lines.first().plainText, QStringLiteral("line 2"));
    QCOMPARE(snapshot.lines.last().plainText, QStringLiteral("line 3"));
  }

  void managesChannelMembers() {
    ChatBufferStore store;
    const auto channel = store.ensureChannelBuffer(
        QStringLiteral("Libera.Chat"), QStringLiteral("#maxchat"));

    QVERIFY(store.setMembers(channel,
                             {QStringLiteral("zNick"), QStringLiteral("alice"),
                              QStringLiteral("Alice")}));
    QCOMPARE(store.snapshot(channel).members,
             QStringList({QStringLiteral("alice"), QStringLiteral("zNick")}));

    QVERIFY(store.addMember(channel, QStringLiteral("Bob")));
    QVERIFY(store.renameMember(channel, QStringLiteral("bob"),
                               QStringLiteral("Carol")));
    QVERIFY(store.removeMember(channel, QStringLiteral("ALICE")));
    QCOMPARE(store.snapshot(channel).members,
             QStringList({QStringLiteral("Carol"), QStringLiteral("zNick")}));
  }

  void matchesStatusPrefixedMembersByNick() {
    ChatBufferStore store;
    const auto channel = store.ensureChannelBuffer(
        QStringLiteral("Libera.Chat"), QStringLiteral("#maxchat"));

    QVERIFY(store.setMembers(
        channel, {QStringLiteral("@Alice"), QStringLiteral("+Bob")}));
    QVERIFY(store.addMember(channel, QStringLiteral("alice")));
    QCOMPARE(store.snapshot(channel).members.size(), 2);

    QVERIFY(store.renameMember(channel, QStringLiteral("alice"),
                               QStringLiteral("Carol")));
    auto snapshot = store.snapshot(channel);
    QVERIFY(!snapshot.members.contains(QStringLiteral("@Alice")));
    QVERIFY(snapshot.members.contains(QStringLiteral("Carol")));

    QVERIFY(store.removeMember(channel, QStringLiteral("bob")));
    snapshot = store.snapshot(channel);
    QVERIFY(!snapshot.members.contains(QStringLiteral("+Bob")));
    QCOMPARE(snapshot.members, QStringList({QStringLiteral("Carol")}));
  }

  void removeActiveBufferSelectsNextAvailableBuffer() {
    ChatBufferStore store;
    const auto server = store.ensureServerBuffer(QStringLiteral("Network"));
    const auto query = store.ensureQueryBuffer(QStringLiteral("Network"),
                                               QStringLiteral("pm"));

    QVERIFY(store.setActiveBuffer(server));
    QVERIFY(store.removeBuffer(server));

    QCOMPARE(store.activeBuffer().target, query.target);
    QVERIFY(!store.contains(server));
  }

  void rejectsInvalidBuffersAndMissingTargets() {
    ChatBufferStore store;

    QVERIFY(!store.ensureServerBuffer(QString()).isValid());
    QVERIFY(
        !store.ensureChannelBuffer(QStringLiteral("Net"), QString()).isValid());

    ChatBufferLine line;
    line.plainText = QStringLiteral("lost");
    QVERIFY(!store.appendLine({QStringLiteral("Net"), QStringLiteral("#none"),
                               ChatBufferKind::Channel},
                              line));
  }
};

QTEST_APPLESS_MAIN(ChatBufferStoreTest)

#include "chat_buffer_store_test.moc"
