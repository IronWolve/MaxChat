#include "core/CommandAlias.h"

#include <QTest>

using maxchat::core::defaultCommandAliases;
using maxchat::core::expandCommandAliases;

class CommandAliasTest final : public QObject {
    Q_OBJECT

  private slots:
    void leavesPlainTextAndUnknownCommandsAlone() {
        const QVariantMap aliases = defaultCommandAliases();

        auto plain = expandCommandAliases(QStringLiteral("hello there"), aliases);
        QCOMPARE(plain.expanded, false);
        QCOMPARE(plain.commandLine, QStringLiteral("hello there"));

        auto unknown = expandCommandAliases(QStringLiteral("/mode #chat +o bob"), aliases);
        QCOMPARE(unknown.expanded, false);
        QCOMPARE(unknown.commandLine, QStringLiteral("/mode #chat +o bob"));
    }

    void expandsDefaultAliases() {
        const QVariantMap aliases = defaultCommandAliases();

        auto join = expandCommandAliases(QStringLiteral("/J maxchat"), aliases);
        QCOMPARE(join.expanded, true);
        QCOMPARE(join.commandLine, QStringLiteral("/join maxchat"));

        auto whois = expandCommandAliases(QStringLiteral("/w alice"), aliases);
        QCOMPARE(whois.commandLine, QStringLiteral("/whois alice"));
    }

    void replacesArgumentPlaceholders() {
        QVariantMap aliases;
        aliases.insert(QStringLiteral("kb"), QStringLiteral("/mode $1 +b $2-"));
        aliases.insert(QStringLiteral("say"), QStringLiteral("/msg $1 $2-"));
        aliases.insert(QStringLiteral("echo"), QStringLiteral("/raw PRIVMSG $*"));

        QCOMPARE(expandCommandAliases(QStringLiteral("/kb #chat *!*@bad.host because"), aliases)
                     .commandLine,
                 QStringLiteral("/mode #chat +b *!*@bad.host because"));
        QCOMPARE(
            expandCommandAliases(QStringLiteral("/say alice hello there"), aliases).commandLine,
            QStringLiteral("/msg alice hello there"));
        QCOMPARE(expandCommandAliases(QStringLiteral("/echo #chat :hello"), aliases).commandLine,
                 QStringLiteral("/raw PRIVMSG #chat :hello"));
    }

    void appendsArgumentsWhenTemplateHasNoPlaceholder() {
        QVariantMap aliases;
        aliases.insert(QStringLiteral("away"), QStringLiteral("/raw AWAY"));

        const auto expanded = expandCommandAliases(QStringLiteral("/away getting coffee"), aliases);
        QCOMPARE(expanded.commandLine, QStringLiteral("/raw AWAY getting coffee"));
    }

    void substitutesMeAndChanPlaceholders() {
        QVariantMap aliases;
        aliases.insert(QStringLiteral("wave"),
                       QStringLiteral("/me waves at $1- in $chan as $me"));

        const auto expanded = expandCommandAliases(QStringLiteral("/wave bob carol"), aliases,
                                                   QStringLiteral("alice"), QStringLiteral("#chat"));
        QCOMPARE(expanded.commandLine,
                 QStringLiteral("/me waves at bob carol in #chat as alice"));

        // No nick/channel context → $me/$chan expand to nothing (Python parity).
        const auto bare = expandCommandAliases(QStringLiteral("/wave bob"), aliases);
        QCOMPARE(bare.commandLine, QStringLiteral("/me waves at bob in  as"));
    }

    void shipsClassicFunAliases() {
        const QVariantMap aliases = defaultCommandAliases();
        QVERIFY(aliases.contains(QStringLiteral("slap")));
        QVERIFY(aliases.contains(QStringLiteral("fish")));

        const auto slap = expandCommandAliases(QStringLiteral("/slap bob"), aliases);
        QCOMPARE(slap.commandLine,
                 QStringLiteral("/me slaps bob around a bit with a large trout"));
    }

    void supportsChainedExpansionWithLoopCap() {
        QVariantMap aliases;
        aliases.insert(QStringLiteral("one"), QStringLiteral("/two $*"));
        aliases.insert(QStringLiteral("two"), QStringLiteral("/raw NOTICE $*"));

        const auto expanded = expandCommandAliases(QStringLiteral("/one nick :hello"), aliases);
        QCOMPARE(expanded.expansionCount, 2);
        QCOMPARE(expanded.commandLine, QStringLiteral("/raw NOTICE nick :hello"));
    }
};

QTEST_APPLESS_MAIN(CommandAliasTest)

#include "command_alias_test.moc"
