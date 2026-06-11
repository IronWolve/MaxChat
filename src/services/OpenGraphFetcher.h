#pragma once

#include "services/OpenGraphParser.h"

#include <QNetworkRequest>
#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;

namespace maxchat::services {

struct OpenGraphFetchOptions {
  qsizetype maxBytes = 256 * 1024;
  int timeoutMs = 10000;
  bool allowPrivateNetwork = false;
};

class OpenGraphFetcher final : public QObject {
  Q_OBJECT

public:
  explicit OpenGraphFetcher(QNetworkAccessManager *manager,
                            QObject *parent = nullptr);

  [[nodiscard]] static QNetworkRequest buildRequest(const QUrl &url);
  void fetch(const QUrl &url, OpenGraphFetchOptions options = {});

signals:
  void cardFetched(const QUrl &url,
                   const maxchat::services::OpenGraphCard &card);
  void fetchFailed(const QUrl &url, const QString &reason);

private:
  void issueRequest(const QUrl &url, OpenGraphFetchOptions options); // after the SSRF gate passes

  QNetworkAccessManager *manager_ = nullptr;
};

} // namespace maxchat::services
