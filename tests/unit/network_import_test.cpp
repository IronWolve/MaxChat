#include "core/NetworkImport.h"

#include <QtTest/QtTest>

using maxchat::core::mergeImportedNetworks;
using maxchat::core::NetworkConfig;
using maxchat::core::NetworkConfigList;
using maxchat::core::networkKey;

class NetworkImportTest final : public QObject {
    Q_OBJECT

  private slots:
    void keyUsesNameThenHost() {
        QCOMPARE(
            networkKey(NetworkConfig{{QStringLiteral("name"), QStringLiteral(" Libera.Chat ")}}),
            QStringLiteral("libera.chat"));
        QCOMPARE(networkKey(
                     NetworkConfig{{QStringLiteral("host"), QStringLiteral(" IRC.Example.Net ")}}),
                 QStringLiteral("irc.example.net"));
    }

    void importedNetworksKeepCurrentCatalogAndRestoreUserFields() {
        const NetworkConfigList base = {
            {
                {QStringLiteral("name"), QStringLiteral("Libera.Chat")},
                {QStringLiteral("host"), QStringLiteral("irc.libera.chat")},
                {QStringLiteral("port"), 6697},
                {QStringLiteral("tls"), true},
                {QStringLiteral("website"), QStringLiteral("https://libera.chat/")},
                {QStringLiteral("servers"),
                 QStringList{QStringLiteral("irc.libera.chat:6667"),
                             QStringLiteral("irc.us.libera.chat:+6697")}},
                {QStringLiteral("nick"), QStringLiteral("comicfan")},
            },
        };
        const NetworkConfigList imported = {
            {
                {QStringLiteral("name"), QStringLiteral("Libera.Chat")},
                {QStringLiteral("host"), QStringLiteral("old.libera.example")},
                {QStringLiteral("port"), 6667},
                {QStringLiteral("tls"), false},
                {QStringLiteral("website"), QStringLiteral("https://old.example/")},
                {QStringLiteral("servers"),
                 QStringList{QStringLiteral("stale.libera.example:6667")}},
                {QStringLiteral("nick"), QStringLiteral("savednick")},
                {QStringLiteral("realname"), QStringLiteral("Saved Name")},
                {QStringLiteral("username"), QStringLiteral("ident")},
                {QStringLiteral("account"), QStringLiteral("nickserv-account")},
                {QStringLiteral("password"), QStringLiteral("nickserv-pass")},
                {QStringLiteral("server_pass"), QStringLiteral("bouncer-pass")},
                {QStringLiteral("allow_insecure_auth"), true},
                {QStringLiteral("autoconnect"), true},
                {QStringLiteral("channels"), QStringLiteral("#maxchat,#test")},
                {QStringLiteral("perform"), QStringList{QStringLiteral("/mode savednick +i")}},
                {QStringLiteral("proxy_type"), QStringLiteral("socks5")},
                {QStringLiteral("proxy_host"), QStringLiteral("127.0.0.1")},
                {QStringLiteral("proxy_port"), 9050},
                {QStringLiteral("proxy_username"), QStringLiteral("proxy-user")},
                {QStringLiteral("proxy_password"), QStringLiteral("proxy-pass")},
            },
        };

        const NetworkConfigList merged = mergeImportedNetworks(imported, base);

        QCOMPARE(merged.size(), 1);
        const NetworkConfig& network = merged.first();
        QCOMPARE(network.value(QStringLiteral("host")).toString(),
                 QStringLiteral("irc.libera.chat"));
        QCOMPARE(network.value(QStringLiteral("port")).toInt(), 6697);
        QCOMPARE(network.value(QStringLiteral("tls")).toBool(), true);
        QCOMPARE(network.value(QStringLiteral("website")).toString(),
                 QStringLiteral("https://libera.chat/"));
        QCOMPARE(network.value(QStringLiteral("servers")).toStringList(),
                 QStringList({QStringLiteral("irc.libera.chat:6667"),
                              QStringLiteral("irc.us.libera.chat:+6697")}));
        QCOMPARE(network.value(QStringLiteral("nick")).toString(), QStringLiteral("savednick"));
        QCOMPARE(network.value(QStringLiteral("server_pass")).toString(),
                 QStringLiteral("bouncer-pass"));
        QCOMPARE(network.value(QStringLiteral("proxy_port")).toInt(), 9050);
    }

