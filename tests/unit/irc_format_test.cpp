#include "irc/IrcFormat.h"

#include <QtTest/QtTest>

using maxchat::irc::nickColor;
using maxchat::irc::stripFormatting;
using maxchat::irc::toHtml;

namespace {

QString cc(ushort code) {
    return QString(QChar(code));
}

} // namespace

class IrcFormatTest final : public QObject {
    Q_OBJECT

  private slots:
    void plainTextIsEscapedWithNoMarkup() {
        QCOMPARE(toHtml(QStringLiteral("a <b> & c")), QStringLiteral("a &lt;b&gt; &amp; c"));
    }

    void bold() {
        const QString html = toHtml(cc(0x02) + QStringLiteral("hi") + cc(0x02));
        QVERIFY(html.contains(QStringLiteral("font-weight:bold")));
        QVERIFY(html.contains(QStringLiteral("hi")));
    }

    void colorTwoDigitIsReadAsOneNumber() {
        const QString html = toHtml(cc(0x03) + QStringLiteral("12blue")).toUpper();
        QVERIFY(html.contains(QStringLiteral("0000FC")));
        QVERIFY(html.contains(QStringLiteral("BLUE")));
    }

    void singleDigitColor() {
        QVERIFY(
            toHtml(cc(0x03) + QStringLiteral("4red")).toUpper().contains(QStringLiteral("FF0000")));
    }

    void resetClearsFormatting() {
        const QString html =
            toHtml(cc(0x02) + QStringLiteral("bold") + cc(0x0f) + QStringLiteral("plain"));
        QVERIFY(html.contains(QStringLiteral("plain")));
    }

    void underlineAndItalic() {
        const QString html =
            toHtml(cc(0x1d) + QStringLiteral("italic") + cc(0x1d) + QStringLiteral(" ") + cc(0x1f) +
                   QStringLiteral("under") + cc(0x1f));
        QVERIFY(html.contains(QStringLiteral("font-style:italic")));
        QVERIFY(html.contains(QStringLiteral("underline")));
    }

    void nickColorDeterministicAndPrefixInsensitive() {
        QCOMPARE(nickColor(QStringLiteral("alice")), nickColor(QStringLiteral("@alice")));
        QCOMPARE(nickColor(QStringLiteral("alice")), nickColor(QStringLiteral("Alice")));
        const QString color = nickColor(QStringLiteral("bob"));
        QVERIFY(color.startsWith(QLatin1Char('#')));
        QCOMPARE(color.size(), 7);
    }

    void stripFormattingRemovesControlCodes() {
        QCOMPARE(stripFormatting(cc(0x02) + QStringLiteral("bold") + cc(0x02) +
                                 QStringLiteral(" ") + cc(0x03) + QStringLiteral("4red") +
                                 cc(0x0f)),
                 QStringLiteral("bold red"));
    }
};

QTEST_MAIN(IrcFormatTest)

#include "irc_format_test.moc"
