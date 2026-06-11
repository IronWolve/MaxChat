#include "ui/SystemInfo.h"

#include <QFile>
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QScreen>
#include <QStorageInfo>
#include <QStringList>
#include <QSysInfo>

#if defined(Q_OS_WIN)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <intrin.h>
#elif defined(Q_OS_LINUX)
#include <sys/sysinfo.h>
#endif

namespace maxchat::ui {

QString formatUptime(qint64 seconds) {
    if (seconds <= 0) {
        return {};
    }
    const qint64 days = seconds / 86400;
    const qint64 hours = (seconds % 86400) / 3600;
    const qint64 mins = (seconds % 3600) / 60;
    QStringList parts;
    if (days > 0) {
        parts << QStringLiteral("%1d").arg(days);
    }
    if (days > 0 || hours > 0) {
        parts << QStringLiteral("%1h").arg(hours);
    }
    parts << QStringLiteral("%1m").arg(mins);
    return parts.join(QLatin1Char(' '));
}

QString formatGiB(quint64 bytes) {
    const double gib = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
    return QStringLiteral("%1 GB").arg(gib, 0, 'f', 1);
}

QString formatSize(quint64 bytes) {
    const double gib = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
    if (gib >= 1024.0) {
        return QStringLiteral("%1 TB").arg(gib / 1024.0, 0, 'f', 1);
    }
    return QStringLiteral("%1 GB").arg(gib, 0, 'f', 1);
}

namespace {

QString osName() {
    QString os = QSysInfo::prettyProductName().trimmed();
    if (os.isEmpty()) {
        os = QStringLiteral("%1 %2").arg(QSysInfo::productType(), QSysInfo::productVersion()).trimmed();
    }
    if (os.isEmpty()) {
        os = QStringLiteral("unknown OS");
    }
    return QStringLiteral("%1 (%2-bit)").arg(os).arg(QSysInfo::WordSize);
}

qint64 uptimeSeconds() {
#if defined(Q_OS_WIN)
    return static_cast<qint64>(GetTickCount64() / 1000ULL);
#elif defined(Q_OS_LINUX)
    struct sysinfo info{};
    if (sysinfo(&info) == 0) {
        return static_cast<qint64>(info.uptime);
    }
    return 0;
#else
    return 0;
#endif
}

QString cpuModel() {
#if defined(Q_OS_WIN)
    int regs[4] = {0};
    char brand[0x40] = {0};
    __cpuid(regs, 0x80000000);
    const unsigned maxExt = static_cast<unsigned>(regs[0]);
    if (maxExt >= 0x80000004) {
        for (unsigned page = 0; page < 3; ++page) {
            __cpuid(regs, static_cast<int>(0x80000002 + page));
            memcpy(brand + page * 16, regs, sizeof(regs));
        }
        return QString::fromLatin1(brand).simplified();
    }
    return {};
#elif defined(Q_OS_LINUX)
    QFile f(QStringLiteral("/proc/cpuinfo"));
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString text = QString::fromUtf8(f.readAll());
        for (const QString& line : text.split(QLatin1Char('\n'))) {
            if (line.startsWith(QStringLiteral("model name"))) {
                const int colon = line.indexOf(QLatin1Char(':'));
                if (colon >= 0) {
                    return line.mid(colon + 1).simplified();
                }
            }
        }
    }
    return {};
#else
    return {};
#endif
}

// total + available physical RAM in bytes; false if unknown.
bool ramInfo(quint64& total, quint64& available) {
#if defined(Q_OS_WIN)
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) {
        total = status.ullTotalPhys;
        available = status.ullAvailPhys;
        return true;
    }
    return false;
#elif defined(Q_OS_LINUX)
    // /proc/meminfo gives MemAvailable (accounts for reclaimable cache), which is
    // a truer "free" than sysinfo's freeram.
    QFile f(QStringLiteral("/proc/meminfo"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    quint64 memTotalKb = 0;
    quint64 memAvailKb = 0;
    const QString text = QString::fromUtf8(f.readAll());
    for (const QString& line : text.split(QLatin1Char('\n'))) {
        if (line.startsWith(QStringLiteral("MemTotal:"))) {
            memTotalKb = line.section(QLatin1Char(' '), -2, -2, QString::SectionSkipEmpty).toULongLong();
        } else if (line.startsWith(QStringLiteral("MemAvailable:"))) {
            memAvailKb = line.section(QLatin1Char(' '), -2, -2, QString::SectionSkipEmpty).toULongLong();
        }
    }
    if (memTotalKb == 0) {
        return false;
    }
    total = memTotalKb * 1024ULL;
    available = memAvailKb * 1024ULL;
    return true;
#else
    Q_UNUSED(total);
    Q_UNUSED(available);
    return false;
#endif
}

QString ramText() {
    quint64 total = 0;
    quint64 available = 0;
    if (!ramInfo(total, available) || total == 0) {
        return {};
    }
    const quint64 used = total > available ? total - available : 0;
    const int pct = static_cast<int>((used * 100ULL) / total);
    return QStringLiteral("%1 (Used: %2 / %3%)").arg(formatGiB(total), formatGiB(used)).arg(pct);
}

// GPU via the OpenGL renderer string — the only portable way without per-vendor
// APIs. Returns empty if no GL context can be created.
QString gpuName() {
    if (QGuiApplication::instance() == nullptr) {
        return {};
    }
    QOffscreenSurface surface;
    surface.create();
    if (!surface.isValid()) {
        return {};
    }
    QOpenGLContext ctx;
    if (!ctx.create() || !ctx.makeCurrent(&surface)) {
        return {};
    }
    QString name;
    if (QOpenGLFunctions* fns = ctx.functions(); fns != nullptr) {
        const auto* renderer = fns->glGetString(GL_RENDERER);
        if (renderer != nullptr) {
            name = QString::fromLatin1(reinterpret_cast<const char*>(renderer));
        }
    }
    ctx.doneCurrent();
    // Trim common bus/driver suffixes ("NVIDIA … /PCIe/SSE2").
    return name.section(QLatin1Char('/'), 0, 0).trimmed();
}

QString screenRes() {
    const QScreen* screen =
        QGuiApplication::instance() != nullptr ? QGuiApplication::primaryScreen() : nullptr;
    if (screen == nullptr) {
        return {};
    }
    const QSize size = screen->geometry().size();
    if (size.isEmpty()) {
        return {};
    }
    return QStringLiteral("%1x%2").arg(size.width()).arg(size.height());
}

bool isNetworkVolume(const QStorageInfo& volume) {
#if defined(Q_OS_WIN)
    const QString winRoot =
        QString(volume.rootPath()).replace(QLatin1Char('/'), QLatin1Char('\\'));
    return GetDriveTypeW(reinterpret_cast<const wchar_t*>(winRoot.utf16())) == DRIVE_REMOTE;
#else
    static const QStringList netFs = {QStringLiteral("nfs"),    QStringLiteral("nfs4"),
                                      QStringLiteral("cifs"),   QStringLiteral("smbfs"),
                                      QStringLiteral("smb3"),   QStringLiteral("fuse.sshfs")};
    return netFs.contains(QString::fromUtf8(volume.fileSystemType()));
#endif
}

// A short label for a mounted volume: "C:" on Windows, the mount point elsewhere.
QString volumeLabel(const QStorageInfo& volume) {
    const QString root = volume.rootPath();
#if defined(Q_OS_WIN)
    if (root.size() >= 2 && root.at(1) == QLatin1Char(':')) {
        return root.left(2); // "C:/" -> "C:"
    }
#endif
    return root;
}

// Every mounted real drive with size + free; network drives tagged "(net)".
QString drivesText() {
#if !defined(Q_OS_WIN)
    // Skip Linux pseudo-filesystems so the list shows actual disks, not the dozens
    // of tmpfs/proc/snap mounts.
    static const QStringList pseudoFs = {
        QStringLiteral("tmpfs"),      QStringLiteral("devtmpfs"),  QStringLiteral("squashfs"),
        QStringLiteral("overlay"),    QStringLiteral("proc"),      QStringLiteral("sysfs"),
        QStringLiteral("cgroup"),     QStringLiteral("cgroup2"),   QStringLiteral("debugfs"),
        QStringLiteral("tracefs"),    QStringLiteral("autofs"),    QStringLiteral("mqueue"),
        QStringLiteral("hugetlbfs"),  QStringLiteral("ramfs"),     QStringLiteral("fusectl"),
        QStringLiteral("configfs"),   QStringLiteral("pstore"),    QStringLiteral("securityfs"),
        QStringLiteral("efivarfs"),   QStringLiteral("bpf"),       QStringLiteral("binfmt_misc"),
        QStringLiteral("devpts")};
#endif
    QStringList drives;
    for (const QStorageInfo& volume : QStorageInfo::mountedVolumes()) {
        if (!volume.isValid() || !volume.isReady() || volume.bytesTotal() <= 0) {
            continue;
        }
#if !defined(Q_OS_WIN)
        if (pseudoFs.contains(QString::fromUtf8(volume.fileSystemType())) ||
            volume.rootPath().startsWith(QStringLiteral("/snap"))) {
            continue;
        }
#endif
        const QString tag = isNetworkVolume(volume) ? QStringLiteral(", net") : QString();
        drives << QStringLiteral("%1 %2 (%3 free%4)")
                      .arg(volumeLabel(volume), formatSize(static_cast<quint64>(volume.bytesTotal())),
                           formatSize(static_cast<quint64>(volume.bytesAvailable())), tag);
    }
    return drives.join(QStringLiteral(", "));
}

} // namespace

QString systemInfoLine() {
    QStringList fields;
    fields << QStringLiteral("OS: %1").arg(osName());
    if (const QString up = formatUptime(uptimeSeconds()); !up.isEmpty()) {
        fields << QStringLiteral("Uptime: %1").arg(up);
    }
    if (const QString cpu = cpuModel(); !cpu.isEmpty()) {
        fields << QStringLiteral("CPU: %1").arg(cpu);
    }
    if (const QString ram = ramText(); !ram.isEmpty()) {
        fields << QStringLiteral("RAM: %1").arg(ram);
    }
    if (const QString gpu = gpuName(); !gpu.isEmpty()) {
        fields << QStringLiteral("GPU: %1").arg(gpu);
    }
    if (const QString res = screenRes(); !res.isEmpty()) {
        fields << QStringLiteral("Res: %1").arg(res);
    }
    if (const QString drives = drivesText(); !drives.isEmpty()) {
        fields << QStringLiteral("Drives: %1").arg(drives);
    }
    return QStringLiteral("[SysInfo] %1").arg(fields.join(QStringLiteral(" | ")));
}

} // namespace maxchat::ui
