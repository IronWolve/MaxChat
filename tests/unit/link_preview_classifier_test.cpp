#include "services/LinkPreviewClassifier.h"

#include <QtTest/QtTest>

using maxchat::services::classifyLinkPreview;
using maxchat::services::isAllowedPreviewFetchUrl;
using maxchat::services::isDirectRasterImageUrl;
using maxchat::services::LinkPreviewKind;

class LinkPreviewClassifierTest final : public QObject {
  Q_OBJECT

private slots:
  void classifiesDirectRasterImages() {
    const auto candidate = classifyLinkPreview(
        QStringLiteral("https://cdn.example.com/a/photo.JPG?size=large"));

    QCOMPARE(candidate.kind, LinkPreviewKind::DirectImage);
    QCOMPARE(candidate.serviceName, QStringLiteral("Image"));
    QCOMPARE(candidate.normalizedHost, QStringLiteral("cdn.example.com"));
    QVERIFY(candidate.isPreviewable());
    QVERIFY(candidate.needsImageFetch());
    QVERIFY(!candidate.needsHtmlFetch());
    QVERIFY(isDirectRasterImageUrl(candidate.fetchUrl));
  }

  void classifiesDirectAudioAndVideoWithoutMultimediaDependency() {
    const auto audio = classifyLinkPreview(
        QUrl(QStringLiteral("https://media.example.net/sounds/notify.wav")));
    const auto video = classifyLinkPreview(
        QUrl(QStringLiteral("https://media.example.net/clips/demo.webm")));

    QCOMPARE(audio.kind, LinkPreviewKind::DirectAudio);
    QCOMPARE(video.kind, LinkPreviewKind::DirectVideo);
    QVERIFY(audio.needsMediaPlayer());
    QVERIFY(video.needsMediaPlayer());
  }

  void classifiesXStatusUrls() {
    const auto candidate = classifyLinkPreview(
        QStringLiteral("https://mobile.twitter.com/example/status/12345"));

    QCOMPARE(candidate.kind, LinkPreviewKind::XPost);
    QCOMPARE(candidate.serviceName, QStringLiteral("X / Twitter"));
    QCOMPARE(candidate.normalizedHost, QStringLiteral("twitter.com"));
    QVERIFY(candidate.needsHtmlFetch());
  }

  void classifiesMastodonStatusUrls() {
    const auto actorUrl = classifyLinkPreview(
        QStringLiteral("https://mastodon.social/@example/1100123456789"));
    const auto usersUrl = classifyLinkPreview(QStringLiteral(
        "https://fosstodon.org/users/example/statuses/1100123456789"));

    QCOMPARE(actorUrl.kind, LinkPreviewKind::MastodonPost);
    QCOMPARE(usersUrl.kind, LinkPreviewKind::MastodonPost);
    QCOMPARE(actorUrl.serviceName, QStringLiteral("Mastodon"));
    QVERIFY(actorUrl.needsHtmlFetch());
  }

  void fallsBackToOpenGraphForNormalWebPages() {
    const auto candidate = classifyLinkPreview(
        QStringLiteral("https://bsky.app/profile/example/post/abc"));

    QCOMPARE(candidate.kind, LinkPreviewKind::OpenGraph);
    QCOMPARE(candidate.serviceName, QStringLiteral("Website"));
    QVERIFY(candidate.needsHtmlFetch());
    QVERIFY(!candidate.needsImageFetch());
  }

  void rejectsUnsupportedSchemesAndPrivateFetchTargets() {
    QVERIFY(
        !classifyLinkPreview(QStringLiteral("irc://irc.example.net/#maxchat"))
             .isPreviewable());
    QVERIFY(
        !classifyLinkPreview(QStringLiteral("https://localhost/private.png"))
             .isPreviewable());
    QVERIFY(!classifyLinkPreview(QStringLiteral("https://192.168.1.10/card"))
                 .isPreviewable());
    QVERIFY(!classifyLinkPreview(
                 QStringLiteral("https://user:pass@example.com/card"))
                 .isPreviewable());

    QVERIFY(!isAllowedPreviewFetchUrl(
        QUrl(QStringLiteral("https://10.0.0.1/card"))));
    QVERIFY(isAllowedPreviewFetchUrl(
        QUrl(QStringLiteral("https://example.com/card"))));
  }

  void doesNotTreatSvgAsDirectImage() {
    const auto candidate = classifyLinkPreview(
        QStringLiteral("https://cdn.example.com/icons/logo.svg"));

    QVERIFY(!candidate.isPreviewable());
    QVERIFY(!isDirectRasterImageUrl(
        QUrl(QStringLiteral("https://cdn.example.com/logo.svg"))));
  }
};

QTEST_APPLESS_MAIN(LinkPreviewClassifierTest)

#include "link_preview_classifier_test.moc"
