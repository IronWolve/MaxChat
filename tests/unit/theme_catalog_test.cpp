#include "ui/ThemeCatalog.h"

#include <QtTest/QtTest>

#include <QFileInfo>

using maxchat::ui::appThemeById;
using maxchat::ui::appThemes;
using maxchat::ui::effectiveWallpaperPath;
using maxchat::ui::normalizeWallpaperValue;
using maxchat::ui::styleSheetForAppearance;
using maxchat::ui::systemThemeId;

class ThemeCatalogTest final : public QObject {
    Q_OBJECT

  private slots:
    void bundledCatalogLoadsSynthwave() {
        const auto definition = appThemeById(QStringLiteral("synthwave"));
        QCOMPARE(definition.id, QStringLiteral("synthwave"));
        QCOMPARE(definition.wallpaper, QStringLiteral("synthwave.png"));
        QVERIFY(!definition.bgGradient.isEmpty());
    }

    void wallpaperResolvesToPlainExistingPath() {
        const QString path = effectiveWallpaperPath(QStringLiteral("synthwave"), QString());
        QVERIFY(!path.isEmpty());
        QVERIFY(!path.startsWith(QStringLiteral("file:")));
        QVERIFY(!path.contains(QLatin1Char('\\')));
        QVERIFY(QFileInfo(path).isFile());
    }

    void wallpaperOffSuppressesThemeDefault() {
        QCOMPARE(effectiveWallpaperPath(QStringLiteral("synthwave"), QStringLiteral("none")),
                 QString());
        QCOMPARE(effectiveWallpaperPath(systemThemeId(), QString()), QString());
    }

    void styleSheetEmbedsQuotedWallpaperPath() {
        const QString sheet = styleSheetForAppearance(
            QStringLiteral("synthwave"), QStringLiteral("follow"), QString());
        QVERIFY(sheet.contains(QStringLiteral("border-image: url(\"")));
        QVERIFY(!sheet.contains(QStringLiteral("file:")));
    }

    void styleSheetFallsBackToGradientWithoutWallpaper() {
        const QString sheet = styleSheetForAppearance(
            QStringLiteral("synthwave"), QStringLiteral("follow"), QStringLiteral("none"));
        QVERIFY(!sheet.contains(QStringLiteral("border-image")));
        QVERIFY(sheet.contains(QStringLiteral("qlineargradient")));
    }

    void darkThemeUsesSolidBackground() {
        const QString sheet = styleSheetForAppearance(
            QStringLiteral("dark"), QStringLiteral("follow"), QString());
        QVERIFY(!sheet.contains(QStringLiteral("border-image")));
        QVERIFY(sheet.contains(QStringLiteral("background:rgb(28,30,33);")));
    }

    void systemThemeProducesEmptyStyleSheet() {
        QCOMPARE(styleSheetForAppearance(systemThemeId(), QStringLiteral("follow"), QString()),
                 QString());
    }

    void monoTerminalChatThemeCarriesTerminalExtras() {
        const auto mono = maxchat::ui::chatThemeById(QStringLiteral("terminal-mono"));
        QCOMPARE(mono.id, QStringLiteral("terminal-mono"));
        QCOMPARE(mono.timestamp, QColor(95, 95, 110));
        QCOMPARE(mono.bracket, QColor(120, 120, 120));
        QCOMPARE(mono.system, QColor(108, 132, 168));
        QVERIFY(mono.monoNicks);
        QVERIFY(mono.nickPalette.isEmpty());
    }

    void loudTerminalChatThemeCarriesNickPalette() {
        const auto loud = maxchat::ui::chatThemeById(QStringLiteral("terminal-loud"));
        QCOMPARE(loud.id, QStringLiteral("terminal-loud"));
        QCOMPARE(loud.timestamp, QColor(0, 200, 200));
        QCOMPARE(loud.bracket, QColor(0, 205, 0));
        QCOMPARE(loud.system, QColor(0, 200, 200));
        QVERIFY(!loud.monoNicks);
        QCOMPARE(loud.nickPalette.size(), 10);
        QCOMPARE(loud.nickPalette.first(), QColor(0, 255, 255));
    }

    void plainChatThemesHaveNoExtras() {
        const auto terminal = maxchat::ui::chatThemeById(QStringLiteral("terminal"));
        QVERIFY(!terminal.timestamp.isValid());
        QVERIFY(!terminal.bracket.isValid());
        QVERIFY(!terminal.system.isValid());
        QVERIFY(!terminal.monoNicks);
        QVERIFY(terminal.nickPalette.isEmpty());
    }
};

QTEST_MAIN(ThemeCatalogTest)

#include "theme_catalog_test.moc"
