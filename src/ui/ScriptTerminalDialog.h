#pragma once

#include "ui/TerminalProfile.h"

#include <QColor>
#include <QDialog>
#include <QVector>

class QLabel;
class QLineEdit;
class QTextBrowser;
class QMenuBar;
class QActionGroup;

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
    // User font override from the Fonts preferences. An empty family or a
    // pointSize <= 0 falls back to the active profile's own font/size.
    void setFontPreferences(const QString& family, int pointSize, bool bold);
    // Change the fixed grid (cols always 80; rows 25 or 40). Re-zooms the window
    // to the base font and re-fits.
    void setGridSize(int cols, int rows);
    [[nodiscard]] int gridCols() const { return profile_.cols; }
    [[nodiscard]] int gridRows() const { return profile_.rows; }

  signals:
    void inputSubmitted(const QString& id, const QString& text);
    void linkActivated(const QString& id, const QString& actionId);
    void terminalClosed(const QString& id);
    // Master close requested (File > Kill Terminal). Unlike closing the window
    // (which only hides it), this asks the owner to destroy the session.
    void killRequested(const QString& id);
    // The user changed font family/size via the Settings menu. The owner
    // persists this as the global terminal font preference.
    void fontPreferenceChanged(const QString& family, int pointSize, bool bold);
    // The user changed the grid size via the Settings menu (cols always 80).
    void gridSizeChanged(const QString& id, int cols, int rows);

  protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

  private:
    void buildMenuBar();
    [[nodiscard]] QString activeFamily() const;
    void applyProfile();
    // Largest point size for the active family that fits cols x rows into the
    // display viewport, so a fixed-grid terminal's text stretches on resize.
    [[nodiscard]] int fitFontPointForViewport() const;
    [[nodiscard]] int baseFontPoint() const;
    // Set both QFont (for metrics) and a per-widget stylesheet (font + colors).
    // The stylesheet makes the terminal authoritative over the global theme QSS,
    // which also matches QTextBrowser/QLineEdit/QLabel and would otherwise
    // override the terminal font on every theme re-polish.
    void applyWidgetStyles(int pointSize);
    void applyFitFont();
    void syncSettingsMenuChecks();

  public:
    // Size the window so the full cols x rows grid is visible at the base font.
    // Uses layout adjustSize() so it works before the window is shown.
    void sizeToGrid();

  private:
    void ensureFrameGrid();
    void renderFrameGrid();

    struct FrameCell {
        QChar ch = QLatin1Char(' ');
        int fg = 7;
        int bg = 0;
    };

    QString id_;
    TerminalProfile profile_;
    QString fontFamilyOverride_;
    int fontPointSizeOverride_ = 0;
    bool fontBoldOverride_ = false;
    QMenuBar* menuBar_ = nullptr;
    QActionGroup* fontSizeGroup_ = nullptr;
    QActionGroup* gridSizeGroup_ = nullptr;
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
