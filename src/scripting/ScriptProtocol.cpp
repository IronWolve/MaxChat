#include "scripting/ScriptProtocol.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLocalSocket>
#include <QtEndian>
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#else
#include <sys/socket.h>
#include <unistd.h>
#ifdef Q_OS_MACOS
#include <sys/un.h>
#include <libproc.h>
#endif
#endif
namespace maxchat::scripting::protocol {
bool validScriptName(const QString& name) {
    if (name.isEmpty() || name != name.trimmed() || name.size() > 200 || name.endsWith(QLatin1Char('.')) ||
        name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\')) || name.contains(QLatin1Char(':'))) return false;
    for (const QChar ch : name) if (ch.unicode() < 32 || ch.unicode() == 127) return false;
    const QString base = name.section(QLatin1Char('.'),0,0).toUpper();
    if (base == QLatin1String("CON") || base == QLatin1String("PRN") || base == QLatin1String("AUX") ||
        base == QLatin1String("NUL") || base == QLatin1String("CONIN$") || base == QLatin1String("CONOUT$")) return false;
    if (base.size() == 4 && (base.startsWith(QLatin1String("COM")) || base.startsWith(QLatin1String("LPT"))) &&
        QStringLiteral("123456789\u00b9\u00b2\u00b3").contains(base.back())) return false;
    return true;
}
QByteArray frame(const QJsonObject& message) {
    const QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    if (payload.isEmpty() || payload.size() > MaximumFrame) return {};
    QByteArray result(4, '\0');
    qToBigEndian(quint32(payload.size()), result.data());
    result += payload;
    return result;
}
bool send(QLocalSocket* socket, const QJsonObject& message) {
    const QByteArray bytes = frame(message);
    if (!socket || bytes.isEmpty() || socket->bytesToWrite() + bytes.size() > 2 * MaximumFrame) return false;
    if (socket->write(bytes) != bytes.size()) return false;
    socket->flush();
    return socket->state() == QLocalSocket::ConnectedState;
}
bool receive(QLocalSocket* socket, QByteArray& buffer, QJsonObject& message, QString& error, quint32 maximum) {
    if (buffer.size() < 4) buffer += socket->read(4 - buffer.size());
    if (buffer.size() < 4) return false;
    const quint32 length = qFromBigEndian<quint32>(buffer.constData());
    if (!length || length > maximum) { error = QStringLiteral("invalid script frame size"); return false; }
    if (buffer.size() < qint64(length) + 4) buffer += socket->read(qint64(length) + 4 - buffer.size());
    if (buffer.size() < qint64(length) + 4) return false;
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(buffer.mid(4), &parse);
    buffer.clear();
    if (parse.error != QJsonParseError::NoError || !document.isObject()) {
        error = QStringLiteral("invalid script protocol"); return false;
    }
    message = document.object();
    return true;
}
bool peerIs(QLocalSocket* socket, qint64 processId, bool serverSide) {
    if (!socket || processId <= 0) return false;
#ifdef Q_OS_WIN
    ULONG pid = 0;
    const auto handle = reinterpret_cast<HANDLE>(socket->socketDescriptor());
    const bool ok = serverSide ? GetNamedPipeClientProcessId(handle, &pid) : GetNamedPipeServerProcessId(handle, &pid);
    return ok && qint64(pid) == processId;
#elif defined(Q_OS_LINUX)
    Q_UNUSED(serverSide);
    struct ucred credentials{}; socklen_t size = sizeof(credentials);
    return getsockopt(int(socket->socketDescriptor()), SOL_SOCKET, SO_PEERCRED, &credentials, &size) == 0 &&
        credentials.pid == processId && credentials.uid == geteuid();
#elif defined(Q_OS_MACOS)
    Q_UNUSED(serverSide);
    pid_t pid = 0; socklen_t size = sizeof(pid); uid_t uid; gid_t gid;
    return getsockopt(int(socket->socketDescriptor()), SOL_LOCAL, LOCAL_PEERPID, &pid, &size) == 0 &&
        getpeereid(int(socket->socketDescriptor()), &uid, &gid) == 0 && pid == processId && uid == geteuid();
#else
    Q_UNUSED(serverSide); return false;
#endif
}
quint64 residentBytes(qint64 processId) {
#ifdef Q_OS_WIN
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, DWORD(processId));
    if (!process) return 0;
    PROCESS_MEMORY_COUNTERS counters{};
    const bool ok = GetProcessMemoryInfo(process, &counters, sizeof(counters));
    CloseHandle(process);
    return ok ? quint64(counters.WorkingSetSize) : 0;
#elif defined(Q_OS_MACOS)
    rusage_info_v2 info{};
    return proc_pid_rusage(int(processId), RUSAGE_INFO_V2, reinterpret_cast<rusage_info_t*>(&info)) == 0 ? info.ri_resident_size : 0;
#elif defined(Q_OS_LINUX)
    QFile file(QStringLiteral("/proc/%1/statm").arg(processId));
    if (!file.open(QIODevice::ReadOnly)) return 0;
    const auto values = file.read(256).simplified().split(' ');
    return values.size() > 1 ? values[1].toULongLong() * quint64(sysconf(_SC_PAGESIZE)) : 0;
#else
    Q_UNUSED(processId); return 0;
#endif
}
double cpuSeconds(qint64 processId) {
    if (processId <= 0) return 0;
#ifdef Q_OS_WIN
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(processId));
    if (!process) return 0;
    FILETIME created, exited, kernel, user;
    const bool ok = GetProcessTimes(process, &created, &exited, &kernel, &user);
    CloseHandle(process);
    if (!ok) return 0;
    ULARGE_INTEGER k{}, u{}; k.LowPart = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
    u.LowPart = user.dwLowDateTime; u.HighPart = user.dwHighDateTime;
    return double(k.QuadPart + u.QuadPart) / 10000000.0;
#elif defined(Q_OS_MACOS)
    rusage_info_v2 info{};
    if (proc_pid_rusage(int(processId), RUSAGE_INFO_V2, reinterpret_cast<rusage_info_t*>(&info)) != 0) return 0;
    return double(info.ri_user_time + info.ri_system_time) / 1000000000.0;
#elif defined(Q_OS_LINUX)
    QFile file(QStringLiteral("/proc/%1/stat").arg(processId));
    if (!file.open(QIODevice::ReadOnly)) return 0;
    const QByteArray raw = file.read(4096);
    const auto fields = raw.mid(raw.lastIndexOf(')') + 1).simplified().split(' ');
    return fields.size() > 12 ? double(fields[11].toULongLong() + fields[12].toULongLong()) / sysconf(_SC_CLK_TCK) : 0;
#else
    return 0;
#endif
}

}
