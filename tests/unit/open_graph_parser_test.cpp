#include "services/OpenGraphParser.h"

#include <QtTest/QtTest>

using maxchat::services::parseOpenGraphCard;

class OpenGraphParserTest final : public QObject {
    Q_OBJECT

  private slots:
    void parsesOpenGraphMetadataWithAttributesInAnyOrder() {
        const QString html = QStringLiteral(R"(
            <html><head>
            <meta content="Article &amp; Title" property="og:title">
            <meta property="og:description" content="A compact  summary">
            <meta property="og:image" content="/images/card.png">
            <meta property="og:url" content="https://example.org/story">
            <meta property="og:site_name" content="Example Site">
            <meta property="og:type" content="article">
            </head></html>
        )");

        const auto card =
            parseOpenGraphCard(html, QUrl(QStringLiteral("https://example.org/post")));

        QCOMPARE(card.title, QStringLiteral("Article & Title"));
        QCOMPARE(card.description, QStringLiteral("A compact summary"));
        QCOMPARE(card.imageUrl.toString(), QStringLiteral("https://example.org/images/card.png"));
        QCOMPARE(card.canonicalUrl.toString(), QStringLiteral("https://example.org/story"));
        QCOMPARE(card.siteName, QStringLiteral("Example Site"));
        QCOMPARE(card.type, QStringLiteral("article"));
        QVERIFY(!card.isEmpty());
    }

    void usesTwitterAndTitleFallbacks() {
        const QString html = QStringLiteral(R"(
            <html><head>
            <title>Fallback Title</title>
            <meta name="twitter:description" content="Twitter summary">
            <meta name="twitter:image" content="https://cdn.example.net/card.jpg">
            </head></html>
        )");

        const auto card =
            parseOpenGraphCard(html, QUrl(QStringLiteral("https://example.net/page")));

        QCOMPARE(card.title, QStringLiteral("Fallback Title"));
        QCOMPARE(card.description, QStringLiteral("Twitter summary"));
        QCOMPARE(card.imageUrl.toString(), QStringLiteral("https://cdn.example.net/card.jpg"));
        QCOMPARE(card.canonicalUrl.toString(), QStringLiteral("https://example.net/page"));
    }

    void parsesDescriptionAndCanonicalFallbacks() {
        const QString html = QStringLiteral(R"(
            <html><head>
            <meta name="description" content="Generic description">
            <link rel="canonical" href="/canonical">
            </head></html>
        )");

        const auto card = parseOpenGraphCard(html, QUrl(QStringLiteral("https://example.com/a/b")));

        QCOMPARE(card.description, QStringLiteral("Generic description"));
        QCOMPARE(card.canonicalUrl.toString(), QStringLiteral("https://example.com/canonical"));
    }

    void emptyHtmlProducesEmptyCardWithoutPageUrl() {
        const auto card = parseOpenGraphCard(QString(), QUrl());

        QVERIFY(card.isEmpty());
    }
};

QTEST_APPLESS_MAIN(OpenGraphParserTest)

#include "open_graph_parser_test.moc"
