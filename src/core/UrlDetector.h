#pragma once

#include <QStringList>

namespace maxchat::core {

[[nodiscard]] QStringList extractUrls(const QString& text);

} // namespace maxchat::core
