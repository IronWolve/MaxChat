#include "core/UrlDetector.h"

#include <QTest>

using maxchat::core::extractUrls;

class UrlDetectorTest final : public QObject {
    Q_OBJECT

  private slots:
    void extractsCommonUrlForms() {
        const QStringList urls = extractUrls(QStringLiteral(
            "See https://example.com/path?q=1 and http://irc.example.net plus www.maxchat.org"));

        QCOMPARE(urls, QStringList({QStringLiteral("https://example.com/path?q=1"),
                                    QStringLiteral("http://irc.example.net"),
                                    QStringLiteral("www.maxchat.org")}));
    }

    void trimsSentencePunctuationAndUnbalancedClosers() {
        const QStringList urls = extractUrls(
            QStringLiteral("Links: (https://example.com/path), https://example.net/test. "
                           "Balanced https://example.org/a_(b)."));

        QCOMPARE(urls, QStringList({QStringLiteral("https://example.com/path"),
                                    QStringLiteral("https://example.net/test"),
                                    QStringLiteral("https://example.org/a_(b)")}));
    }

    void removesCaseInsensitiveDuplicates() {
        const QStringList urls = extractUrls(
            QStringLiteral("https://Example.com https://example.com https://other.example"));

        QCOMPARE(urls, QStringList({QStringLiteral("https://Example.com"),
                                    QStringLiteral("https://other.example")}));
    }
};

QTEST_MAIN(UrlDetectorTest)

#include "url_detector_test.moc"
