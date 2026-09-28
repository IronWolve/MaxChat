// Private, resource-bounded Lua worker. Never use stdout as its control channel.
#include "scripting/LuaRuntime.h"
#include "scripting/ScriptHost.h"
#include "scripting/ScriptProtocol.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QLocalSocket>
#include <QTimer>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <thread>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#ifdef Q_OS_UNIX
#include <sys/resource.h>
#include <unistd.h>
#ifdef Q_OS_LINUX
#include <sys/prctl.h>
#endif
#endif
using namespace maxchat::scripting;
namespace {
[[noreturn]] void stopWorker(int code) {
#ifdef Q_OS_UNIX
    // The worker owns this session/group; native children from os.execute/popen
    // share it. Deliberately detached api.launch commands run in the parent.
    if (getpgrp() == getpid()) ::kill(-getpid(), SIGKILL);
#endif
    std::_Exit(code);
}
#ifdef Q_OS_UNIX
void terminated(int) { stopWorker(80); }
#endif
using Clock = std::chrono::steady_clock;
qint64 milliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
}
double cpuSeconds() {
#ifdef Q_OS_WIN
    FILETIME created, exited, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return 0;
    ULARGE_INTEGER k{}, u{}; k.LowPart = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
    u.LowPart = user.dwLowDateTime; u.HighPart = user.dwHighDateTime;
    return double(k.QuadPart + u.QuadPart) / 10000000.0;
#else
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
    return usage.ru_utime.tv_sec + usage.ru_stime.tv_sec + (usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1000000.0;
#endif
}
class Watchdog {
public:
    explicit Watchdog(LuaRuntime& runtime, qint64 parentPid) : thread_([this, &runtime, parentPid] {
        double tokens = 2.0;
        auto lastTime = Clock::now();
        auto lastCpu = cpuSeconds();
        while (!stopping_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            const auto now = Clock::now(); const auto cpu = cpuSeconds();
            const double elapsed = std::chrono::duration<double>(now - lastTime).count();
            // Allow bursts, but stop a flood of short timers from using a core
            // indefinitely. At most 20% average CPU per worker after its burst.
            tokens = std::min(2.0, tokens + elapsed * 0.20) - (cpu - lastCpu);
            lastTime = now; lastCpu = cpu;
            if (tokens < 0 || runtime.memoryLimitExceeded()) stopWorker(81);
            const qint64 deadline = deadline_.load();
            if (deadline && milliseconds() > deadline) stopWorker(82);
            const quint64 resident = protocol::residentBytes(QCoreApplication::applicationPid());
            if (resident > protocol::MaximumProcessBytes) stopWorker(83);
#ifdef Q_OS_UNIX
            if (getppid() != parentPid) stopWorker(84);
#endif
        }
    }) {}
    ~Watchdog() { stopping_.store(true); thread_.join(); }
    void begin() { remaining_ = protocol::ExecutionMilliseconds; waiting_ = 0; deadline_.store(milliseconds() + remaining_); }
    void end() { deadline_.store(0); }
    void pause() {
        remaining_ = deadline_.load() - milliseconds();
        if (remaining_ <= 0 || waiting_ >= protocol::HostWaitMilliseconds) stopWorker(82);
        pausedAt_ = milliseconds();
        deadline_.store(pausedAt_ + protocol::HostWaitMilliseconds - waiting_);
    }
    void resume() {
        waiting_ += milliseconds() - pausedAt_;
        if (waiting_ > protocol::HostWaitMilliseconds) stopWorker(82);
        deadline_.store(milliseconds() + remaining_);
    }
private:
    std::atomic_bool stopping_{false};
    std::atomic<qint64> deadline_{0};
    qint64 remaining_ = 0, waiting_ = 0, pausedAt_ = 0;
    std::thread thread_;
};
class RemoteHost final : public ScriptHost {
public:
    QLocalSocket* socket = nullptr;
    Watchdog* watchdog = nullptr;
    QByteArray input;
    int sequence = 0;
    QList<QJsonObject> pending;
    QJsonValue call(const QString& method, QJsonArray arguments = {}) {
        if (!watchdog) stopWorker(85);
        watchdog->pause();
        const int id = ++sequence;
        if (!protocol::send(socket, {{QStringLiteral("type"), QStringLiteral("host")},
            {QStringLiteral("id"), id}, {QStringLiteral("method"), method}, {QStringLiteral("args"), arguments}})) stopWorker(85);
        for (;;) {
            QJsonObject message; QString error;
            if (protocol::receive(socket, input, message, error)) {
                if (message.value(QStringLiteral("type")) != QJsonValue(QStringLiteral("host-result"))) {
                    if (pending.size() >= 64) stopWorker(85);
                    pending.append(message);
                    continue;
                }
                if (message.value(QStringLiteral("id")).toInt(-1) != id) stopWorker(85);
                watchdog->resume();
                return message.value(QStringLiteral("value"));
            }
            if (!error.isEmpty() || socket->state() != QLocalSocket::ConnectedState) stopWorker(85);
            socket->waitForReadyRead(50);
        }
    }
    bool scriptLaunch(const QString& command) override { return call(QStringLiteral("launch"), {command}).toBool(); }
    void scriptEcho(const QString&, const QString& text) override { call(QStringLiteral("echo"), {text}); }
    void scriptSay(const QString&, const QString& target, const QString& text) override { call(QStringLiteral("say"), {target,text}); }
    void scriptSendRaw(const QString&, const QString& text) override { call(QStringLiteral("raw"), {text}); }
    void scriptInsertInput(const QString& text) override { call(QStringLiteral("insert"), {text}); }
    void scriptNotify(const QString& title, const QString& text) override { call(QStringLiteral("notify"), {title,text}); }
    bool scriptMcData(const QString& network, const QString& target, const QString& service,
                      const QString& verb, const QString& payload, bool notice) override {
        return call(QStringLiteral("mc"), {network,target,service,verb,payload,notice}).toBool();
    }
    bool scriptTerminalOpen(const QString&, const QString& id, const QString& title,
                            const QString& profile, int cols, int rows) override {
        return call(QStringLiteral("terminal-open"), {id,title,profile,cols,rows}).toBool();
    }
    void scriptTerminalClose(const QString&, const QString& id) override { call(QStringLiteral("terminal-close"), {id}); }
    void scriptTerminalClear(const QString&, const QString& id) override { call(QStringLiteral("terminal-clear"), {id}); }
    void scriptTerminalWrite(const QString&, const QString& id, const QString& text) override { call(QStringLiteral("terminal-write"), {id,text}); }
    bool scriptTerminalFrame(const QString&, const QString& id, const QString& text) override { return call(QStringLiteral("terminal-frame"), {id,text}).toBool(); }
    void scriptTerminalStatus(const QString&, const QString& id, const QString& text) override { call(QStringLiteral("terminal-status"), {id,text}); }
    void scriptTerminalPrompt(const QString&, const QString& id, const QString& text) override { call(QStringLiteral("terminal-prompt"), {id,text}); }
    QSize scriptTerminalSize(const QString&, const QString& id) override {
        const auto value = call(QStringLiteral("terminal-size"), {id}).toArray();
        return value.size() == 2 ? QSize(value[0].toInt(),value[1].toInt()) : QSize();
    }
    void scriptTerminalProfile(const QString&, const QString& id, const QString& profile, int cols, int rows) override {
        call(QStringLiteral("terminal-profile"), {id,profile,cols,rows});
    }
    void scriptTerminalFit(const QString&, const QString& id, const QString& mode) override { call(QStringLiteral("terminal-fit"), {id,mode}); }
    QString scriptTerminalHotspot(const QString& id, const QString& label) override { return call(QStringLiteral("terminal-hotspot"), {id,label}).toString(); }
    QString scriptMe(const QString&) override { return call(QStringLiteral("me")).toString(); }
    QString scriptTarget() override { return call(QStringLiteral("target")).toString(); }
    QString scriptNetwork() override { return call(QStringLiteral("network")).toString(); }
    QStringList stringList(const QJsonValue& value) {
        QStringList result; for (const auto& item : value.toArray()) result.append(item.toString()); return result;
    }
    QStringList scriptChannels(const QString&) override { return stringList(call(QStringLiteral("channels"))); }
    QStringList scriptNicks(const QString&, const QString& target) override { return stringList(call(QStringLiteral("nicks"), {target})); }
    QString scriptHttpGet(const QString& url) override { return call(QStringLiteral("http"), {url}).toString(); }
};
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc != 3) return 2;
    bool valid = false;
    const qint64 parentPid = QString::fromLocal8Bit(argv[2]).toLongLong(&valid);
    if (!valid || parentPid <= 0) return 2;
