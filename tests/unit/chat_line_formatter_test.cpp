#include "core/ChatLineFormatter.h"

#include <QtTest/QtTest>

using maxchat::core::ChatLineFormatOptions;
using maxchat::core::formatChatLine;

namespace {

QString cc(ushort code) {
    return QString(QChar(code));
}

} // namespace

class ChatLineFormatterTest final : public QObject {
    Q_OBJECT

  private slots:
    void formatsTimestampAndAlignedNickColumn() {
        ChatLineFormatOptions options;
        options.timestamp = QStringLiteral("01:23 PM");
        options.nickColumnWidth = 8;

        const auto line = formatChatLine(QStringLiteral("<bob> hello"), options);

        QCOMPARE(line.plainText, QStringLiteral("01:23 PM    <bob> hello"));
        QCOMPARE(line.prefixPlain, QStringLiteral("01:23 PM    <bob> "));
        QCOMPARE(line.messagePlain, QStringLiteral("hello"));
        QVERIFY(line.hangingIndent);
        QVERIFY(!line.html.contains(QStringLiteral("<table")));
        QVERIFY(line.html.contains(QStringLiteral("01:23 PM")));
        QVERIFY(line.html.contains(QStringLiteral("&nbsp;&nbsp;&nbsp;<span")));
        QVERIFY(!line.html.contains(QStringLiteral("|</span>")));
        QVERIFY(line.html.contains(QStringLiteral("hello")));
    }

    void canDisableAlignmentAndFormatting() {
        ChatLineFormatOptions options;
        options.showTimestamp = false;
        options.alignNicks = false;
        options.renderFormatting = false;
        options.colorNicks = false;

        const auto line = formatChatLine(QStringLiteral("<alice> ") + cc(0x02) +
                                             QStringLiteral("bold <tag>") + cc(0x02),
                                         options);

        QCOMPARE(line.plainText, QStringLiteral("<alice> bold <tag>"));
        QCOMPARE(line.html, QStringLiteral("&lt;alice&gt; bold &lt;tag&gt;"));
    }

    void rendersIrcFormattingWhenEnabled() {
        ChatLineFormatOptions options;
        options.showTimestamp = false;
        options.alignNicks = false;
        options.colorNicks = false;

        const auto line =
            formatChatLine(QStringLiteral("<alice> ") + cc(0x02) + QStringLiteral("bold") +
                               cc(0x02) + QLatin1Char(' ') + cc(0x03) + QStringLiteral("4red"),
                           options);

        QCOMPARE(line.plainText, QStringLiteral("<alice> bold red"));
        QVERIFY(line.html.contains(QStringLiteral("font-weight:bold")));
        QVERIFY(line.html.toUpper().contains(QStringLiteral("#FF0000")));
    }

    void alignedSystemLinesKeepSeparatorColumn() {
        ChatLineFormatOptions options;
        options.showTimestamp = false;
        options.nickColumnWidth = 4;

        const auto line = formatChatLine(QStringLiteral("! Connected"), options);

        QCOMPARE(line.plainText, QStringLiteral("     ! Connected"));
        QCOMPARE(line.prefixPlain, QStringLiteral("     "));
        QCOMPARE(line.messagePlain, QStringLiteral("! Connected"));
        QVERIFY(line.hangingIndent);
        QVERIFY(!line.html.contains(QStringLiteral("<table")));
        QVERIFY(line.html.contains(QStringLiteral("&nbsp;&nbsp;&nbsp;&nbsp;")));
        QVERIFY(!line.html.contains(QStringLiteral("|</span>")));
    }

