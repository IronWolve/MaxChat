#include "services/LinkPreviewPolicy.h"

#include <QtTest/QtTest>

using maxchat::services::isLinkPreviewEnabled;
using maxchat::services::LinkPreviewCandidate;
using maxchat::services::LinkPreviewKind;
using maxchat::services::linkPreviewTogglesFromServices;
using maxchat::services::linkPreviewTogglesFromSettings;

namespace {

LinkPreviewCandidate candidate(LinkPreviewKind kind) {
  LinkPreviewCandidate candidate;
  candidate.kind = kind;
  candidate.fetchUrl = QUrl(QStringLiteral("https://example.com/preview"));
  return candidate;
}

} // namespace

class LinkPreviewPolicyTest final : public QObject {
  Q_OBJECT

private slots:
  void defaultsMissingServiceKeysToEnabled() {
    const auto toggles = linkPreviewTogglesFromServices({});

    QVERIFY(toggles.images);
    QVERIFY(toggles.media);
    QVERIFY(toggles.xCards);
    QVERIFY(toggles.webCards);
  }

  void readsNestedContentServicesMap() {
    QVariantMap services;
    services.insert(QStringLiteral("images"), false);
    services.insert(QStringLiteral("media"), false);
    services.insert(QStringLiteral("xcards"), true);
    services.insert(QStringLiteral("webcards"), false);

    QVariantMap settings;
    settings.insert(QStringLiteral("content_services"), services);

    const auto toggles = linkPreviewTogglesFromSettings(settings);

    QCOMPARE(toggles.images, false);
    QCOMPARE(toggles.media, false);
    QCOMPARE(toggles.xCards, true);
    QCOMPARE(toggles.webCards, false);
  }

  void mapsPreviewTypesToExpectedToggles() {
    QVariantMap services;
    services.insert(QStringLiteral("images"), false);
    services.insert(QStringLiteral("media"), false);
    services.insert(QStringLiteral("xcards"), false);
    services.insert(QStringLiteral("webcards"), true);
    const auto toggles = linkPreviewTogglesFromServices(services);

    QVERIFY(!isLinkPreviewEnabled(candidate(LinkPreviewKind::DirectImage),
                                  toggles));
    QVERIFY(!isLinkPreviewEnabled(candidate(LinkPreviewKind::DirectAudio),
                                  toggles));
    QVERIFY(!isLinkPreviewEnabled(candidate(LinkPreviewKind::DirectVideo),
                                  toggles));
    QVERIFY(!isLinkPreviewEnabled(candidate(LinkPreviewKind::XPost), toggles));
    QVERIFY(
        isLinkPreviewEnabled(candidate(LinkPreviewKind::OpenGraph), toggles));
    QVERIFY(isLinkPreviewEnabled(candidate(LinkPreviewKind::MastodonPost),
                                 toggles));
    QVERIFY(!isLinkPreviewEnabled(candidate(LinkPreviewKind::None), toggles));
  }
};

QTEST_APPLESS_MAIN(LinkPreviewPolicyTest)

#include "link_preview_policy_test.moc"
