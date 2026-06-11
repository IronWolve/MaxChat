#include "services/LinkPreviewRenderer.h"

#include <QtTest/QtTest>

using maxchat::services::LinkPreviewCandidate;
using maxchat::services::LinkPreviewKind;
using maxchat::services::LinkPreviewRenderOptions;
using maxchat::services::OpenGraphCard;
using maxchat::services::primaryDomainForPreview;
using maxchat::services::renderDirectImagePreviewHtml;
using maxchat::services::renderDirectMediaPreviewHtml;
using maxchat::services::renderOpenGraphPreviewHtml;

class LinkPreviewRendererTest final : public QObject {
  Q_OBJECT

private slots:
  void rendersOpenGraphCardWithEscapingAndImageLink() {
    LinkPreviewCandidate candidate;
    candidate.kind = LinkPreviewKind::OpenGraph;
    candidate.fetchUrl = QUrl(QStringLiteral("https://example.com/fallback"));

    OpenGraphCard card;
    card.title = QStringLiteral("A <Title>");
    card.description = QStringLiteral("Summary & details");
    card.imageUrl =
        QUrl(QStringLiteral("https://cdn.example.com/card.png?x=1&y=2"));
    card.canonicalUrl =
        QUrl(QStringLiteral("https://example.com/story?ref=irc"));
    card.siteName = QStringLiteral("Example Site");

    const QString html = renderOpenGraphPreviewHtml(candidate, card);

    QVERIFY(html.contains(QStringLiteral("A &lt;Title&gt;")));
    // Description is shown alongside the image (image renders below the card).
    QVERIFY(html.contains(QStringLiteral("Summary &amp; details")));
    QVERIFY(html.contains(QStringLiteral("Example Site")));
    QVERIFY(html.contains(
        QStringLiteral("href=\"https://example.com/story?ref=irc\"")));
    QVERIFY(html.contains(QStringLiteral(
        "src=\"https://cdn.example.com/card.png?x=1&amp;y=2\"")));
  }

  void keepsAndEscapesDescriptionWhenNoImage() {
    LinkPreviewCandidate candidate;
    candidate.kind = LinkPreviewKind::OpenGraph;
    candidate.fetchUrl = QUrl(QStringLiteral("https://example.com/article"));

    OpenGraphCard card;
    card.title = QStringLiteral("Headline");
    card.description = QStringLiteral("Body & more");
    card.siteName = QStringLiteral("News");
    // no imageUrl — the description is the substance, so it must show (escaped).

    const QString html = renderOpenGraphPreviewHtml(candidate, card);

    QVERIFY(html.contains(QStringLiteral("Headline")));
    QVERIFY(html.contains(QStringLiteral("Body &amp; more")));
  }

  void fallsBackToCandidateTargetAndPrimaryDomain() {
    LinkPreviewCandidate candidate;
    candidate.kind = LinkPreviewKind::MastodonPost;
    candidate.fetchUrl =
        QUrl(QStringLiteral("https://www.mastodon.social/@user/123"));
    candidate.serviceName = QStringLiteral("Mastodon");

    OpenGraphCard card;
    card.description = QStringLiteral("A post without a title");

    const QString html = renderOpenGraphPreviewHtml(candidate, card);

    QVERIFY(html.contains(QStringLiteral(">Mastodon<")));
    QVERIFY(html.contains(QStringLiteral("mastodon.social")));
  }

  void socialPostKeepsTextEvenWithImage() {
    LinkPreviewCandidate candidate;
    candidate.kind = LinkPreviewKind::XPost;
    candidate.originalUrl = QUrl(QStringLiteral("https://x.com/user/status/1"));
    candidate.fetchUrl =
        QUrl(QStringLiteral("https://fxtwitter.com/user/status/1"));

    OpenGraphCard card;
    card.title = QStringLiteral("user");
    card.description = QStringLiteral("the actual tweet text");
    card.imageUrl = QUrl(QStringLiteral("https://pbs.twimg.com/media/x.jpg"));

    const QString html = renderOpenGraphPreviewHtml(candidate, card);

    // The post text is the content — it must survive even though there's an
    // image (unlike a generic web card, where the image replaces the blurb).
    QVERIFY(html.contains(QStringLiteral("the actual tweet text")));
    QVERIFY(html.contains(QStringLiteral("media/x.jpg")));
  }

  void rendersDirectImagePreview() {
    LinkPreviewCandidate candidate;
    candidate.kind = LinkPreviewKind::DirectImage;
    candidate.fetchUrl =
        QUrl(QStringLiteral("https://images.example.net/pic.jpg"));

    LinkPreviewRenderOptions options;
    options.maxImageWidth = 160;
    options.maxImageHeight = 90;

    const QString html = renderDirectImagePreviewHtml(candidate, options);

    QVERIFY(html.contains(
        QStringLiteral("href=\"https://images.example.net/pic.jpg\"")));
    QVERIFY(html.contains(
        QStringLiteral("src=\"https://images.example.net/pic.jpg\"")));
    QVERIFY(html.contains(QStringLiteral("max-width:160px")));
    QVERIFY(html.contains(QStringLiteral("max-height:90px")));
  }

  void rendersDirectMediaPreview() {
    LinkPreviewCandidate candidate;
    candidate.kind = LinkPreviewKind::DirectAudio;
    candidate.fetchUrl =
        QUrl(QStringLiteral("https://media.example.net/uploads/song.mp3"));

    const QString html = renderDirectMediaPreviewHtml(candidate);

    QVERIFY(html.contains(
        QStringLiteral("href=\"https://media.example.net/uploads/song.mp3\"")));
    QVERIFY(html.contains(QStringLiteral(">Audio</b>: song.mp3")));
    QVERIFY(html.contains(QStringLiteral("media.example.net")));
  }

  void rejectsNonMediaDirectPreview() {
    LinkPreviewCandidate candidate;
    candidate.kind = LinkPreviewKind::DirectImage;
    candidate.fetchUrl =
        QUrl(QStringLiteral("https://images.example.net/pic.jpg"));

    QCOMPARE(renderDirectMediaPreviewHtml(candidate), QString());
  }

  void trimsLongText() {
    LinkPreviewCandidate candidate;
    candidate.kind = LinkPreviewKind::OpenGraph;
    candidate.fetchUrl = QUrl(QStringLiteral("https://example.com/post"));

    OpenGraphCard card;
    card.title = QStringLiteral("1234567890");
    card.description = QStringLiteral("abcdefghijklmnopqrstuvwxyz");

    LinkPreviewRenderOptions options;
    options.maxTitleChars = 6;
    options.maxDescriptionChars = 9;

    const QString html = renderOpenGraphPreviewHtml(candidate, card, options);

    QVERIFY(html.contains(QStringLiteral("123...")));
    QVERIFY(html.contains(QStringLiteral("abcdef...")));
  }

  void primaryDomainDropsCommonPrefixes() {
    QCOMPARE(primaryDomainForPreview(
                 QUrl(QStringLiteral("https://www.example.com/"))),
             QStringLiteral("example.com"));
    QCOMPARE(primaryDomainForPreview(
                 QUrl(QStringLiteral("https://mobile.twitter.com/a"))),
             QStringLiteral("twitter.com"));
  }
};

QTEST_APPLESS_MAIN(LinkPreviewRendererTest)

#include "link_preview_renderer_test.moc"
