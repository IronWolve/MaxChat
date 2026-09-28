#include "scripting/LuaEngine.h"
#include "scripting/ScriptHost.h"
#include "scripting/ScriptProtocol.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#include <cmath>
#include <algorithm>
#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif
namespace maxchat::scripting {
struct ScriptWorker {
    QString name, path, network, pendingNetwork;
    ScriptPermissions permissions;
    QProcess process;
    QPointer<QLocalSocket> socket;
    QPointer<QEventLoop> waiter;
    QByteArray input;
    QList<QJsonObject> pending;
    qint64 queuedBytes = 0;
    bool drainScheduled = false;
    bool ready = false, dead = false, busy = false, done = false, result = false, active = false, inHost = false;
    int sequence = 0, activity = 0, hostSequence = 0, calls = 0;
    qint64 output = 0, totalOutput = 0, remaining = 0, hostWait = 0;
    QElapsedTimer clock, activeClock, hostClock;
    double lastCpu = 0;
    quint64 lastMemory = 0;
#ifdef Q_OS_WIN
    HANDLE job = nullptr;
    ~ScriptWorker() { if (job) CloseHandle(job); }
#endif
};
namespace {
bool launch(const QString& command) {
    const auto parts = QProcess::splitCommand(command);
    if (parts.isEmpty()) return false;
#ifdef Q_OS_WIN
    const QString program = parts.first();
    QStringList arguments;
    for (QString argument : parts.mid(1)) {
        if (argument.contains(QLatin1Char(' ')) || argument.contains(QLatin1Char('\t')) || argument.contains(QLatin1Char('"')) || argument.isEmpty()) {
            argument.replace(QLatin1Char('"'), QStringLiteral("\\\""));
            argument = QLatin1Char('"') + argument + QLatin1Char('"');
        }
        arguments.append(argument);
    }
    const QString joined = arguments.join(QLatin1Char(' '));
    return reinterpret_cast<quintptr>(ShellExecuteW(nullptr, L"open", reinterpret_cast<LPCWSTR>(program.utf16()),
        joined.isEmpty() ? nullptr : reinterpret_cast<LPCWSTR>(joined.utf16()), nullptr, SW_SHOWNORMAL)) > 32;
#else
    return QProcess::startDetached(parts.first(), parts.mid(1));
#endif
}

}
LuaEngine::LuaEngine(ScriptHost* host, QString scriptsDir, QString dataRoot, QObject* parent, QString workerExecutable)
    : QObject(parent), host_(host), scriptsDir_(std::move(scriptsDir)), dataRoot_(std::move(dataRoot)), workerExecutable_(std::move(workerExecutable)),
      server_(std::make_unique<QLocalServer>()), monitor_(new QTimer(this)) {
    connect(server_.get(), &QLocalServer::newConnection, this, &LuaEngine::accept);
    monitor_->setInterval(25);
    connect(monitor_, &QTimer::timeout, this, &LuaEngine::inspect);
    monitor_->start();
    cpuClock_.start();
}
LuaEngine::~LuaEngine() {
    closing_ = true;
    const auto workers = workers_.values();
    for (const auto& worker : workers) stop(worker);
    // No user callbacks during destruction of the host's widgets.
    server_->close();
}
bool LuaEngine::available() { return true; }
bool LuaEngine::listen() {
    if (server_->isListening()) return true;
    const QString token = QUuid::createUuid().toString(QUuid::Id128);
#ifdef Q_OS_LINUX
    server_->setSocketOptions(QLocalServer::AbstractNamespaceOption);
    return server_->listen(QStringLiteral("maxchat-lua-") + token);
#elif defined(Q_OS_WIN)
    server_->setSocketOptions(QLocalServer::UserAccessOption);
    return server_->listen(QStringLiteral("maxchat-lua-") + token);
#else
    socketDirectory_ = std::make_unique<QTemporaryDir>(QDir::temp().filePath(QStringLiteral("mc-ipc-XXXXXX")));
    if (!socketDirectory_->isValid()) return false;
    server_->setSocketOptions(QLocalServer::UserAccessOption);
    return server_->listen(socketDirectory_->filePath(QStringLiteral("s")));
#endif
}
void LuaEngine::report(const QString& name, const QString& reason) {
    if (!closing_ && host_) host_->scriptEcho(currentNetwork_, QStringLiteral("[scripts] %1: %2").arg(name, reason));
}
void LuaEngine::stop(const QSharedPointer<ScriptWorker>& worker, const QString& reason) {
    if (worker->dead) return;
    worker->dead = true; worker->done = true;
    if (workers_.value(worker->name) == worker) workers_.remove(worker->name);
    if (worker->socket) {
        worker->socket->disconnect(this); worker->socket->abort(); worker->socket->deleteLater(); worker->socket = nullptr;
    }
    worker->process.disconnect(this);
#ifdef Q_OS_WIN
    if (worker->job) { TerminateJobObject(worker->job, 80); CloseHandle(worker->job); worker->job = nullptr; }
#endif
    if (worker->process.state() != QProcess::NotRunning) {
        worker->process.terminate();
        if (!worker->process.waitForFinished(100)) { worker->process.kill(); worker->process.waitForFinished(1000); }
    }
    if (worker->waiter) worker->waiter->quit();
    if (!reason.isEmpty()) report(worker->name, reason);
}
void LuaEngine::accept() {
    while (server_->hasPendingConnections()) {
        auto* socket = server_->nextPendingConnection();
        // A host callback can destroy this engine during readyRead. Keep the
        // socket alive until Qt has finished its current notification; stop()
        // always disposes of accepted sockets with deleteLater().
        socket->setParent(nullptr);
        QSharedPointer<ScriptWorker> match;
        for (const auto& worker : workers_) {
            if (!worker->dead && !worker->socket && protocol::peerIs(socket, worker->process.processId(), true)) { match = worker; break; }
        }
        if (!match) { socket->abort(); socket->deleteLater(); continue; }
        match->socket = socket; socket->setReadBufferSize(protocol::MaximumWorkerFrame + 4);
        connect(socket, &QLocalSocket::readyRead, this, [this, weak = match.toWeakRef()] { if (auto worker = weak.toStrongRef()) read(worker); });
        connect(socket, &QLocalSocket::disconnected, this, [this, weak = match.toWeakRef()] {
            if (auto worker = weak.toStrongRef()) stop(worker, QStringLiteral("script worker disconnected or exceeded a resource limit"));
        });
        if (!protocol::send(socket, {{QStringLiteral("type"),QStringLiteral("init")},
            {QStringLiteral("scriptsDir"),scriptsDir_},{QStringLiteral("dataRoot"),dataRoot_}}))
            stop(match, QStringLiteral("could not initialize script worker"));
    }
}
void LuaEngine::read(const QSharedPointer<ScriptWorker>& worker) {
    const QPointer<LuaEngine> alive(this);
    while (alive && !worker->dead && worker->socket) {
        QJsonObject message; QString error;
        if (!protocol::receive(worker->socket, worker->input, message, error, protocol::MaximumWorkerFrame)) {
            if (!error.isEmpty()) stop(worker, error);
            return;
        }
        const QString type = message.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("ready") && !worker->ready) worker->ready = true;
        else if (type == QLatin1String("begin") && worker->ready && !worker->active &&
                 message.value(QStringLiteral("activity")).toInt() == worker->activity + 1) {
            const int command = message.value(QStringLiteral("command")).toInt(-1);
            if (command != 0 && (!worker->busy || command != worker->sequence)) {
                stop(worker,QStringLiteral("invalid script activity context")); return;
            }
            if (command) worker->network = worker->pendingNetwork;
            ++worker->activity; worker->active = true; worker->remaining = protocol::ExecutionMilliseconds;
            worker->hostWait = 0; worker->calls = 0; worker->output = 0; worker->activeClock.start();
        } else if (type == QLatin1String("end") && worker->active && !worker->inHost &&
                   message.value(QStringLiteral("activity")).toInt() == worker->activity) {
            worker->active = false; drain(worker);
        }
        else if (type == QLatin1String("result") && worker->busy && !worker->active &&
                   message.value(QStringLiteral("id")).toInt(-1) == worker->sequence && message.value(QStringLiteral("value")).isBool()) {
            worker->result = message.value(QStringLiteral("value")).toBool(); worker->done = true;
        } else if (type == QLatin1String("host") && worker->active && !worker->inHost) hostCall(worker, message);
        else { stop(worker, QStringLiteral("invalid script worker protocol")); return; }
        if (worker->waiter) worker->waiter->quit();
    }
}
void LuaEngine::inspect() {
    const QPointer<LuaEngine> alive(this);
    quint64 aggregate = 0, greatestGrowth = 0;
    double usedCpu = 0, greatestCpu = 0;
    QSharedPointer<ScriptWorker> memoryOffender, cpuOffender;
    const auto workers = workers_.values();
    for (const auto& worker : workers) {
        if (!alive) return;
        if (worker->dead) continue;
        const quint64 memory = protocol::residentBytes(worker->process.processId());
        const double cpu = protocol::cpuSeconds(worker->process.processId());
        const double delta = std::max(0.0, cpu - worker->lastCpu); worker->lastCpu = cpu;
        usedCpu += delta;
        if (delta > greatestCpu) { greatestCpu = delta; cpuOffender = worker; }
        const quint64 growth = memory > worker->lastMemory ? memory - worker->lastMemory : 0;
        if (growth > greatestGrowth) { greatestGrowth = growth; memoryOffender = worker; }
        worker->lastMemory = memory; aggregate += memory;
        if (memory > protocol::MaximumProcessBytes) stop(worker, QStringLiteral("script process memory limit exceeded"));
        else if (worker->active && !worker->inHost && worker->activeClock.elapsed() > worker->remaining)
            stop(worker, QStringLiteral("script execution time limit exceeded"));
        else if (worker->inHost && worker->hostWait + worker->hostClock.elapsed() > protocol::HostWaitMilliseconds)
            stop(worker, QStringLiteral("script host-operation time limit exceeded"));
    }
    if (!alive) return;
    const double elapsed = cpuClock_.restart() / 1000.0;
    cpuTokens_ = std::min(2.0, cpuTokens_ + elapsed * 0.50) - usedCpu;
    if (cpuTokens_ < 0 && cpuOffender) {
        cpuTokens_ = 0; stop(cpuOffender,QStringLiteral("aggregate script CPU limit exceeded"));
    }
    if (alive && aggregate > protocol::MaximumAggregateBytes && memoryOffender)
        stop(memoryOffender,QStringLiteral("aggregate script memory limit exceeded"));
}

