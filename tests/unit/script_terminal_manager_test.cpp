#include "ui/ScriptTerminalManager.h"
#include "ui/ScriptTerminalDialog.h"
#include "ui/TerminalProfile.h"

#include <QSignalSpy>
#include <QtTest/QtTest>

using maxchat::ui::ScriptTerminalManager;
using maxchat::ui::TerminalInfo;
using maxchat::ui::terminalProfile;

class ScriptTerminalManagerTest final : public QObject {
    Q_OBJECT

  private slots:
    void assignsTermNumbersInCreationOrder() {
        ScriptTerminalManager mgr;
        mgr.openTerminal(QStringLiteral("bbs\x1f""a"), QStringLiteral("A"),
                         terminalProfile(QStringLiteral("ibm-vga")), QStringLiteral("synIRC"),
                         QStringLiteral("bbs"));
        mgr.openTerminal(QStringLiteral("bbs\x1f""b"), QStringLiteral("B"),
                         terminalProfile(QStringLiteral("ibm-vga")), QStringLiteral("synIRC"),
                         QStringLiteral("bbs"));

        const QList<TerminalInfo> terms = mgr.terminals();
        QCOMPARE(terms.size(), 2);
        QCOMPARE(terms.at(0).label, QStringLiteral("Term 1"));
        QCOMPARE(terms.at(1).label, QStringLiteral("Term 2"));
        QCOMPARE(terms.at(0).network, QStringLiteral("synIRC"));
        QCOMPARE(terms.at(0).scriptName, QStringLiteral("bbs"));
    }

    void reopeningSameIdDoesNotDuplicate() {
        ScriptTerminalManager mgr;
        const QString id = QStringLiteral("bbs\x1f""only");
        mgr.openTerminal(id, QStringLiteral("T"), terminalProfile(QStringLiteral("ibm-vga")),
                         QStringLiteral("net"), QStringLiteral("bbs"));
        mgr.openTerminal(id, QStringLiteral("T"), terminalProfile(QStringLiteral("ibm-vga")),
                         QStringLiteral("net"), QStringLiteral("bbs"));
        QCOMPARE(mgr.terminals().size(), 1);
        QVERIFY(mgr.hasTerminal(id));
    }

    void closeHidesButKeepsSession() {
        ScriptTerminalManager mgr;
        const QString id = QStringLiteral("bbs\x1f""keep");
        mgr.openTerminal(id, QStringLiteral("T"), terminalProfile(QStringLiteral("ibm-vga")),
                         QStringLiteral("net"), QStringLiteral("bbs"));
        QSignalSpy closedSpy(&mgr, &ScriptTerminalManager::terminalClosed);

        mgr.closeTerminal(id); // hide only
        QVERIFY(mgr.hasTerminal(id));
        QCOMPARE(mgr.terminals().size(), 1);
        QCOMPARE(closedSpy.count(), 0);
    }

    void killDestroysAndSignals() {
        ScriptTerminalManager mgr;
        const QString id = QStringLiteral("bbs\x1f""die");
        mgr.openTerminal(id, QStringLiteral("T"), terminalProfile(QStringLiteral("ibm-vga")),
                         QStringLiteral("net"), QStringLiteral("bbs"));
        QSignalSpy closedSpy(&mgr, &ScriptTerminalManager::terminalClosed);
        QSignalSpy changedSpy(&mgr, &ScriptTerminalManager::terminalsChanged);

        mgr.killTerminal(id);
        QVERIFY(!mgr.hasTerminal(id));
        QCOMPARE(mgr.terminals().size(), 0);
        QCOMPARE(closedSpy.count(), 1);
        QCOMPARE(closedSpy.at(0).at(0).toString(), id);
        QCOMPARE(changedSpy.count(), 1);
    }

    void openEmitsTerminalsChanged() {
        ScriptTerminalManager mgr;
        QSignalSpy changedSpy(&mgr, &ScriptTerminalManager::terminalsChanged);
        mgr.openTerminal(QStringLiteral("bbs\x1f""x"), QStringLiteral("X"),
                         terminalProfile(QStringLiteral("ibm-vga")), QStringLiteral("net"),
                         QStringLiteral("bbs"));
        QCOMPARE(changedSpy.count(), 1);
    }

    void closeAllRemovesEverything() {
        ScriptTerminalManager mgr;
        mgr.openTerminal(QStringLiteral("bbs\x1f""1"), QStringLiteral("1"),
                         terminalProfile(QStringLiteral("ibm-vga")), QStringLiteral("net"),
                         QStringLiteral("bbs"));
        mgr.openTerminal(QStringLiteral("bbs\x1f""2"), QStringLiteral("2"),
                         terminalProfile(QStringLiteral("ibm-vga")), QStringLiteral("net"),
                         QStringLiteral("bbs"));
        mgr.closeAll();
        QCOMPARE(mgr.terminals().size(), 0);
    }
};

QTEST_MAIN(ScriptTerminalManagerTest)

#include "script_terminal_manager_test.moc"
