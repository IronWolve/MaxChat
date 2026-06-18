#pragma once

#include <QString>

namespace maxchat::ui {

// A one-line IRC-style system summary:
//   OS: … | Uptime: … | CPU: … | RAM: … (Used: … / NN%) | GPU: … | Res: WxH | Net: …
// Fields that can't be read on the current platform are omitted. Platform code
// is isolated in SystemInfo.cpp.
[[nodiscard]] QString systemInfoLine();

// Pure formatters, exposed for unit testing.
[[nodiscard]] QString formatUptime(qint64 seconds);
[[nodiscard]] QString formatGiB(quint64 bytes);   // always GB (used for RAM)
[[nodiscard]] QString formatSize(quint64 bytes);  // GB, or TB for large drives

} // namespace maxchat::ui
