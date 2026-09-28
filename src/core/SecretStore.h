#pragma once

#include <QByteArray>
#include <QString>
#include <memory>

namespace maxchat::core {

// Values never travel through command-line arguments, environment variables,
// temporary files, or diagnostic output. Tests inject an in-memory backend.
class SecretStore {
public:
    virtual ~SecretStore() = default;
    virtual bool read(const QString& id, QByteArray& value, QString& error) = 0;
    virtual bool write(const QString& id, const QByteArray& value, QString& error) = 0;
    virtual bool remove(const QString& id) = 0;
};

[[nodiscard]] std::shared_ptr<SecretStore> platformSecretStore();

} // namespace maxchat::core
