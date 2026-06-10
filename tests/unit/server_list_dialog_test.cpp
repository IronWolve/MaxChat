#include "ui/ServerListDialog.h"

#include "core/SettingsStore.h"

#include <QtTest/QtTest>

using maxchat::core::defaultNetworkConfigs;
using maxchat::core::NetworkConfig;
using maxchat::core::NetworkConfigList;
using maxchat::ui::ServerListDialog;

class ServerListDialogTest final : public QObject {
    Q_OBJECT

  private:
    static NetworkConfigList sampleNetworks() {
        return {
            {{QStringLiteral("name"), QStringLiteral("Libera.Chat")},
             {QStringLiteral("host"), QStringLiteral("irc.libera.chat")},
             {QStringLiteral("port"), 6697},
             {QStringLiteral("tls"), true},
             {QStringLiteral("website"), QStringLiteral("https://libera.chat/")},
             {QStringLiteral("servers"), QStringList{QStringLiteral("irc.libera.chat:6667")}}},
            {{QStringLiteral("name"), QStringLiteral("EFnet")},
             {QStringLiteral("host"), QStringLiteral("irc.efnet.org")},
             {QStringLiteral("port"), 6667},
             {QStringLiteral("tls"), false},
             {QStringLiteral("website"), QStringLiteral("https://www.efnet.org/")},
             {QStringLiteral("servers"), QStringList{QStringLiteral("irc.underworld.no:+6697")}}},
        };
    }

  private slots:
    void networkLabelIncludesNameHostAndHomepageDomain() {
        const QString label = ServerListDialog::networkLabel(sampleNetworks().first());

        QVERIFY(label.contains(QStringLiteral("Libera.Chat")));
        QVERIFY(label.contains(QStringLiteral("irc.libera.chat")));
        QVERIFY(label.contains(QStringLiteral("libera.chat")));
    }

    void homepageButtonTracksSelection() {
        ServerListDialog dialog(sampleNetworks(), false);

        dialog.setCurrentRow(0);
        QCOMPARE(dialog.currentHomepageButtonText(), QStringLiteral("https://libera.chat/"));
        dialog.setCurrentRow(1);
        QCOMPARE(dialog.currentHomepageButtonText(), QStringLiteral("https://www.efnet.org/"));
    }

    void moveDeleteAndResetMutateDialogNetworks() {
        ServerListDialog dialog(sampleNetworks(), true);
        QCOMPARE(dialog.connectOnStart(), true);

        dialog.setCurrentRow(0);
        QVERIFY(dialog.moveCurrentNetwork(1));
        QCOMPARE(dialog.networks().first().value(QStringLiteral("name")).toString(),
                 QStringLiteral("EFnet"));

        QVERIFY(dialog.removeCurrentNetwork());
        QCOMPARE(dialog.networks().size(), 1);
        QCOMPARE(dialog.networks().first().value(QStringLiteral("name")).toString(),
                 QStringLiteral("EFnet"));

        dialog.resetToDefaults();
        QCOMPARE(dialog.networks().size(), defaultNetworkConfigs().size());
        QCOMPARE(dialog.networks().first().value(QStringLiteral("name")).toString(),
                 QStringLiteral("Libera.Chat"));
    }

    void longHomepageFallsBackToDomain() {
        NetworkConfig network;
        network.insert(
            QStringLiteral("website"),
            QStringLiteral(
                "https://example.net/some/really/long/path/that/would/not/fit/in/a/button"));

        QCOMPARE(ServerListDialog::homepageButtonText(network), QStringLiteral("example.net"));
    }
};

QTEST_MAIN(ServerListDialogTest)

#include "server_list_dialog_test.moc"
