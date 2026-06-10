#include "core/DefaultNetworks.h"

#include <QtTest/QtTest>

#include <algorithm>

using maxchat::core::allServers;
using maxchat::core::defaultNetworks;
using maxchat::core::parseServerSpec;
using maxchat::core::serverSpec;

class DefaultNetworksTest final : public QObject {
    Q_OBJECT

  private:
    static qsizetype indexOfNetwork(const QList<maxchat::core::NetworkDefaults>& networks,
                                    const QString& name) {
        for (qsizetype i = 0; i < networks.size(); ++i) {
            if (networks.at(i).name == name) {
                return i;
            }
        }
        return -1;
    }

  private slots:
    void catalogShapeMatchesCurrentPythonDefaults() {
        const auto networks = defaultNetworks();

        QCOMPARE(networks.size(), 146);
        QCOMPARE(networks.at(0).name, QStringLiteral("Libera.Chat"));
        QCOMPARE(networks.at(1).name, QStringLiteral("Freenode"));
        QCOMPARE(networks.at(2).name, QStringLiteral("OFTC"));
    }

    void primaryServerIsFirstInExpandedServerList() {
        const auto networks = defaultNetworks();
        const auto liberaServers = allServers(networks.first());

        QVERIFY(!liberaServers.isEmpty());
        QCOMPARE(liberaServers.first().host, QStringLiteral("irc.libera.chat"));
        QCOMPARE(liberaServers.first().port, 6697);
        QCOMPARE(liberaServers.first().tls, true);
        QCOMPARE(liberaServers.at(1).host, QStringLiteral("irc.libera.chat"));
        QCOMPARE(liberaServers.at(1).port, 6667);
        QCOMPARE(liberaServers.at(1).tls, false);
    }

    void curatedNetworksKeepHomepagesAndFailovers() {
        const auto networks = defaultNetworks();

        const qsizetype efnetIndex = indexOfNetwork(networks, QStringLiteral("EFnet"));
        QVERIFY(efnetIndex >= 0);
        QCOMPARE(networks.at(efnetIndex).website, QStringLiteral("https://www.efnet.org/"));
        const auto efnetServers = allServers(networks.at(efnetIndex));
        QVERIFY(std::any_of(efnetServers.cbegin(), efnetServers.cend(), [](const auto& server) {
            return server.host == QStringLiteral("irc.underworld.no") && server.port == 6697 &&
                   server.tls;
        }));

        const qsizetype pureIndex = indexOfNetwork(networks, QStringLiteral("PureIRC"));
        QVERIFY(pureIndex >= 0);
        QCOMPARE(networks.at(pureIndex).website, QStringLiteral("https://pureirc.com/"));
        QVERIFY(allServers(networks.at(pureIndex)).size() >= 4);
    }

    void everyNetworkHasHttpHomepage() {
        const auto networks = defaultNetworks();
        for (const auto& network : networks) {
            QVERIFY2(network.website.startsWith(QStringLiteral("http://")) ||
                         network.website.startsWith(QStringLiteral("https://")),
                     qPrintable(network.name + QStringLiteral(" has no http homepage")));
        }
    }

    void parserHandlesPlainAndTlsPortSpecs() {
        const auto plain = parseServerSpec(QStringLiteral("irc.example.test:6667"), 7000, true);
        QCOMPARE(plain.host, QStringLiteral("irc.example.test"));
        QCOMPARE(plain.port, 6667);
        QCOMPARE(plain.tls, false);
        QCOMPARE(serverSpec(plain), QStringLiteral("irc.example.test:6667"));

        const auto tls = parseServerSpec(QStringLiteral("irc.example.test:+6697"));
        QCOMPARE(tls.host, QStringLiteral("irc.example.test"));
        QCOMPARE(tls.port, 6697);
        QCOMPARE(tls.tls, true);
        QCOMPARE(serverSpec(tls), QStringLiteral("irc.example.test:+6697"));

        const auto defaulted = parseServerSpec(QStringLiteral("irc.example.test"), 6697, true);
        QCOMPARE(defaulted.host, QStringLiteral("irc.example.test"));
        QCOMPARE(defaulted.port, 6697);
        QCOMPARE(defaulted.tls, true);
    }
};

QTEST_MAIN(DefaultNetworksTest)

#include "default_networks_test.moc"
