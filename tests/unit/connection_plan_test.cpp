#include "core/ConnectionPlan.h"

#include <QtTest/QtTest>

using maxchat::core::connectionPlanFromNetwork;
using maxchat::core::hasConnectableServer;
using maxchat::core::NetworkConfig;
using maxchat::core::parseAutojoinChannels;

class ConnectionPlanTest final : public QObject {
    Q_OBJECT

  private slots:
    void autojoinChannelsNormalizeCommasSpacesAndPrefixes() {
        QCOMPARE(parseAutojoinChannels(QStringLiteral("#maxchat,linux chat &local")),
                 QStringList({QStringLiteral("#maxchat"), QStringLiteral("#linux"),
                              QStringLiteral("#chat"), QStringLiteral("&local")}));
    }

    void primaryServerStaysFirstAndFailoversAreParsed() {
        NetworkConfig network{
            {QStringLiteral("name"), QStringLiteral("ExampleNet")},
            {QStringLiteral("host"), QStringLiteral("irc.example.net")},
            {QStringLiteral("port"), 6667},
            {QStringLiteral("tls"), false},
            {QStringLiteral("nick"), QStringLiteral("tester")},
            {QStringLiteral("username"), QStringLiteral("ident")},
            {QStringLiteral("realname"), QStringLiteral("Real Tester")},
            {QStringLiteral("account"), QStringLiteral("services-account")},
            {QStringLiteral("password"), QStringLiteral("nickserv-secret")},
            {QStringLiteral("server_pass"), QStringLiteral("server-secret")},
            {QStringLiteral("accept_invalid_cert"), true},
            {QStringLiteral("allow_insecure_auth"), true},
            {QStringLiteral("channels"), QStringLiteral("#one two")},
            {QStringLiteral("servers"), QStringList{QStringLiteral("irc2.example.net:+6697"),
                                                    QStringLiteral("irc3.example.net:7000")}},
        };

        const auto plan = connectionPlanFromNetwork(network);

        QVERIFY(hasConnectableServer(plan));
        QCOMPARE(plan.networkName, QStringLiteral("ExampleNet"));
        QCOMPARE(plan.nick, QStringLiteral("tester"));
        QCOMPARE(plan.username, QStringLiteral("ident"));
        QCOMPARE(plan.realname, QStringLiteral("Real Tester"));
        QCOMPARE(plan.saslAccount, QStringLiteral("services-account"));
        QCOMPARE(plan.saslPassword, QStringLiteral("nickserv-secret"));
        QCOMPARE(plan.serverPassword, QStringLiteral("server-secret"));
        QCOMPARE(plan.acceptInvalidCertificate, true);
        QCOMPARE(plan.allowInsecureAuth, true);
        QCOMPARE(plan.autojoin, QStringList({QStringLiteral("#one"), QStringLiteral("#two")}));
        QCOMPARE(plan.reconnect.servers.size(), 3);
        QCOMPARE(plan.reconnect.servers.at(0).host, QStringLiteral("irc.example.net"));
        QCOMPARE(plan.reconnect.servers.at(0).port, 6667);
        QCOMPARE(plan.reconnect.servers.at(0).tls, false);
        QCOMPARE(plan.reconnect.servers.at(1).host, QStringLiteral("irc2.example.net"));
        QCOMPARE(plan.reconnect.servers.at(1).port, 6697);
        QCOMPARE(plan.reconnect.servers.at(1).tls, true);
        QCOMPARE(plan.reconnect.servers.at(2).host, QStringLiteral("irc3.example.net"));
        QCOMPARE(plan.reconnect.servers.at(2).port, 7000);
        QCOMPARE(plan.reconnect.servers.at(2).tls, false);
    }

    void duplicateServersAreSkipped() {
        NetworkConfig network{
            {QStringLiteral("host"), QStringLiteral("irc.example.net")},
            {QStringLiteral("port"), 6697},
            {QStringLiteral("tls"), true},
            {QStringLiteral("servers"), QStringList{QStringLiteral("irc.example.net:+6697"),
                                                    QStringLiteral("irc.example.net:6697")}},
        };

        const auto plan = connectionPlanFromNetwork(network);

        QCOMPARE(plan.reconnect.servers.size(), 2);
        QCOMPARE(plan.reconnect.servers.at(0).tls, true);
        QCOMPARE(plan.reconnect.servers.at(1).tls, false);
    }

    void emptyHostIsNotConnectable() {
        const auto plan = connectionPlanFromNetwork({});

        QVERIFY(!hasConnectableServer(plan));
        QCOMPARE(plan.nick, QStringLiteral("comicfan"));
    }
};

QTEST_APPLESS_MAIN(ConnectionPlanTest)

#include "connection_plan_test.moc"
