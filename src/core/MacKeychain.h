#pragma once
#include <QByteArray>
#include <QString>
#include <Security/Security.h>

namespace maxchat::core {
// An explicit keychain is used by isolated tests. Normal helpers use the current
// default file keychain, and never search other keychains or fall back to disk.
bool macKeychainRequest(const QString& operation, const QString& id, QByteArray& value,
                        SecKeychainRef keychain = nullptr);
} // namespace maxchat::core
