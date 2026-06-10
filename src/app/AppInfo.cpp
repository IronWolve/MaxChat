#include "app/AppInfo.h"

namespace maxchat::app {

QString applicationName() {
    return QStringLiteral("MaxChat");
}

QString organizationName() {
    return QStringLiteral("MaxChat Project");
}

QString version() {
    return QStringLiteral("0.1.0-dev");
}

QString displayName() {
    return QStringLiteral("MaxChat C++");
}

} // namespace maxchat::app
