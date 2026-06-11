#pragma once

#include <QString>

namespace maxchat::ui {

// A one-line mIRC-style system summary:
//   OS: … | Uptime: … | CPU: … | RAM: … (Used: … / NN%) | GPU: … | Res: WxH | Net: …
// Fields that can't be read on the current platform are omitted. Platform code
// is isolated in SystemInfo.cpp.
[[nodiscard]] QString systemInfoLine();

// Pure formatters, exposed for unit testing.
[[nodiscard]] QString formatUptime(qint64 seconds);
[[nodiscard]] QString formatGiB(quint64 bytes);

} // namespace maxchat::ui
