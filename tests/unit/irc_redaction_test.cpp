#include "irc/IrcRedaction.h"

#include <QtTest/QtTest>

using maxchat::irc::redactLine;

class IrcRedactionTest final : public QObject {
    Q_OBJECT

  private slots:
    void passIsMasked() {
        QCOMPARE(redactLine(QStringLiteral("PASS hunter2")), QStringLiteral("PASS ****"));
    }

    void saslPayloadMaskedButMechanismKept() {
        QCOMPARE(redactLine(QStringLiteral("AUTHENTICATE bXl1c2VyAGh1bnRlcg==")),
                 QStringLiteral("AUTHENTICATE ****"));
        QCOMPARE(redactLine(QStringLiteral("AUTHENTICATE PLAIN")),
                 QStringLiteral("AUTHENTICATE PLAIN"));
        QCOMPARE(redactLine(QStringLiteral("AUTHENTICATE +")), QStringLiteral("AUTHENTICATE +"));
    }

    void servicesPasswordsMasked() {
        QCOMPARE(redactLine(QStringLiteral("PRIVMSG NickServ :IDENTIFY hunter2")),
                 QStringLiteral("PRIVMSG NickServ :IDENTIFY ****"));
        QCOMPARE(redactLine(QStringLiteral("PRIVMSG NickServ :GHOST oldnick hunter2")),
                 QStringLiteral("PRIVMSG NickServ :GHOST ****"));
    }

    void normalTrafficUntouched() {
        QCOMPARE(redactLine(QStringLiteral("PRIVMSG #chan :hello there")),
                 QStringLiteral("PRIVMSG #chan :hello there"));
        QCOMPARE(redactLine(QStringLiteral(":srv 001 me :Welcome")),
                 QStringLiteral(":srv 001 me :Welcome"));
    }
};

QTEST_MAIN(IrcRedactionTest)

#include "irc_redaction_test.moc"
