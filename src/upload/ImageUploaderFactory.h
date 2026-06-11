#pragma once

#include <QVariantMap>

#include <memory>

class QNetworkAccessManager;
class QObject;

namespace maxchat::upload {

class ImageUploader;

// Creates the right ImageUploader implementation from the settings map.
// Returns nullptr when upload is disabled (service == "").
std::unique_ptr<ImageUploader> makeImageUploader(const QVariantMap &settings,
                                                  QNetworkAccessManager *manager,
                                                  QObject *parent = nullptr);

} // namespace maxchat::upload