bool LuaEngine::request(const QSharedPointer<ScriptWorker>& worker, QJsonObject message) {
    if (worker->dead || worker->busy || !worker->ready || !worker->socket) return false;
    const QPointer<LuaEngine> alive(this);
    worker->busy = true; worker->done = false; worker->result = false;
    worker->pendingNetwork = message.value(QStringLiteral("network")).toString(worker->network);
    message.insert(QStringLiteral("id"), ++worker->sequence);
    if (!protocol::send(worker->socket, message)) { stop(worker, QStringLiteral("script message exceeds size limit")); return false; }
    QElapsedTimer elapsed; elapsed.start();
    while (alive && !worker->dead && !worker->done) {
        if (elapsed.elapsed() > protocol::ExecutionMilliseconds + protocol::HostWaitMilliseconds + 1000) {
            stop(worker, QStringLiteral("script operation deadline exceeded")); break;
        }
        QEventLoop loop; worker->waiter = &loop;
        QTimer::singleShot(25, &loop, &QEventLoop::quit);
        loop.exec(QEventLoop::AllEvents);
        worker->waiter = nullptr;
    }
    worker->busy = false;
    if (alive) drain(worker);
    return alive && !worker->dead && worker->done && worker->result;
}
bool LuaEngine::load(const QString& path, const ScriptPermissions& permissions) {
    const QPointer<LuaEngine> alive(this);
    const QFileInfo info(path); const QString name = info.completeBaseName();
    if (!protocol::validScriptName(name) || !info.isFile() || info.size() > 1024 * 1024) {
        report(QStringLiteral("script"), QStringLiteral("invalid script name or source exceeds 1 MiB")); return false;
    }
    if (workers_.contains(name)) unload(name);
    if (!alive) return false;
    if (workers_.size() >= protocol::MaximumWorkers) { report(name, QStringLiteral("loaded-script limit reached")); return false; }
    if (!listen()) { report(name, QStringLiteral("could not create private script channel")); return false; }
    QString helper = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("maxchat-script-worker"));
