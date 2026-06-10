#pragma once

#include <QDialog>
#include <QVariantMap>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;

namespace maxchat::ui {

class ColorPick;

class PreferencesDialog final : public QDialog {
    Q_OBJECT

  public:
    explicit PreferencesDialog(QVariantMap settings, QWidget* parent = nullptr);

    [[nodiscard]] QVariantMap settings() const;

  signals:
    void exportSettingsRequested();
    void importSettingsRequested();
    void resetServerListRequested();
    void resetAllSettingsRequested();
    void testNotificationRequested();

  private:
    void setAllFonts(const QString& family, int size, bool bold);
    void refillThemeCombo(QComboBox* combo, bool chat, const QString& selectId);
    void buildAppearanceTab(QWidget* tab);
    void buildThemesTab(QWidget* tab);
    void buildMessagesTab(QWidget* tab);
    void buildNotificationsTab(QWidget* tab);
    void buildLayoutTab(QWidget* tab);
    void buildProtectionTab(QWidget* tab);
    void buildFilesTab(QWidget* tab);
    void buildFontsTab(QWidget* tab);
    void buildComicTab(QWidget* tab);
    void buildServicesTab(QWidget* tab);
    void buildLocalizationTab(QWidget* tab);
    void buildDataTab(QWidget* tab);

    QVariantMap settings_;
    QComboBox* theme_ = nullptr;
    QComboBox* chatTheme_ = nullptr;
    QComboBox* wallpaper_ = nullptr;
    QLineEdit* appFontFamily_ = nullptr;
    QSpinBox* appFontSize_ = nullptr;
    QCheckBox* appFontBold_ = nullptr;
    QLineEdit* chatFontFamily_ = nullptr;
    QSpinBox* chatFontSize_ = nullptr;
    QCheckBox* chatFontBold_ = nullptr;
    QCheckBox* showTimestamps_ = nullptr;
    QLineEdit* timestampFormat_ = nullptr;
    QCheckBox* wordWrap_ = nullptr;
    QCheckBox* alignNicks_ = nullptr;
    QCheckBox* separatorLine_ = nullptr;
    QSpinBox* nickWidth_ = nullptr;
    QCheckBox* hideJoinPart_ = nullptr;
    QCheckBox* coloredNicks_ = nullptr;
    QCheckBox* showFormatting_ = nullptr;
    QCheckBox* indentWrap_ = nullptr;
    QCheckBox* markerLine_ = nullptr;
    QCheckBox* showMode_ = nullptr;
    QCheckBox* pmEcho_ = nullptr;
    QLineEdit* logMask_ = nullptr;
    QSpinBox* replayLines_ = nullptr;
    QCheckBox* inputHint_ = nullptr;
    QLineEdit* listFontFamily_ = nullptr;
    QSpinBox* listFontSize_ = nullptr;
    QCheckBox* listFontBold_ = nullptr;
    ColorPick* chatTextColor_ = nullptr;
    ColorPick* eventColor_ = nullptr;
    ColorPick* treeColor_ = nullptr;
    ColorPick* userlistColor_ = nullptr;
    ColorPick* nickLabelColor_ = nullptr;
    ColorPick* statusColor_ = nullptr;
    ColorPick* topicColor_ = nullptr;
    QCheckBox* serverListVisible_ = nullptr;
    QCheckBox* memberListVisible_ = nullptr;
    QCheckBox* buttonBarVisible_ = nullptr;
    QCheckBox* buttonsAsTabs_ = nullptr;
    QCheckBox* connectOnStart_ = nullptr;
    QCheckBox* autoReconnect_ = nullptr;
    QCheckBox* floodProtect_ = nullptr;
    QSpinBox* floodMessages_ = nullptr;
    QSpinBox* floodSeconds_ = nullptr;
    QCheckBox* loggingEnabled_ = nullptr;
    QCheckBox* replayLogEnabled_ = nullptr;
    QCheckBox* linkImages_ = nullptr;
    QCheckBox* linkMedia_ = nullptr;
    QCheckBox* linkXCards_ = nullptr;
    QCheckBox* linkWebCards_ = nullptr;
    QComboBox* trayIcon_ = nullptr;
    QCheckBox* dnd_ = nullptr;
    QComboBox* notifyPopup_ = nullptr;
    QCheckBox* notifyPm_ = nullptr;
    QCheckBox* notifyHighlight_ = nullptr;
    QLineEdit* highlightWords_ = nullptr;
    QCheckBox* notifyFlash_ = nullptr;
    QComboBox* notifyCorner_ = nullptr;
    QSpinBox* notifyDuration_ = nullptr;
    QComboBox* notifyTheme_ = nullptr;
    QCheckBox* beepHighlight_ = nullptr;
    QCheckBox* notifySound_ = nullptr;
    QCheckBox* ctcpSound_ = nullptr;
    QCheckBox* minimizeToTray_ = nullptr;
    QComboBox* interfaceLanguage_ = nullptr;
    QCheckBox* spellcheckEnabled_ = nullptr;
    QComboBox* spellLanguage_ = nullptr;
};

} // namespace maxchat::ui