#ifdef Q_OS_UNIX
    if (getpgrp() != getpid() && setsid() == -1) return 2;
    struct sigaction action{}; action.sa_handler = terminated; sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, nullptr); sigaction(SIGINT, &action, nullptr);
    const rlimit noCore{0,0}; setrlimit(RLIMIT_CORE, &noCore);
#ifdef Q_OS_LINUX
    prctl(PR_SET_PDEATHSIG, SIGTERM);
    // Preserve Qt/ASan's already-reserved mappings, bounding additional address
    // space. The Lua allocator and RSS watchdog provide tighter live limits.
    QFile stat(QStringLiteral("/proc/self/statm"));
    if (stat.open(QIODevice::ReadOnly)) {
        const quint64 baseline = stat.read(256).split(' ').value(0).toULongLong() * quint64(sysconf(_SC_PAGESIZE));
        if (baseline) { const rlimit limit{baseline + protocol::MaximumProcessBytes, baseline + protocol::MaximumProcessBytes}; setrlimit(RLIMIT_AS, &limit); }
    }
#endif
#endif
    QLocalSocket socket;
#ifdef Q_OS_LINUX
    socket.setSocketOptions(QLocalSocket::AbstractNamespaceOption);
#endif
    socket.setReadBufferSize(protocol::MaximumFrame + 4);
    socket.connectToServer(QString::fromLocal8Bit(argv[1]));
    if (!socket.waitForConnected(5000) || !protocol::peerIs(&socket, parentPid, false)) return 3;
    RemoteHost host; host.socket = &socket;
    // The parent's init message supplies directories and permissions. No source
    // checkout, profile discovery or untrusted command-line script arguments.
    QByteArray input; QJsonObject init; QString error;
    while (!protocol::receive(&socket, input, init, error)) {
        if (!error.isEmpty() || !socket.waitForReadyRead(5000)) return 3;
    }
    if (init.value(QStringLiteral("type")) != QJsonValue(QStringLiteral("init"))) return 3;
    const QString scriptsDir = init.value(QStringLiteral("scriptsDir")).toString();
    const QString dataRoot = init.value(QStringLiteral("dataRoot")).toString();
    LuaRuntime runtime(&host, scriptsDir, dataRoot);
    Watchdog watchdog(runtime, parentPid); host.watchdog = &watchdog;
    int activity = 0;
    int commandId = 0;
    bool busy = false;
    std::function<void()> receive;
    runtime.setActivityCallback([&](bool active) {
        if (active) { busy = true; watchdog.begin(); ++activity; }
        if (!protocol::send(&socket, {{QStringLiteral("type"), active ? QStringLiteral("begin") : QStringLiteral("end")},
                                   {QStringLiteral("activity"), activity},{QStringLiteral("command"),commandId}})) stopWorker(85);
        if (!active) { watchdog.end(); busy = false; if (receive) QTimer::singleShot(0, &app, receive); }
    });
    receive = [&]() {
        if (busy) return;
        QJsonObject message; QString error;
        for (;;) {
            if (!host.pending.isEmpty()) message = host.pending.takeFirst();
            else if (!protocol::receive(&socket, host.input, message, error)) break;
            busy = true;
            const auto type = message.value(QStringLiteral("type")).toString();
            const int id = message.value(QStringLiteral("id")).toInt(-1);
            bool result = false;
            if (id < 1) stopWorker(85);
            commandId = id;
            if (type == QLatin1String("load") && runtime.loaded().isEmpty()) {
                ScriptPermissions permissions = ScriptPermissions::fromMap(message.value(QStringLiteral("permissions")).toObject().toVariantMap());
                for (const auto& path : message.value(QStringLiteral("allowedDirs")).toArray()) permissions.allowedDirs.append(path.toString());
                result = runtime.load(message.value(QStringLiteral("path")).toString(), permissions);
            } else if (type == QLatin1String("dispatch")) {
                result = runtime.dispatch(message.value(QStringLiteral("hook")).toString(),
                    message.value(QStringLiteral("network")).toString(), message.value(QStringLiteral("args")).toArray().toVariantList());
            } else if (type == QLatin1String("unload")) {
                const auto names = runtime.loaded();
                result = names.size() == 1 && runtime.unload(names.front());
            } else stopWorker(85);
            if (!protocol::send(&socket, {{QStringLiteral("type"), QStringLiteral("result")},
                {QStringLiteral("id"),id},{QStringLiteral("value"),result}})) stopWorker(85);
            commandId = 0; busy = false;
        }
        if (!error.isEmpty()) stopWorker(85);
    };
    QObject::connect(&socket, &QLocalSocket::readyRead, &app, receive);
    QObject::connect(&socket, &QLocalSocket::disconnected, &app, [] { stopWorker(84); });
    if (!protocol::send(&socket, {{QStringLiteral("type"),QStringLiteral("ready")}})) return 3;
    QTimer::singleShot(0, &app, receive);
    app.exec();
    stopWorker(0);
}