    void scopedServerLabelsUseTheNickColumn() {
        ChatLineFormatOptions options;
        options.showTimestamp = false;
        options.nickColumnWidth = 8;

        const auto motd = formatChatLine(QStringLiteral("[motd] - hello"), options);
        const auto topic = formatChatLine(QStringLiteral("[topic] #chat: hello"), options);
        const auto plain = formatChatLine(QStringLiteral("! End of names"), options);

        QCOMPARE(motd.plainText, QStringLiteral("  [motd] - hello"));
        QCOMPARE(topic.plainText, QStringLiteral(" [topic] #chat: hello"));
        QCOMPARE(plain.plainText, QStringLiteral("         ! End of names"));
        QCOMPARE(motd.prefixPlain, QStringLiteral("  [motd] "));
        QCOMPARE(topic.prefixPlain, QStringLiteral(" [topic] "));
        QVERIFY(!motd.html.contains(QStringLiteral("<table")));
        QVERIFY(topic.html.contains(QStringLiteral("[topic]")));
    }

    void canDisableSeparatorLine() {
        ChatLineFormatOptions options;
        options.showTimestamp = false;
        options.nickColumnWidth = 8;
        options.separatorLine = false;

        const auto line = formatChatLine(QStringLiteral("<bob> hello"), options);

        QCOMPARE(line.plainText, QStringLiteral("   <bob> hello"));
        QVERIFY(!line.html.contains(QStringLiteral("|</span>")));
    }

    void usesThemeTimestampColor() {
        ChatLineFormatOptions options;
        options.timestamp = QStringLiteral("01:23 PM");
        options.timestampColor = QStringLiteral("#00c8c8");

        const auto line = formatChatLine(QStringLiteral("<bob> hello"), options);

        QVERIFY(line.html.contains(QStringLiteral("color:#00c8c8\">01:23 PM</span>")));
        QVERIFY(!line.html.contains(QStringLiteral("#8a8a8a")));
    }

    void monoNicksSkipNickColoring() {
        ChatLineFormatOptions options;
        options.showTimestamp = false;
        options.renderFormatting = false;
        options.monoNicks = true;

        const auto line = formatChatLine(QStringLiteral("<bob> hello"), options);

        QVERIFY(!line.html.contains(QStringLiteral("<span")));
        QVERIFY(line.html.contains(QStringLiteral("&lt;bob&gt;")));
    }

    void paletteNicksUseProvidedColors() {
        ChatLineFormatOptions options;
        options.showTimestamp = false;
        options.nickPalette = QStringList{QStringLiteral("#00ffff")};

        const auto line = formatChatLine(QStringLiteral("<bob> hello"), options);

        QVERIFY(line.html.contains(QStringLiteral("color:#00ffff")));
    }

    void bracketColorTintsMarksSeparately() {
        ChatLineFormatOptions options;
        options.showTimestamp = false;
        options.bracketColor = QStringLiteral("#00cd00");
        options.nickPalette = QStringList{QStringLiteral("#00ffff")};

        const auto line = formatChatLine(QStringLiteral("<bob> hello"), options);

        QVERIFY(line.html.contains(QStringLiteral("color:#00cd00\">&lt;</span>")));
        QVERIFY(line.html.contains(QStringLiteral("color:#00ffff\">bob</span>")));
        QVERIFY(line.html.contains(QStringLiteral("color:#00cd00\">&gt;</span>")));
    }

    void systemLinesTintWholeBody() {
        ChatLineFormatOptions options;
        options.showTimestamp = false;
        options.systemColor = QStringLiteral("#6c84a8");
        options.systemLine = true;

        const auto line = formatChatLine(QStringLiteral("* bob joined #chan"), options);

        QVERIFY(line.html.contains(QStringLiteral("color:#6c84a8")));
        QVERIFY(line.html.contains(QStringLiteral("joined #chan")));
        // The nick keeps the system tint instead of its hash color.
        QVERIFY(!line.html.contains(QStringLiteral("color:#e0")));
    }

    void systemColorLeavesChatMessagesAlone() {
        ChatLineFormatOptions options;
        options.showTimestamp = false;
        options.systemColor = QStringLiteral("#6c84a8");
        options.systemLine = false;

        const auto line = formatChatLine(QStringLiteral("<bob> hello"), options);

        QVERIFY(!line.html.contains(QStringLiteral("#6c84a8")));
    }
};

QTEST_APPLESS_MAIN(ChatLineFormatterTest)

#include "chat_line_formatter_test.moc"
