#pragma once

#include <QMetaType>
#include <QString>
#include <QUrl>

namespace maxchat::services {

struct OpenGraphCard {
  QString title;
  QString description;
  QUrl imageUrl;
  QUrl canonicalUrl;
  QString siteName;
  QString type;

  [[nodiscard]] bool isEmpty() const;
};

[[nodiscard]] OpenGraphCard parseOpenGraphCard(const QString &html,
                                               const QUrl &pageUrl = {});

} // namespace maxchat::services

Q_DECLARE_METATYPE(maxchat::services::OpenGraphCard)
