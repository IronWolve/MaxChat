#include "ui/AnsiRenderer.h"

#include <QtTest/QtTest>

using maxchat::ui::AnsiRenderer;

class AnsiRendererTest final : public QObject {
    Q_OBJECT

  private slots:
    void escapesPlainTextAndPreservesUnicode() {
        const QString html = AnsiRenderer::toHtml(QStringLiteral("<x>\n╔═╗"));
        QVERIFY(html.contains(QStringLiteral("&lt;x&gt;")));
        QVERIFY(html.contains(QStringLiteral("<br>")));
        QVERIFY(html.contains(QStringLiteral("╔═╗")));
    }

    void rendersSgrColorsAndReset() {
        const QString html = AnsiRenderer::toHtml(
            QStringLiteral("plain \x1b[31;1mred\x1b[0m plain"));
        QVERIFY(html.contains(QStringLiteral("color:#aa0000")));
        QVERIFY(html.contains(QStringLiteral("font-weight:700")));
        QVERIFY(html.contains(QStringLiteral(">red</span>")));
    }

    void rendersHotspotsAsLocalLinks() {
        const QString html = AnsiRenderer::toHtml(
            AnsiRenderer::hotspot(QStringLiteral("main-menu"), QStringLiteral("Main <Menu>")));
        QVERIFY(html.contains(QStringLiteral("mc-term:main-menu")));
        QVERIFY(html.contains(QStringLiteral("Main &lt;Menu&gt;")));
    }
};

QTEST_MAIN(AnsiRendererTest)

#include "ansi_renderer_test.moc"
