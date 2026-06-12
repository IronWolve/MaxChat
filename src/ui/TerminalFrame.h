#pragma once

#include <QChar>
#include <QString>
#include <QVector>

namespace maxchat::ui {

class TerminalFrame final {
  public:
    enum class OpType {
        Clear,
        Home,
        Position,
        Attribute,
        Write,
        Newline,
        ExtendedWrite,
    };

    struct Op {
        OpType type = OpType::Clear;
        int row = 0;
        int col = 0;
        int fg = 7;
        int bg = 0;
        QString text;
    };

    [[nodiscard]] static bool parse(const QString& ops, QVector<Op>* out,
                                    QString* error = nullptr);
};

} // namespace maxchat::ui
