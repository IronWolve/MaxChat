#pragma once
#include "scripting/ScriptPermissions.h"
#include <QHash>
#include <QElapsedTimer>
#include <QObject>
#include <QSharedPointer>
#include <QStringList>
#include <QVariantList>
#include <memory>
class QJsonObject;
class QLocalServer;
class QLocalSocket;
class QTemporaryDir;
class QTimer;
namespace maxchat::scripting {
class ScriptHost;
struct ScriptWorker;
// Supervisor only: no Lua bytecode or user script executes in the UI process.
class LuaEngine final : public QObject {
    Q_OBJECT
public:
    LuaEngine(ScriptHost* host, QString scriptsDir, QString dataRoot, QObject* parent = nullptr,
              QString workerExecutable = {});
    ~LuaEngine() override;
    [[nodiscard]] static bool available();
    bool load(const QString& path, const ScriptPermissions& permissions = {});
    int loadAll(const QHash<QString, ScriptPermissions>& permissions = {}, bool startupOnly = false);
    bool unload(const QString& name);
    bool reload(const QString& name);
    [[nodiscard]] ScriptPermissions permsForScript(const QString& name) const;
    [[nodiscard]] QStringList loaded() const;
    bool dispatch(const QString& hook, const QString& network, const QVariantList& arguments = {});
    bool dispatchToScript(const QString& script, const QString& hook, const QString& network,
                          const QVariantList& arguments = {});
    void setPermissions(const ScriptPermissions& permissions) { permissions_ = permissions; }
    [[nodiscard]] const ScriptPermissions& permissions() const { return permissions_; }
    void setCurrentNetwork(const QString& network) { currentNetwork_ = network; }
private:
    bool listen();
    void accept();
    void read(const QSharedPointer<ScriptWorker>& worker);
    bool request(const QSharedPointer<ScriptWorker>& worker, QJsonObject message);
    void hostCall(const QSharedPointer<ScriptWorker>& worker, const QJsonObject& message);
    void stop(const QSharedPointer<ScriptWorker>& worker, const QString& reason = {});
    void inspect();
    void drain(const QSharedPointer<ScriptWorker>& worker);
    void report(const QString& name, const QString& reason);
    ScriptHost* host_;
    QString scriptsDir_, dataRoot_, currentNetwork_, workerExecutable_;
    QElapsedTimer cpuClock_;
    double cpuTokens_ = 2.0;
    ScriptPermissions permissions_;
    QHash<QString, QSharedPointer<ScriptWorker>> workers_;
    std::unique_ptr<QLocalServer> server_;
    std::unique_ptr<QTemporaryDir> socketDirectory_;
    QTimer* monitor_;
    bool closing_ = false;
};
}
