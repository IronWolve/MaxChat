#include "ui/TerminalFrame.h"

#include <QByteArray>

namespace maxchat::ui {

namespace {

bool hexValue(const QChar ch, int* out) {
    const ushort u = ch.unicode();
    if (u >= '0' && u <= '9') {
        *out = static_cast<int>(u - '0');
        return true;
    }
    if (u >= 'a' && u <= 'f') {
        *out = static_cast<int>(10 + u - 'a');
        return true;
    }
    if (u >= 'A' && u <= 'F') {
        *out = static_cast<int>(10 + u - 'A');
        return true;
    }
    return false;
}

bool hexByte(const QString& s, qsizetype at, int* out) {
    if (at + 1 >= s.size()) {
        return false;
    }
    int hi = 0;
    int lo = 0;
    if (!hexValue(s.at(at), &hi) || !hexValue(s.at(at + 1), &lo)) {
        return false;
    }
    *out = (hi << 4) | lo;
    return true;
}

bool hexWord(const QString& s, qsizetype at, int* out) {
    int b1 = 0;
    int b2 = 0;
    if (!hexByte(s, at, &b1) || !hexByte(s, at + 2, &b2)) {
        return false;
    }
    *out = (b1 << 8) | b2;
    return true;
}

void setError(QString* error, const QString& text) {
    if (error != nullptr) {
        *error = text;
    }
}

bool readUtf8Text(const QString& ops, qsizetype* pos, const int byteLength, QString* out,
                  QString* error) {
    if (byteLength < 0) {
        setError(error, QStringLiteral("negative text length"));
        return false;
    }
    QByteArray bytes;
    while (*pos < ops.size() && bytes.size() < byteLength) {
        // Consume a whole codepoint per step: a non-BMP char is two QChars (a
        // surrogate pair) and 4 UTF-8 bytes. Encoding one QChar at a time turns
        // a lone high surrogate into U+FFFD (3 bytes) and desyncs the byte count.
        qsizetype units = 1;
        if (ops.at(*pos).isHighSurrogate() && *pos + 1 < ops.size() &&
            ops.at(*pos + 1).isLowSurrogate()) {
            units = 2;
        }
        const QByteArray next = ops.mid(*pos, units).toUtf8();
        if (bytes.size() + next.size() > byteLength) {
            setError(error, QStringLiteral("text length splits a UTF-8 character"));
            return false;
        }
        bytes.append(next);
        *pos += units;
    }
    if (bytes.size() != byteLength) {
        setError(error, QStringLiteral("truncated text payload"));
        return false;
    }
    *out = QString::fromUtf8(bytes);
    return true;
}

} // namespace

bool TerminalFrame::parse(const QString& ops, QVector<Op>* out, QString* error) {
    if (out == nullptr) {
        setError(error, QStringLiteral("missing output vector"));
        return false;
    }
    // Bound the work a single frame can demand: a legitimate full 80x50 redraw
    // (incl. MCB1 bitmap art) stays well under these, but a script calling
    // api.terminal_frame with a giant op stream must not be able to spin the
    // parser/grid. Caps are generous, not a protocol limit.
    constexpr qsizetype kMaxOpsBytes = 256 * 1024;
    constexpr int kMaxOps = 20000;
    if (ops.size() > kMaxOpsBytes) {
        setError(error, QStringLiteral("frame op stream too large"));
        return false;
    }
    out->clear();
    qsizetype pos = 0;
    while (pos < ops.size()) {
        if (out->size() >= kMaxOps) {
            setError(error, QStringLiteral("too many frame ops"));
            return false;
        }
        const QChar opcode = ops.at(pos++);
        Op op;
        switch (opcode.unicode()) {
        case 'C':
            op.type = OpType::Clear;
            out->append(op);
            break;
        case 'H':
            op.type = OpType::Home;
            out->append(op);
            break;
        case 'P': {
            int row = 0;
            int col = 0;
            if (!hexByte(ops, pos, &row) || !hexByte(ops, pos + 2, &col)) {
                setError(error, QStringLiteral("bad position op"));
                return false;
            }
            pos += 4;
            op.type = OpType::Position;
            op.row = row;
            op.col = col;
            out->append(op);
            break;
        }
        case 'A': {
            int fg = 0;
            int bg = 0;
            if (pos + 1 >= ops.size() || !hexValue(ops.at(pos), &fg) ||
                !hexValue(ops.at(pos + 1), &bg)) {
                setError(error, QStringLiteral("bad attribute op"));
                return false;
            }
            pos += 2;
            op.type = OpType::Attribute;
            op.fg = fg;
            op.bg = bg;
            out->append(op);
            break;
        }
        case 'W': {
            int length = 0;
            if (!hexByte(ops, pos, &length)) {
                setError(error, QStringLiteral("bad write length"));
                return false;
            }
            pos += 2;
            op.type = OpType::Write;
            if (!readUtf8Text(ops, &pos, length, &op.text, error)) {
                return false;
            }
            out->append(op);
            break;
        }
        case 'N':
            op.type = OpType::Newline;
            out->append(op);
            break;
        case 'X': {
            int length = 0;
            if (!hexWord(ops, pos, &length)) {
                setError(error, QStringLiteral("bad extended write length"));
                return false;
            }
            pos += 4;
            op.type = OpType::ExtendedWrite;
            if (!readUtf8Text(ops, &pos, length, &op.text, error)) {
                return false;
            }
            out->append(op);
            break;
        }
        default:
            setError(error, QStringLiteral("unknown op '%1'").arg(opcode));
            return false;
        }
    }
    return true;
}

} // namespace maxchat::ui