#ifdef Q_OS_WIN
    helper += QStringLiteral(".exe");
#endif
    if (!workerExecutable_.isEmpty()) helper = workerExecutable_;
    if (!QFileInfo(helper).isExecutable()) { report(name, QStringLiteral("script worker is missing; reinstall the complete app")); return false; }
    const auto worker = QSharedPointer<ScriptWorker>::create();
    worker->name = name; worker->path = info.absoluteFilePath(); worker->network = currentNetwork_; worker->permissions = permissions;
    workers_.insert(name, worker);
    worker->process.setProgram(helper);
    worker->process.setArguments({server_->fullServerName(), QString::number(QCoreApplication::applicationPid())});
    worker->process.setStandardInputFile(QProcess::nullDevice());
#ifdef Q_OS_WIN
    worker->process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* arguments) {
        arguments->flags |= CREATE_NO_WINDOW;
    });
#endif
#ifdef Q_OS_UNIX
    worker->process.setUnixProcessParameters(QProcess::UnixProcessFlag::CreateNewSession);
#endif
    for (auto signal : {&QProcess::readyReadStandardOutput, &QProcess::readyReadStandardError}) {
        connect(&worker->process, signal, this, [this, weak = worker.toWeakRef()] {
            if (auto current = weak.toStrongRef()) {
                current->totalOutput += current->process.readAllStandardOutput().size() + current->process.readAllStandardError().size();
                if (current->totalOutput > 256 * 1024) stop(current, QStringLiteral("script stdout/stderr limit exceeded"));
            }
        });
    }
    connect(&worker->process, &QProcess::finished, this, [this, weak = worker.toWeakRef()](int, QProcess::ExitStatus) {
        if (auto current = weak.toStrongRef()) stop(current, QStringLiteral("script worker stopped after a crash or resource limit"));
    });
    worker->process.start();
    if (!worker->process.waitForStarted(5000)) { stop(worker, QStringLiteral("could not start script worker")); return false; }
