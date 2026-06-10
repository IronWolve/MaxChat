#include "ui/UrlListDialog.h"

#include <QSignalSpy>
#include <QTest>

using maxchat::ui::UrlListDialog;

class UrlListDialogTest final : public QObject {
    Q_OBJECT

  private slots:
    void retainsAppendsAndClearsUrls() {
        UrlListDialog dialog;
        dialog.setUrls({QStringLiteral("https://example.com"), QStringLiteral("www.maxchat.org")});

        QCOMPARE(dialog.urls(), QStringList({QStringLiteral("https://example.com"),
                                             QStringLiteral("www.maxchat.org")}));

        dialog.appendUrls({QStringLiteral("https://irc.example.net")});
        QCOMPARE(dialog.urls(), QStringList({QStringLiteral("https://example.com"),
                                             QStringLiteral("www.maxchat.org"),
                                             QStringLiteral("https://irc.example.net")}));

        dialog.clearUrls();
        QCOMPARE(dialog.urls(), QStringList());
    }

    void clearButtonCanNotifyOwner() {
        UrlListDialog dialog;
        QSignalSpy cleared(&dialog, &UrlListDialog::clearRequested);
        dialog.setUrls({QStringLiteral("https://example.com")});

        dialog.clearUrls();
        emit dialog.clearRequested();

        QCOMPARE(cleared.count(), 1);
    }
};

QTEST_MAIN(UrlListDialogTest)

#include "url_list_dialog_test.moc"
