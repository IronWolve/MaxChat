#pragma once

#include "ui/TerminalProfile.h"

#include <QDialog>

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

    QString id_;
    TerminalProfile profile_;
    QLabel* status_ = nullptr;
    QTextBrowser* display_ = nullptr;
    QLabel* prompt_ = nullptr;
    QLineEdit* input_ = nullptr;
};

} // namespace maxchat::ui
