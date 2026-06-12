#include "ui/TerminalProfile.h"

#include <QtTest/QtTest>

using maxchat::ui::terminalProfile;

class TerminalProfileTest final : public QObject {
    Q_OBJECT

  private slots:
    void ibmVgaProfileIsFixed80x25() {
        const auto profile = terminalProfile(QStringLiteral("ibm-vga"));
        QCOMPARE(profile.id, QStringLiteral("ibm-vga"));
        QCOMPARE(profile.cols, 80);
        QCOMPARE(profile.rows, 25);
        QCOMPARE(profile.fixedGrid, true);
        QCOMPARE(profile.fitMode, QStringLiteral("fit"));
    }

    void c64ProfileIsFixed40x25() {
        const auto profile = terminalProfile(QStringLiteral("c64"));
        QCOMPARE(profile.id, QStringLiteral("c64"));
        QCOMPARE(profile.cols, 40);
        QCOMPARE(profile.rows, 25);
        QCOMPARE(profile.fixedGrid, true);
        QCOMPARE(profile.fitMode, QStringLiteral("integer"));
    }

    void freeProfileUsesRequestedSize() {
        const auto profile = terminalProfile(QStringLiteral("free"), 100, 30);
        QCOMPARE(profile.id, QStringLiteral("free"));
        QCOMPARE(profile.cols, 100);
        QCOMPARE(profile.rows, 30);
        QCOMPARE(profile.fixedGrid, false);
    }
};

QTEST_GUILESS_MAIN(TerminalProfileTest)

#include "terminal_profile_test.moc"
