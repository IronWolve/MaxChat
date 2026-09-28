#include "core/SecretStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

namespace maxchat::core {
namespace {

class PlatformSecretStore final : public SecretStore {
public:
    bool read(const QString& id, QByteArray& value, QString& error) override {
        QJsonObject reply;
        if (!request(QStringLiteral("read"), id, {}, reply, error)) return false;
        value = QByteArray::fromBase64(reply.value(QStringLiteral("value")).toString().toLatin1());
        return true;
    }
    bool write(const QString& id, const QByteArray& value, QString& error) override {
        QJsonObject reply;
        return request(QStringLiteral("write"), id, value, reply, error);
    }
    bool remove(const QString& id) override {
        QJsonObject reply;
        QString error;
        return request(QStringLiteral("remove"), id, {}, reply, error);
    }
private:
    bool request(const QString& operation, const QString& id, const QByteArray& value,
                 QJsonObject& reply, QString& error) {
        QString helper = QDir(QCoreApplication::applicationDirPath())
                             .filePath(QStringLiteral("maxchat-secrets"));
#ifdef Q_OS_WIN
        helper += QStringLiteral(".exe");
#endif
        QProcess process;
        process.setProgram(helper);
        process.start();
        if (!process.waitForStarted(1000)) {
            error = QStringLiteral("The credential-storage helper is missing or could not start. Reinstall the complete application.");
            return false;
        }
        const QByteArray input = QJsonDocument(QJsonObject{
            {QStringLiteral("operation"), operation}, {QStringLiteral("id"), id},
            {QStringLiteral("value"), QString::fromLatin1(value.toBase64())}}).toJson(QJsonDocument::Compact);
        process.write(input);
        process.closeWriteChannel();
        // A locked/missing desktop service must not hang a settings operation
        // indefinitely. The separate helper owns all blocking native calls.
        if (!process.waitForFinished(5000)) {
            process.kill();
            process.waitForFinished(1000);
            error = QStringLiteral("Credential storage did not respond. Unlock the OS keychain and try again; existing settings were not changed.");
            return false;
        }
        const QJsonDocument document = QJsonDocument::fromJson(process.readAllStandardOutput());
        reply = document.object();
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 ||
            !document.isObject() || !reply.value(QStringLiteral("ok")).toBool()) {
            // Never include a native backend's output: it may contain data.
            error = QStringLiteral("OS credential storage is unavailable. Unlock the keychain and try again; passwords will not be saved as plaintext.");
            return false;
        }
        return true;
    }
};

} // namespace

std::shared_ptr<SecretStore> platformSecretStore() {
    return std::make_shared<PlatformSecretStore>();
}
} // namespace maxchat::core
