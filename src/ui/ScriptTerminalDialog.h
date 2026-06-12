#pragma once

#include "ui/TerminalProfile.h"

#include <QColor>
#include <QDialog>
#include <QVector>

class QLabel;
class QLineEdit;
class QTextBrowser;

namespace maxchat::ui {

class ScriptTerminalDialog final : public QDialog {
    Q_OBJECT

  public:
    explicit ScriptTerminalDialog(QString id, QString title, TerminalProfile profile,
                                  QWidget* parent = nullptr);

    [[nodiscard]] QString id() const { return id_; }
    [[nodiscard]] QSize terminalSize() const;
    [[nodiscard]] TerminalProfile profile() const { return profile_; }

    void writeText(const QString& text);
    void clear();
    bool applyFrame(const QString& ops, QString* error = nullptr);
    void setStatusText(const QString& text);
    void setPromptText(const QString& text);
    void setProfile(const TerminalProfile& profile);
    void setFitMode(const QString& mode);

  signals:
    void inputSubmitted(const QString& id, const QString& text);
    void linkActivated(const QString& id, const QString& actionId);
    void terminalClosed(const QString& id);

  protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

  private:
    void applyProfile();
    void updateFixedGridSize();
    void ensureFrameGrid();
    void renderFrameGrid();

    struct FrameCell {
        QChar ch = QLatin1Char(' ');
        int fg = 7;
        int bg = 0;
    };

    QString id_;
    TerminalProfile profile_;
    QLabel* status_ = nullptr;
    QTextBrowser* display_ = nullptr;
    QLabel* prompt_ = nullptr;
    QLineEdit* input_ = nullptr;
    QVector<QVector<FrameCell>> frameGrid_;
    int frameCursorRow_ = 0;
    int frameCursorCol_ = 0;
    int frameFg_ = 7;
    int frameBg_ = 0;
    bool frameMode_ = false;
};

} // namespace maxchat::ui
