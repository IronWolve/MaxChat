// Private stdin/stdout protocol for OS credential storage. Never log payloads.
// A separate, time-bounded process prevents a locked keyring from hanging the
// application indefinitely. No credentials are accepted on the command line.
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include <QUuid>
#include <cstdio>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincred.h>
#endif

namespace {
constexpr qsizetype MaximumValueBytes = 128 * 1024;

#ifdef Q_OS_WIN
QString target(const QString& id) { return QStringLiteral("MaxChat/credentials/") + id; }
bool readEntry(const QString& name, QByteArray& value) {
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(reinterpret_cast<LPCWSTR>(name.utf16()), CRED_TYPE_GENERIC, 0, &credential))
        return false;
    const bool valid = credential->CredentialBlobSize <= CRED_MAX_CREDENTIAL_BLOB_SIZE;
    if (valid)
        value = QByteArray(reinterpret_cast<const char*>(credential->CredentialBlob),
                           static_cast<qsizetype>(credential->CredentialBlobSize));
    CredFree(credential);
    return valid;
}
bool writeEntry(const QString& name, const QByteArray& value) {
    if (value.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE) return false;
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(name.utf16()));
    credential.CredentialBlobSize = static_cast<DWORD>(value.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(value.constData()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    return CredWriteW(&credential, 0) != FALSE;
}
bool removeEntries(const QString& id) {
    // UUID-scoped names: never enumerate or delete another application's items.
    const QString base = target(id);
    const QString filter = base + QStringLiteral("/*");
    DWORD count = 0;
    PCREDENTIALW* entries = nullptr;
    bool ok = true;
    if (CredEnumerateW(reinterpret_cast<LPCWSTR>(filter.utf16()), 0, &count, &entries)) {
        for (DWORD i = 0; i < count; ++i)
            ok = (CredDeleteW(entries[i]->TargetName, CRED_TYPE_GENERIC, 0) != FALSE) && ok;
        CredFree(entries);
    } else if (GetLastError() != ERROR_NOT_FOUND) {
        ok = false;
    }
    if (!CredDeleteW(reinterpret_cast<LPCWSTR>(base.utf16()), CRED_TYPE_GENERIC, 0) &&
        GetLastError() != ERROR_NOT_FOUND) ok = false;
    return ok;
}
bool nativeRequest(const QString& operation, const QString& id, QByteArray& value) {
    const QString base = target(id);
    if (operation == QLatin1String("remove")) return removeEntries(id);
    if (operation == QLatin1String("write")) {
        // Credential Manager bounds each blob. Store immutable UUID-scoped
        // chunks, then publish their count only once all writes have succeeded.
        const qsizetype chunkSize = CRED_MAX_CREDENTIAL_BLOB_SIZE;
        const qsizetype count = (value.size() + chunkSize - 1) / chunkSize;
        for (qsizetype i = 0; i < count; ++i) {
            if (!writeEntry(base + QLatin1Char('/') + QString::number(i), value.mid(i * chunkSize, chunkSize))) {
                removeEntries(id);
                return false;
            }
        }
        if (writeEntry(base, QByteArray::number(count))) return true;
        removeEntries(id);
        return false;
    }
    QByteArray header;
    if (!readEntry(base, header)) return false;
    bool valid = false;
    const int count = header.toInt(&valid);
    if (!valid || count < 1 || count > 1 + MaximumValueBytes / CRED_MAX_CREDENTIAL_BLOB_SIZE)
        return false;
    value.clear();
    for (int i = 0; i < count; ++i) {
        QByteArray chunk;
        if (!readEntry(base + QLatin1Char('/') + QString::number(i), chunk)) return false;
        value += chunk;
        if (value.size() > MaximumValueBytes) return false;
    }
    return true;
}
#elif defined(Q_OS_LINUX)
// libsecret's public C ABI uses opaque objects here. Resolve the installed
// library at runtime; a desktop without Secret Service fails closed. Schema
// construction uses secret_schema_new rather than duplicating its ABI struct.
bool nativeRequest(const QString& operation, const QString& id, QByteArray& value) {
    QLibrary secret(QStringLiteral("secret-1"), 0);
    QLibrary glib(QStringLiteral("glib-2.0"), 0);
    using SchemaNew = void* (*)(const char*, int, ...);
    using Free = void (*)(void*);
    using Store = int (*)(const void*, const char*, const char*, const char*, void*, void**, ...);
    using Lookup = char* (*)(const void*, void*, void**, ...);
    using Clear = int (*)(const void*, void*, void**, ...);
    const auto schemaNew = reinterpret_cast<SchemaNew>(secret.resolve("secret_schema_new"));
    const auto schemaFree = reinterpret_cast<Free>(secret.resolve("secret_schema_unref"));
    const auto store = reinterpret_cast<Store>(secret.resolve("secret_password_store_sync"));
    const auto lookup = reinterpret_cast<Lookup>(secret.resolve("secret_password_lookup_sync"));
    const auto clear = reinterpret_cast<Clear>(secret.resolve("secret_password_clear_sync"));
    const auto passwordFree = reinterpret_cast<Free>(secret.resolve("secret_password_free"));
    const auto errorFree = reinterpret_cast<Free>(glib.resolve("g_error_free"));
    if (!schemaNew || !schemaFree || !store || !lookup || !clear || !passwordFree || !errorFree)
        return false;
    // SECRET_SCHEMA_NONE = 0; SECRET_SCHEMA_ATTRIBUTE_STRING = 0.
    void* schema = schemaNew("org.maxchat.Credentials", 0, "id", 0, static_cast<const char*>(nullptr));
    if (!schema) return false;
    const QByteArray key = id.toUtf8();
    void* error = nullptr;
    bool ok = false;
    if (operation == QLatin1String("write")) {
        const QByteArray encoded = value.toBase64();
        ok = store(schema, nullptr, "MaxChat credentials", encoded.constData(), nullptr, &error,
                   "id", key.constData(), static_cast<const char*>(nullptr)) != 0;
    } else if (operation == QLatin1String("read")) {
        char* found = lookup(schema, nullptr, &error, "id", key.constData(), static_cast<const char*>(nullptr));
        if (found) {
            const QByteArray encoded(found);
            value = QByteArray::fromBase64(encoded);
            ok = value.size() <= MaximumValueBytes && value.toBase64() == encoded;
            passwordFree(found);
        }
    } else {
        clear(schema, nullptr, &error, "id", key.constData(), static_cast<const char*>(nullptr));
        ok = error == nullptr; // Already absent is successful cleanup.
    }
    if (error) { errorFree(error); ok = false; }
    schemaFree(schema);
    return ok;
}
#else
bool nativeRequest(const QString&, const QString&, QByteArray&) { return false; }
#endif
} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    QFile input;
    if (!input.open(stdin, QIODevice::ReadOnly)) return 1;
    const QByteArray data = input.read(256 * 1024 + 1);
    const QJsonDocument document = QJsonDocument::fromJson(data);
    const QJsonObject request = document.object();
    const QString operation = request.value(QStringLiteral("operation")).toString();
    const QString id = request.value(QStringLiteral("id")).toString();
    const QByteArray encoded = request.value(QStringLiteral("value")).toString().toLatin1();
    QByteArray value = QByteArray::fromBase64(encoded);
    bool ok = argc == 1 && data.size() <= 256 * 1024 && document.isObject() &&
              !QUuid::fromString(id).isNull() &&
              QUuid::fromString(id).toString(QUuid::WithoutBraces) == id &&
              (operation == QLatin1String("read") || operation == QLatin1String("write") ||
               operation == QLatin1String("remove")) &&
              value.size() <= MaximumValueBytes && value.toBase64() == encoded;
    if (ok) ok = nativeRequest(operation, id, value);
    QJsonObject response{{QStringLiteral("ok"), ok}};
    if (ok && operation == QLatin1String("read"))
        response.insert(QStringLiteral("value"), QString::fromLatin1(value.toBase64()));
    QFile output;
    if (!output.open(stdout, QIODevice::WriteOnly)) return 1;
    const QByteArray responseBytes = QJsonDocument(response).toJson(QJsonDocument::Compact);
    if (output.write(responseBytes) != responseBytes.size() || !output.flush()) return 1;
    return ok ? 0 : 1;
}
