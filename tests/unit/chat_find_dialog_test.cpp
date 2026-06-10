#include "ui/ChatFindDialog.h"

#include <QSignalSpy>
#include <QTest>

using maxchat::ui::ChatFindDialog;

class ChatFindDialogTest final : public QObject {
    Q_OBJECT

  private slots:
    void storesSearchOptions() {
        ChatFindDialog dialog;
        dialog.setSearchText(QStringLiteral("Needle"));
        dialog.setCaseSensitive(true);
        dialog.setWrapSearch(false);

        QCOMPARE(dialog.searchText(), QStringLiteral("Needle"));
        QCOMPARE(dialog.caseSensitive(), true);
        QCOMPARE(dialog.wrapSearch(), false);
    }

    void emitsFindRequestsWithCurrentOptions() {
        ChatFindDialog dialog;
        QSignalSpy next(&dialog, &ChatFindDialog::findNextRequested);
        QSignalSpy previous(&dialog, &ChatFindDialog::findPreviousRequested);

        dialog.setSearchText(QStringLiteral("needle"));
        dialog.setCaseSensitive(true);
        dialog.setWrapSearch(true);
        dialog.requestFindNext();
        dialog.requestFindPrevious();

        QCOMPARE(next.count(), 1);
        QCOMPARE(next.at(0).at(0).toString(), QStringLiteral("needle"));
        QCOMPARE(next.at(0).at(1).toBool(), true);
        QCOMPARE(next.at(0).at(2).toBool(), true);
        QCOMPARE(previous.count(), 1);
        QCOMPARE(previous.at(0).at(0).toString(), QStringLiteral("needle"));
    }
};

QTEST_MAIN(ChatFindDialogTest)

#include "chat_find_dialog_test.moc"
