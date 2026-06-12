#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace maxchat::scripting {

// What a loaded script is allowed to reach beyond the safe core. All default to
// off — scripts start fully sandboxed and the user opts in per capability in
// Preferences > Scripts. `allowedDirs` are absolute paths that file reads/writes
// are confined to (the script's own data dir is always allowed via api.*).
struct ScriptPermissions {
    bool readFiles = false;   // io.open in read modes, within allowedDirs
    bool writeFiles = false;  // io.open in write/append modes, within allowedDirs
    bool runPrograms = false; // os.execute / io.popen / os.remove / os.rename
    bool loadModules = false; // require/package + load/loadfile/dofile
    bool network = false;     // api.http_get
    QStringList allowedDirs;

    [[nodiscard]] bool anyFileAccess() const { return readFiles || writeFiles; }

    bool operator==(const ScriptPermissions& o) const {
        return readFiles == o.readFiles && writeFiles == o.writeFiles &&
               runPrograms == o.runPrograms && loadModules == o.loadModules &&
               network == o.network && allowedDirs == o.allowedDirs;
    }
    bool operator!=(const ScriptPermissions& o) const { return !(*this == o); }

    [[nodiscard]] QVariantMap toMap() const {
        return QVariantMap{
            {QStringLiteral("read"),    readFiles},
            {QStringLiteral("write"),   writeFiles},
            {QStringLiteral("exec"),    runPrograms},
            {QStringLiteral("modules"), loadModules},
            {QStringLiteral("network"), network},
        };
    }

    [[nodiscard]] static ScriptPermissions fromMap(const QVariantMap& m) {
        ScriptPermissions p;
        p.readFiles   = m.value(QStringLiteral("read"),    false).toBool();
        p.writeFiles  = m.value(QStringLiteral("write"),   false).toBool();
        p.runPrograms = m.value(QStringLiteral("exec"),    false).toBool();
        p.loadModules = m.value(QStringLiteral("modules"), false).toBool();
        p.network     = m.value(QStringLiteral("network"), false).toBool();
        return p;
    }
};

} // namespace maxchat::scripting
