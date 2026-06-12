#pragma once

#include <QString>

namespace maxchat::ui {

class AnsiRenderer final {
  public:
    [[nodiscard]] static QString toHtml(const QString& text);
    [[nodiscard]] static QString hotspot(const QString& actionId, const QString& label);
};

} // namespace maxchat::ui
