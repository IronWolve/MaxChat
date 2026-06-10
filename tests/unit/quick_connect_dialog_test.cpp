#include "ui/QuickConnectDialog.h"

#include <QDialogButtonBox>
#include <QPushButton>
#include <QtTest/QtTest>

using maxchat::ui::QuickConnectDialog;

class QuickConnectDialogTest final : public QObject {
    Q_OBJECT

  private slots:
    void defaultsAreTlsAndRequireHost() {
        QuickConnectDialog dialog;
        auto* buttons = dialog.findChild<QDialogButtonBox*>();
        QVERIFY(buttons != nullptr);
        QVERIFY(!buttons->button(QDialogButtonBox::Ok)->isEnabled());

        dialog.setConnectionValues(QStringLiteral("irc.example.net"), 6697, true,
                                   QStringLiteral("tester"), QStringLiteral("#maxchat"));

        QVERIFY(buttons->button(QDialogButtonBox::Ok)->isEnabled());
        const auto network = dialog.network();
        QCOMPARE(network.value(QStringLiteral("name")).toString(),
                 QStringLiteral("irc.example.net"));
        QCOMPARE(network.value(QStringLiteral("host")).toString(),
                 QStringLiteral("irc.example.net"));
        QCOMPARE(network.value(QStringLiteral("port")).toInt(), 6697);
        QCOMPARE(network.value(QStringLiteral("tls")).toBool(), true);
        QCOMPARE(network.value(QStringLiteral("nick")).toString(), QStringLiteral("tester"));
        QCOMPARE(network.value(QStringLiteral("channels")).toString(), QStringLiteral("#maxchat"));
    }

    void blankNickFallsBackToComicfan() {
        QuickConnectDialog dialog;
        dialog.setConnectionValues(QStringLiteral("irc.example.net"), 6667, false, QString(),
                                   QString());

        const auto network = dialog.network();

        QCOMPARE(network.value(QStringLiteral("nick")).toString(), QStringLiteral("comicfan"));
        QCOMPARE(network.value(QStringLiteral("tls")).toBool(), false);
    }
};

QTEST_MAIN(QuickConnectDialogTest)

#include "quick_connect_dialog_test.moc"
