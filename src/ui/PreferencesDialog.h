#pragma once

#include <QDialog>
#include <QVariantMap>

class QCheckBox;
class QComboBox;
class QFontComboBox;
class QLineEdit;
class QListWidget;
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
    void openComicSettingsRequested();
    void browseCharactersRequested();

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
    void buildScriptsTab(QWidget* tab);
    void buildServicesTab(QWidget* tab);
    void buildLocalizationTab(QWidget* tab);
    void buildDataTab(QWidget* tab);

    QVariantMap settings_;
    QCheckBox* scriptRead_ = nullptr;
    QCheckBox* scriptWrite_ = nullptr;
    QCheckBox* scriptExec_ = nullptr;
    QCheckBox* scriptModules_ = nullptr;
    QCheckBox* scriptNetwork_ = nullptr;
    QListWidget* scriptDirs_ = nullptr;
    QComboBox* theme_ = nullptr;
    QComboBox* chatTheme_ = nullptr;
    QComboBox* wallpaper_ = nullptr;
    QFontComboBox* appFontFamily_ = nullptr;
    QSpinBox* appFontSize_ = nullptr;
    QCheckBox* appFontBold_ = nullptr;
    QFontComboBox* chatFontFamily_ = nullptr;
    QSpinBox* chatFontSize_ = nullptr;
    QCheckBox* chatFontBold_ = nullptr;
    QCheckBox* showTimestamps_ = nullptr;
    QComboBox* timestampFormat_ = nullptr;
    QCheckBox* wordWrap_ = nullptr;
    QCheckBox* alignNicks_ = nullptr;
    QCheckBox* separatorLine_ = nullptr;
    QSpinBox* nickWidth_ = nullptr;
    QCheckBox* hideJoinPart_ = nullptr;
    QCheckBox* coloredNicks_ = nullptr;
    QCheckBox* showFormatting_ = nullptr;
    QCheckBox* indentWrap_ = nullptr;
    QCheckBox* markerLine_ = nullptr;
    QCheckBox* stripColorCopy_ = nullptr;
    QCheckBox* sortByStatus_ = nullptr;
    QComboBox* trayIcon_ = nullptr;
    QCheckBox* showMode_ = nullptr;
    QCheckBox* pmEcho_ = nullptr;
    QLineEdit* logMask_ = nullptr;
    QSpinBox* replayLines_ = nullptr;
    QCheckBox* inputHint_ = nullptr;
    QFontComboBox* listFontFamily_ = nullptr;
    QSpinBox* listFontSize_ = nullptr;
    QCheckBox* listFontBold_ = nullptr;
    QFontComboBox* nickFontFamily_ = nullptr;
    QSpinBox* nickFontSize_ = nullptr;
    QCheckBox* nickFontBold_ = nullptr;
    QFontComboBox* statusFontFamily_ = nullptr;
    QSpinBox* statusFontSize_ = nullptr;
    QCheckBox* statusFontBold_ = nullptr;
    QFontComboBox* topicFontFamily_ = nullptr;
    QSpinBox* topicFontSize_ = nullptr;
    QCheckBox* topicFontBold_ = nullptr;
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
    QCheckBox* pasteGuard_ = nullptr;
    QSpinBox* pasteLines_ = nullptr;
    QCheckBox* autoRejoin_ = nullptr;
    QCheckBox* ignoreInvites_ = nullptr;
    QCheckBox* inviteProtect_ = nullptr;
    QCheckBox* confirmQuit_ = nullptr;
    QCheckBox* updateCheck_ = nullptr;
    QSpinBox* scrollback_ = nullptr;
    QCheckBox* hideVersion_ = nullptr;
    QLineEdit* ctcpVersion_ = nullptr;
    QCheckBox* loggingEnabled_ = nullptr;
    QCheckBox* replayLogEnabled_ = nullptr;
    QComboBox* dccAccept_ = nullptr;
    QLineEdit* dccTrusted_ = nullptr;
    QCheckBox* dccPassive_ = nullptr;
    QLineEdit* dccIp_ = nullptr;
    QSpinBox* dccPortFirst_ = nullptr;
    QSpinBox* dccPortLast_ = nullptr;
    QLineEdit* dccDir_ = nullptr;
    QCheckBox* linkImages_ = nullptr;
    QCheckBox* linkMedia_ = nullptr;
    QCheckBox* linkXCards_ = nullptr;
    QCheckBox* linkWebCards_ = nullptr;
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
