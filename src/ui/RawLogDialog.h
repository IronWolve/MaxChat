#pragma once

#include <QDialog>
#include <QStringList>

class QPlainTextEdit;

namespace maxchat::ui {

class RawLogDialog final : public QDialog {
    Q_OBJECT

  public:
    explicit RawLogDialog(QWidget* parent = nullptr);

    void setLines(const QStringList& lines);
    void appendLine(const QString& line);
    void clearLog();

    [[nodiscard]] QString rawText() const;

  signals:
    void clearRequested();

  private:
    QPlainTextEdit* m_logView = nullptr;
};

} // namespace maxchat::ui
