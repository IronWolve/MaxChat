#include "irc/IrcMessage.h"

#include <QtTest/QtTest>

using maxchat::irc::parseMessage;

class IrcMessageTest final : public QObject {
    Q_OBJECT

  private slots:
    void simplePrivmsg() {
        const auto msg = parseMessage(QStringLiteral(":nick!user@host PRIVMSG #chan :hello world"));
        QCOMPARE(msg.command, QStringLiteral("PRIVMSG"));
        QCOMPARE(msg.nick(), QStringLiteral("nick"));
        QCOMPARE(msg.prefix, QStringLiteral("nick!user@host"));
        QCOMPARE(msg.params.at(0), QStringLiteral("#chan"));
        QCOMPARE(msg.trailing(), QStringLiteral("hello world"));
    }

    void pingHasNoPrefix() {
        const auto msg = parseMessage(QStringLiteral("PING :server.example"));
        QCOMPARE(msg.command, QStringLiteral("PING"));
        QCOMPARE(msg.prefix, QString());
        QCOMPARE(msg.trailing(), QStringLiteral("server.example"));
    }

    void numericWelcome() {
        const auto msg = parseMessage(QStringLiteral(":srv 001 me :Welcome to the network, me"));
        QCOMPARE(msg.command, QStringLiteral("001"));
        QCOMPARE(msg.params.at(0), QStringLiteral("me"));
        QVERIFY(msg.trailing().startsWith(QStringLiteral("Welcome")));
    }

    void trailingKeepsSpacesAndColons() {
        const auto msg = parseMessage(QStringLiteral(":n!u@h PRIVMSG #c :a : b : c"));
        QCOMPARE(msg.trailing(), QStringLiteral("a : b : c"));
    }

    void ircv3Tags() {
        const auto msg =
            parseMessage(QStringLiteral("@id=123;+comic/v1=char:3 :n!u@h PRIVMSG #c :hi"));
        QCOMPARE(msg.tags.value(QStringLiteral("id")), QStringLiteral("123"));
        QCOMPARE(msg.tags.value(QStringLiteral("+comic/v1")), QStringLiteral("char:3"));
        QCOMPARE(msg.command, QStringLiteral("PRIVMSG"));
        QCOMPARE(msg.trailing(), QStringLiteral("hi"));
    }

    void tagValueUnescaping() {
        const auto msg = parseMessage(QStringLiteral(R"(@k=a\sb\:c :n!u@h PRIVMSG #c :x)"));
        QCOMPARE(msg.tags.value(QStringLiteral("k")), QStringLiteral("a b;c"));
    }
};

QTEST_MAIN(IrcMessageTest)

#include "irc_message_test.moc"
