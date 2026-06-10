#include "ui/RawLogDialog.h"

#include <QTest>

using maxchat::ui::RawLogDialog;

class RawLogDialogTest final : public QObject {
    Q_OBJECT

  private slots:
    void retainsAppendsAndClearsText() {
        RawLogDialog dialog;
        dialog.setLines(
            {QStringLiteral("<< :srv 001 nick :Welcome"), QStringLiteral(">> PONG :srv")});

        QCOMPARE(dialog.rawText(), QStringLiteral("<< :srv 001 nick :Welcome\n>> PONG :srv"));

        dialog.appendLine(QStringLiteral("<< PING :srv"));
        QCOMPARE(dialog.rawText(),
                 QStringLiteral("<< :srv 001 nick :Welcome\n>> PONG :srv\n<< PING :srv"));

        dialog.clearLog();
        QCOMPARE(dialog.rawText(), QString());
    }
};

QTEST_MAIN(RawLogDialogTest)

#include "raw_log_dialog_test.moc"