#ifdef Q_OS_WIN
    worker->job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_PROCESS_MEMORY;
    limits.ProcessMemoryLimit = protocol::MaximumProcessBytes;
    HANDLE child = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(worker->process.processId()));
    const bool assigned = worker->job && child && SetInformationJobObject(worker->job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) && AssignProcessToJobObject(worker->job, child);
    if (child) CloseHandle(child);
    if (!assigned) { stop(worker, QStringLiteral("could not constrain script worker memory")); return false; }
#endif
    QElapsedTimer startup; startup.start();
    while (alive && !worker->dead && !worker->ready && startup.elapsed() < 5000) {
        QEventLoop loop; worker->waiter = &loop; QTimer::singleShot(25, &loop, &QEventLoop::quit); loop.exec(); worker->waiter = nullptr;
    }
    if (!alive) return false;
    if (!worker->ready) { stop(worker, QStringLiteral("script worker startup failed")); return false; }
    const bool ok = request(worker, {{QStringLiteral("type"),QStringLiteral("load")},{QStringLiteral("path"),worker->path},
        {QStringLiteral("permissions"),QJsonObject::fromVariantMap(permissions.toMap())},
        {QStringLiteral("allowedDirs"),QJsonArray::fromStringList(permissions.allowedDirs)}});
    if (alive && !ok) stop(worker);
    return ok;
}
int LuaEngine::loadAll(const QHash<QString, ScriptPermissions>& permissions, bool startupOnly) {
    int count = 0; const QPointer<LuaEngine> alive(this);
    const auto files = QDir(scriptsDir_).entryInfoList({QStringLiteral("*.lua")},QDir::Files,QDir::Name);
    for (const auto& file : files) {
        if (!alive) return count;
        const auto permission = permissions.value(file.completeBaseName());
        if (!file.fileName().startsWith(QLatin1Char('_')) && (!startupOnly || permission.loadAtStart) && load(file.absoluteFilePath(),permission)) ++count;
    }
    return count;
}
bool LuaEngine::unload(const QString& name) {
    const auto worker = workers_.value(name); if (!worker) return false;
    const QPointer<LuaEngine> alive(this);
    if (!worker->busy && !worker->active) request(worker, {{QStringLiteral("type"),QStringLiteral("unload")}});
    if (alive) stop(worker);
    return true;
}
bool LuaEngine::reload(const QString& name) {
    const auto worker = workers_.value(name);
    if (!worker) return false;
    const QString path = worker->path; const auto permissions = worker->permissions;
    return load(path,permissions);
}
ScriptPermissions LuaEngine::permsForScript(const QString& name) const { const auto worker = workers_.value(name); return worker ? worker->permissions : ScriptPermissions(); }
QStringList LuaEngine::loaded() const { auto result = workers_.keys(); result.sort(); return result; }
bool LuaEngine::dispatch(const QString& hook, const QString& network, const QVariantList& arguments) {
    currentNetwork_ = network; bool consumed = false; const auto workers = workers_.values(); const QPointer<LuaEngine> alive(this);
    for (const auto& worker : workers) {
        if (!alive) return consumed;
        if (dispatchToScript(worker->name,hook,network,arguments)) consumed = true;
    }
    return consumed;
}
bool LuaEngine::dispatchToScript(const QString& script, const QString& hook, const QString& network, const QVariantList& arguments) {
    const auto worker = workers_.value(script); if (!worker || worker->dead) return false;
    if (worker->busy || worker->active) {
        // Reentrant UI commands must never accidentally be sent as raw IRC while
        // the handler is busy. The user may retry; other UI events remain live.
        if (hook == QLatin1String("on_command")) { report(script, QStringLiteral("script is busy; retry the command")); return true; }
        QJsonObject queued{{QStringLiteral("type"),QStringLiteral("dispatch")},{QStringLiteral("hook"),hook},
            {QStringLiteral("network"),network},{QStringLiteral("args"),QJsonArray::fromVariantList(arguments)}};
        worker->queuedBytes += QJsonDocument(queued).toJson(QJsonDocument::Compact).size();
        if (worker->pending.size() >= 64 || worker->queuedBytes > 1024 * 1024)
            stop(worker,QStringLiteral("script event queue limit exceeded"));
        else worker->pending.append(queued);
        return false;
    }
    return request(worker, {{QStringLiteral("type"),QStringLiteral("dispatch")},{QStringLiteral("hook"),hook},
        {QStringLiteral("network"),network},{QStringLiteral("args"),QJsonArray::fromVariantList(arguments)}});
}
void LuaEngine::drain(const QSharedPointer<ScriptWorker>& worker) {
    if (worker->dead || worker->busy || worker->active || worker->pending.isEmpty() || worker->drainScheduled) return;
    worker->drainScheduled = true;
    QTimer::singleShot(0, this, [this, weak = worker.toWeakRef()] {
        const auto current = weak.toStrongRef(); if (!current) return;
        current->drainScheduled = false;
        if (current->dead || current->busy || current->active || current->pending.isEmpty()) return;
        const auto message = current->pending.takeFirst();
        current->queuedBytes -= QJsonDocument(message).toJson(QJsonDocument::Compact).size();
        request(current,message);
    });
}
void LuaEngine::hostCall(const QSharedPointer<ScriptWorker>& worker, const QJsonObject& message) {
    const auto method = message.value(QStringLiteral("method")).toString();
    const auto arguments = message.value(QStringLiteral("args")).toArray();
    const int id = message.value(QStringLiteral("id")).toInt(-1);
    const auto valid = [&](const char* pattern) {
        const QByteArray types(pattern);
        if (!message.value(QStringLiteral("args")).isArray() || arguments.size() != types.size()) return false;
        for (int i = 0; i < types.size(); ++i) {
            if (types[i] == 's' && (!arguments[i].isString() || arguments[i].toString().size() > 256 * 1024)) return false;
            if (types[i] == 'b' && !arguments[i].isBool()) return false;
            if (types[i] == 'n' && (!arguments[i].isDouble() || !std::isfinite(arguments[i].toDouble()) ||
                std::floor(arguments[i].toDouble()) != arguments[i].toDouble() || std::abs(arguments[i].toDouble()) > 100000)) return false;
        }
        return true;
    };
    QString signature;
    if (method == QLatin1String("echo") || method == QLatin1String("raw") || method == QLatin1String("insert") ||
        method == QLatin1String("nicks") || method == QLatin1String("http") || method == QLatin1String("launch") ||
        method == QLatin1String("terminal-close") || method == QLatin1String("terminal-clear") || method == QLatin1String("terminal-size")) signature = QStringLiteral("s");
    else if (method == QLatin1String("say") || method == QLatin1String("notify") || method == QLatin1String("terminal-write") ||
        method == QLatin1String("terminal-frame") || method == QLatin1String("terminal-status") || method == QLatin1String("terminal-prompt") ||
        method == QLatin1String("terminal-fit") || method == QLatin1String("terminal-hotspot")) signature = QStringLiteral("ss");
    else if (method == QLatin1String("mc")) signature = QStringLiteral("sssssb");
    else if (method == QLatin1String("terminal-open")) signature = QStringLiteral("sssnn");
    else if (method == QLatin1String("terminal-profile")) signature = QStringLiteral("ssnn");
    else if (method != QLatin1String("me") && method != QLatin1String("target") && method != QLatin1String("network") && method != QLatin1String("channels")) {
        stop(worker,QStringLiteral("unknown script host operation")); return;
    }
    if (id != worker->hostSequence + 1 || !valid(signature.toLatin1().constData()) || ++worker->calls > 128) {
        stop(worker,QStringLiteral("invalid or excessive script host calls")); return;
    }
    const bool terminalBody = method == QLatin1String("terminal-write") || method == QLatin1String("terminal-frame");
    for (int i = 0; i < arguments.size(); ++i) {
        if (!arguments[i].isString()) continue;
        int maximum = terminalBody && i == 1 ? 256 * 1024 : 8192;
        if (method == QLatin1String("notify")) maximum = i == 0 ? 256 : 4096;
        if (method == QLatin1String("terminal-open")) maximum = 512;
        if (arguments[i].toString().size() > maximum) {
            stop(worker,QStringLiteral("script argument exceeds its output limit")); return;
        }
    }
    worker->hostSequence = id;
    for (const auto& argument : arguments) if (argument.isString()) worker->output += argument.toString().size() * 2;
    if (worker->output > 1024 * 1024) { stop(worker,QStringLiteral("script output limit exceeded")); return; }
    const bool irc = method == QLatin1String("say") || method == QLatin1String("raw") || method == QLatin1String("mc");
    if ((irc && !worker->permissions.ircSend) || (method == QLatin1String("http") && !worker->permissions.network) ||
        (method == QLatin1String("launch") && !worker->permissions.runPrograms)) {
        stop(worker,QStringLiteral("script capability violation")); return;
    }
    worker->remaining -= worker->activeClock.elapsed();
    if (worker->remaining <= 0) { stop(worker,QStringLiteral("script execution time limit exceeded")); return; }
    worker->inHost = true; worker->hostClock.start();
    const QPointer<LuaEngine> alive(this);
    const auto s = [&](int index) { return arguments[index].toString(); };
    const auto n = [&](int index) { return arguments[index].toInt(); };
    const auto list = [](QStringList values) {
        if (values.size() > 4096) values = values.mid(0,4096);
        for (auto& value : values) if (value.size() > 256) value.truncate(256);
        return QJsonArray::fromStringList(values);
    };
    QJsonValue result;
    // All identities and grants come from the supervisor, never worker fields.
    if (method == QLatin1String("launch")) result = launch(s(0));
    else if (host_) {
        if (method == QLatin1String("echo")) host_->scriptEcho(worker->network,s(0));
        else if (method == QLatin1String("say")) host_->scriptSay(worker->network,s(0),s(1));
        else if (method == QLatin1String("raw")) host_->scriptSendRaw(worker->network,s(0));
        else if (method == QLatin1String("insert")) host_->scriptInsertInput(s(0));
        else if (method == QLatin1String("notify")) host_->scriptNotify(s(0),s(1));
        else if (method == QLatin1String("mc")) result = host_->scriptMcData(s(0).isEmpty() ? worker->network : s(0),s(1),s(2),s(3),s(4),arguments[5].toBool());
        else if (method == QLatin1String("terminal-open")) result = host_->scriptTerminalOpen(worker->name,s(0),s(1),s(2),n(3),n(4));
        else if (method == QLatin1String("terminal-close")) host_->scriptTerminalClose(worker->name,s(0));
        else if (method == QLatin1String("terminal-clear")) host_->scriptTerminalClear(worker->name,s(0));
        else if (method == QLatin1String("terminal-write")) host_->scriptTerminalWrite(worker->name,s(0),s(1));
        else if (method == QLatin1String("terminal-frame")) result = host_->scriptTerminalFrame(worker->name,s(0),s(1));
        else if (method == QLatin1String("terminal-status")) host_->scriptTerminalStatus(worker->name,s(0),s(1));
        else if (method == QLatin1String("terminal-prompt")) host_->scriptTerminalPrompt(worker->name,s(0),s(1));
        else if (method == QLatin1String("terminal-size")) { const auto size = host_->scriptTerminalSize(worker->name,s(0)); result = QJsonArray{size.width(),size.height()}; }
        else if (method == QLatin1String("terminal-profile")) host_->scriptTerminalProfile(worker->name,s(0),s(1),n(2),n(3));
        else if (method == QLatin1String("terminal-fit")) host_->scriptTerminalFit(worker->name,s(0),s(1));
        else if (method == QLatin1String("terminal-hotspot")) result = host_->scriptTerminalHotspot(s(0),s(1));
        else if (method == QLatin1String("me")) result = host_->scriptMe(worker->network);
        else if (method == QLatin1String("target")) result = host_->scriptTarget();
        else if (method == QLatin1String("network")) result = host_->scriptNetwork();
        else if (method == QLatin1String("channels")) result = list(host_->scriptChannels(worker->network));
        else if (method == QLatin1String("nicks")) result = list(host_->scriptNicks(worker->network,s(0)));
        else if (method == QLatin1String("http")) result = host_->scriptHttpGet(s(0), [weak = worker.toWeakRef()] {
            const auto current = weak.toStrongRef(); return !current || current->dead;
        });
    }
    if (!alive || worker->dead) return;
    worker->hostWait += worker->hostClock.elapsed(); worker->inHost = false; worker->activeClock.restart();
    if (worker->hostWait > protocol::HostWaitMilliseconds || !protocol::send(worker->socket,
        {{QStringLiteral("type"),QStringLiteral("host-result")},{QStringLiteral("id"),id},{QStringLiteral("value"),result}}))
        stop(worker,QStringLiteral("script host response exceeded its resource limit"));
}
}