    void importedNetworksKeepCustomNetworksAndAppendMissingDefaults() {
        const NetworkConfigList base = {
            {{QStringLiteral("name"), QStringLiteral("Libera.Chat")},
             {QStringLiteral("host"), QStringLiteral("irc.libera.chat")},
             {QStringLiteral("port"), 6697},
             {QStringLiteral("tls"), true},
             {QStringLiteral("servers"), QStringList{}}},
            {{QStringLiteral("name"), QStringLiteral("EFnet")},
             {QStringLiteral("host"), QStringLiteral("irc.efnet.org")},
             {QStringLiteral("port"), 6667},
             {QStringLiteral("tls"), false},
             {QStringLiteral("servers"), QStringList{}}},
        };
        const NetworkConfig custom = {
            {QStringLiteral("name"), QStringLiteral("My Bouncer")},
            {QStringLiteral("host"), QStringLiteral("bouncer.example")},
            {QStringLiteral("port"), 6697},
            {QStringLiteral("tls"), true},
            {QStringLiteral("servers"),
             QStringList{QStringLiteral("backup-bouncer.example:+6697")}},
            {QStringLiteral("password"), QStringLiteral("secret")},
        };
        const NetworkConfigList imported = {
            {{QStringLiteral("name"), QStringLiteral("Libera.Chat")},
             {QStringLiteral("host"), QStringLiteral("old.libera.example")},
             {QStringLiteral("channels"), QStringLiteral("#libera")}},
            custom,
        };

        const NetworkConfigList merged = mergeImportedNetworks(imported, base);

        QCOMPARE(merged.size(), 3);
        QCOMPARE(merged.at(0).value(QStringLiteral("name")).toString(),
                 QStringLiteral("Libera.Chat"));
        QCOMPARE(merged.at(0).value(QStringLiteral("host")).toString(),
                 QStringLiteral("irc.libera.chat"));
        QCOMPARE(merged.at(0).value(QStringLiteral("channels")).toString(),
                 QStringLiteral("#libera"));
        QCOMPARE(merged.at(1), custom);
        QCOMPARE(merged.at(2).value(QStringLiteral("name")).toString(), QStringLiteral("EFnet"));
    }

    void duplicateDefaultNameIsKeptAsCustomAfterFirstMatch() {
        const NetworkConfigList base = {
            {{QStringLiteral("name"), QStringLiteral("Libera.Chat")},
             {QStringLiteral("host"), QStringLiteral("irc.libera.chat")},
             {QStringLiteral("port"), 6697},
             {QStringLiteral("tls"), true},
             {QStringLiteral("servers"), QStringList{}}},
        };
        const NetworkConfigList imported = {
            {{QStringLiteral("name"), QStringLiteral("Libera.Chat")},
             {QStringLiteral("host"), QStringLiteral("old.libera.example")},
             {QStringLiteral("nick"), QStringLiteral("main")}},
            {{QStringLiteral("name"), QStringLiteral("Libera.Chat")},
             {QStringLiteral("host"), QStringLiteral("znc.example")},
             {QStringLiteral("nick"), QStringLiteral("bouncer")},
             {QStringLiteral("password"), QStringLiteral("secret")}},
        };

        const NetworkConfigList merged = mergeImportedNetworks(imported, base);

        QCOMPARE(merged.size(), 2);
        QCOMPARE(merged.at(0).value(QStringLiteral("host")).toString(),
                 QStringLiteral("irc.libera.chat"));
        QCOMPARE(merged.at(0).value(QStringLiteral("nick")).toString(), QStringLiteral("main"));
        QCOMPARE(merged.at(1).value(QStringLiteral("host")).toString(),
                 QStringLiteral("znc.example"));
        QCOMPARE(merged.at(1).value(QStringLiteral("nick")).toString(), QStringLiteral("bouncer"));
        QCOMPARE(merged.at(1).value(QStringLiteral("password")).toString(),
                 QStringLiteral("secret"));
    }
};

QTEST_MAIN(NetworkImportTest)

#include "network_import_test.moc"
