#include "irc/ReconnectPlanner.h"

#include <QtTest/QtTest>

using maxchat::irc::chooseReconnectServer;
using maxchat::irc::ReconnectState;
using maxchat::irc::ServerRetryLimit;

class ReconnectPlannerTest final : public QObject {
    Q_OBJECT

  private:
    static ReconnectState makeState() {
        ReconnectState state;
        state.servers = {
            {QStringLiteral("server1.test"), 6667, false},
            {QStringLiteral("server2.test"), 6697, true},
        };
        state.serverIndex = 0;
        state.serverAttempt = 1;
        return state;
    }

  private slots:
    void autoReconnectRetriesCurrentServerThreeTimesBeforeFailover() {
        ReconnectState state = makeState();

        const auto first = chooseReconnectServer(state);
        const auto second = chooseReconnectServer(state);
        const auto third = chooseReconnectServer(state);

        QCOMPARE(ServerRetryLimit, 3);
        QCOMPARE(first.host, QStringLiteral("server1.test"));
        QCOMPARE(second.host, QStringLiteral("server1.test"));
        QCOMPARE(third.host, QStringLiteral("server2.test"));
        QCOMPARE(first.port, 6667);
        QCOMPARE(second.port, 6667);
        QCOMPARE(third.port, 6697);
        QCOMPARE(first.tls, false);
        QCOMPARE(second.tls, false);
        QCOMPARE(third.tls, true);
        QCOMPARE(state.serverIndex, 1);
        QCOMPARE(state.serverAttempt, 1);
    }

    void manualReconnectAdvancesToNextFailoverServer() {
        ReconnectState state = makeState();

        const auto server = chooseReconnectServer(state, true);

        QCOMPARE(server.host, QStringLiteral("server2.test"));
        QCOMPARE(server.port, 6697);
        QCOMPARE(server.tls, true);
        QCOMPARE(state.serverIndex, 1);
        QCOMPARE(state.serverAttempt, 1);
    }

    void emptyServerListIsSafe() {
        ReconnectState state;
        const auto server = chooseReconnectServer(state);

        QCOMPARE(server.host, QString());
        QCOMPARE(server.port, 6667);
        QCOMPARE(server.tls, false);
        QCOMPARE(state.serverIndex, 0);
        QCOMPARE(state.serverAttempt, 0);
    }
};

QTEST_MAIN(ReconnectPlannerTest)

#include "reconnect_planner_test.moc"
