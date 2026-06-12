#include "ui/TerminalFrame.h"

#include <QtTest/QtTest>

using maxchat::ui::TerminalFrame;

class TerminalFrameTest final : public QObject {
    Q_OBJECT

  private slots:
    void parsesBasicOps() {
        QVector<TerminalFrame::Op> ops;
        QString error;
        QVERIFY2(TerminalFrame::parse(QStringLiteral("CP0101A0FW09Retro-BBSN"), &ops, &error),
                 qPrintable(error));
        QCOMPARE(ops.size(), 5);
        QCOMPARE(ops.at(0).type, TerminalFrame::OpType::Clear);
        QCOMPARE(ops.at(1).type, TerminalFrame::OpType::Position);
        QCOMPARE(ops.at(1).row, 1);
        QCOMPARE(ops.at(1).col, 1);
        QCOMPARE(ops.at(2).type, TerminalFrame::OpType::Attribute);
        QCOMPARE(ops.at(2).fg, 0);
        QCOMPARE(ops.at(2).bg, 15);
        QCOMPARE(ops.at(3).type, TerminalFrame::OpType::Write);
        QCOMPARE(ops.at(3).text, QStringLiteral("Retro-BBS"));
        QCOMPARE(ops.at(4).type, TerminalFrame::OpType::Newline);
    }

    void respectsUtf8ByteLengths() {
        QVector<TerminalFrame::Op> ops;
        QString error;
        QVERIFY2(TerminalFrame::parse(QStringLiteral("W03░W02Hi"), &ops, &error),
                 qPrintable(error));
        QCOMPARE(ops.size(), 2);
        QCOMPARE(ops.at(0).text, QStringLiteral("░"));
        QCOMPARE(ops.at(1).text, QStringLiteral("Hi"));
    }

    void rejectsBadFrames() {
        QVector<TerminalFrame::Op> ops;
        QString error;
        QVERIFY(!TerminalFrame::parse(QStringLiteral("P01"), &ops, &error));
        QVERIFY(error.contains(QStringLiteral("position")));

        QVERIFY(!TerminalFrame::parse(QStringLiteral("W04abc"), &ops, &error));
        QVERIFY(error.contains(QStringLiteral("truncated")));

        QVERIFY(!TerminalFrame::parse(QStringLiteral("Z"), &ops, &error));
        QVERIFY(error.contains(QStringLiteral("unknown")));
    }
};

QTEST_MAIN(TerminalFrameTest)

#include "terminal_frame_test.moc"
