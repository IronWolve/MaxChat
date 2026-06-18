#include "app/AppInfo.h"

#include "maxchat_version.h" // generated from CMake project(VERSION) — see Version.h.in

namespace maxchat::app {

QString applicationName() {
    return QStringLiteral("MaxChat");
}

QString organizationName() {
    return QStringLiteral("MaxChat Project");
}

QString version() {
    return QStringLiteral(MAXCHAT_VERSION);
}

QString displayName() {
    return QStringLiteral("MaxChat");
}

} // namespace maxchat::app
