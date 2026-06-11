#include "ui/PreferencesDialog.h"

#include "core/SettingsStore.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFontComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QtTest/QtTest>

using maxchat::ui::PreferencesDialog;

class PreferencesDialogTest final : public QObject {
    Q_OBJECT

  private slots:
    void navigationUsesLeftSideButtonsInsteadOfTopTabs() {
        PreferencesDialog dialog(maxchat::core::SettingsStore::defaultSettings());

        auto* buttons = dialog.findChild<QListWidget*>(QStringLiteral("preferencesButtons"));
        auto* pages = dialog.findChild<QStackedWidget*>(QStringLiteral("preferencesPages"));
        QVERIFY(buttons != nullptr);
        QVERIFY(pages != nullptr);
        QVERIFY(dialog.findChild<QTabWidget*>() == nullptr);
        QCOMPARE(buttons->count(), 15);
        QCOMPARE(pages->count(), 15);
        QCOMPARE(buttons->item(0)->text(), QStringLiteral("Appearance"));
        QCOMPARE(buttons->item(3)->text(), QStringLiteral("Notifications"));
        QCOMPARE(buttons->item(6)->text(), QStringLiteral("Themes"));
        QCOMPARE(buttons->item(7)->text(), QStringLiteral("Fonts"));
        QCOMPARE(buttons->item(8)->text(), QStringLiteral("Spelling"));
        QCOMPARE(buttons->item(9)->text(), QStringLiteral("Localization"));
        QCOMPARE(buttons->item(10)->text(), QStringLiteral("Comic"));
        QCOMPARE(buttons->item(11)->text(), QStringLiteral("Scripts"));
        QCOMPARE(buttons->item(13)->text(), QStringLiteral("Image Hosting"));
        QCOMPARE(buttons->item(14)->text(), QStringLiteral("Data"));
        QCOMPARE(buttons->currentRow(), 0);
        QCOMPARE(pages->currentIndex(), 0);

        buttons->setCurrentRow(5);
        QCOMPARE(pages->currentIndex(), 5);
    }

    void writesEditedSettingsAndContentServices() {
        QVariantMap settings = maxchat::core::SettingsStore::defaultSettings();
        PreferencesDialog dialog(settings);

        auto* theme = dialog.findChild<QComboBox*>(QStringLiteral("theme"));
        auto* chatTheme = dialog.findChild<QComboBox*>(QStringLiteral("chatTheme"));
        auto* wallpaper = dialog.findChild<QComboBox*>(QStringLiteral("wallpaper"));
        auto* timestampFormat = dialog.findChild<QComboBox*>(QStringLiteral("timestampFormat"));
        auto* spellcheck = dialog.findChild<QCheckBox*>(QStringLiteral("spellcheckEnabled"));
        auto* floodProtect = dialog.findChild<QCheckBox*>(QStringLiteral("floodProtect"));
        auto* autoReconnect = dialog.findChild<QCheckBox*>(QStringLiteral("autoReconnect"));
        auto* floodMessages = dialog.findChild<QSpinBox*>(QStringLiteral("floodMessages"));
        auto* floodSeconds = dialog.findChild<QSpinBox*>(QStringLiteral("floodSeconds"));
        auto* hideJoinPart = dialog.findChild<QCheckBox*>(QStringLiteral("hideJoinPart"));
        auto* separatorLine = dialog.findChild<QCheckBox*>(QStringLiteral("separatorLine"));
        auto* nickWidth = dialog.findChild<QSpinBox*>(QStringLiteral("nickWidth"));
        auto* serverListVisible = dialog.findChild<QCheckBox*>(QStringLiteral("serverListVisible"));
        auto* memberListVisible = dialog.findChild<QCheckBox*>(QStringLiteral("memberListVisible"));
        auto* buttonBarVisible = dialog.findChild<QCheckBox*>(QStringLiteral("buttonBarVisible"));
        auto* buttonsAsTabs = dialog.findChild<QCheckBox*>(QStringLiteral("buttonsAsTabs"));
        auto* connectOnStart = dialog.findChild<QCheckBox*>(QStringLiteral("connectOnStart"));
        auto* coloredNicks = dialog.findChild<QCheckBox*>(QStringLiteral("coloredNicks"));
        auto* showFormatting = dialog.findChild<QCheckBox*>(QStringLiteral("showFormatting"));
        auto* logging = dialog.findChild<QCheckBox*>(QStringLiteral("loggingEnabled"));
        auto* replay = dialog.findChild<QCheckBox*>(QStringLiteral("replayLogEnabled"));
        auto* images = dialog.findChild<QCheckBox*>(QStringLiteral("linkImages"));
        auto* media = dialog.findChild<QCheckBox*>(QStringLiteral("linkMedia"));
        auto* webcards = dialog.findChild<QCheckBox*>(QStringLiteral("linkWebCards"));
        QVERIFY(theme != nullptr);
        QVERIFY(chatTheme != nullptr);
        QVERIFY(wallpaper != nullptr);
        QVERIFY(theme->findData(QStringLiteral("vaporwave")) >= 0);
        QVERIFY(theme->findData(QStringLiteral("system")) >= 0);
        QVERIFY(chatTheme->findData(QStringLiteral("green")) >= 0);
        QVERIFY(wallpaper->findData(QStringLiteral("synthwave.png")) >= 0);
        QVERIFY(timestampFormat != nullptr);
        QVERIFY(spellcheck != nullptr);
        QVERIFY(floodProtect != nullptr);
        QVERIFY(autoReconnect != nullptr);
        QVERIFY(floodMessages != nullptr);
        QVERIFY(floodSeconds != nullptr);
        QVERIFY(hideJoinPart != nullptr);
        QVERIFY(separatorLine != nullptr);
        QVERIFY(nickWidth != nullptr);
        QVERIFY(serverListVisible != nullptr);
        QVERIFY(memberListVisible != nullptr);
        QVERIFY(buttonBarVisible != nullptr);
        QVERIFY(buttonsAsTabs != nullptr);
        QVERIFY(connectOnStart != nullptr);
        QVERIFY(coloredNicks != nullptr);
        QVERIFY(showFormatting != nullptr);
        QVERIFY(logging != nullptr);
        QVERIFY(replay != nullptr);
        QVERIFY(images != nullptr);
        QVERIFY(media != nullptr);
        QVERIFY(webcards != nullptr);

        theme->setCurrentIndex(theme->findData(QStringLiteral("system")));
        chatTheme->setCurrentIndex(chatTheme->findData(QStringLiteral("green")));
        wallpaper->setCurrentIndex(wallpaper->findData(QStringLiteral("synthwave.png")));
        timestampFormat->setEditText(QStringLiteral("%H:%M"));
        spellcheck->setChecked(false);
        autoReconnect->setChecked(false);
        floodProtect->setChecked(true);
        floodMessages->setValue(7);
        floodSeconds->setValue(3);
        hideJoinPart->setChecked(true);
        separatorLine->setChecked(false);
        nickWidth->setValue(22);
        serverListVisible->setChecked(false);
        memberListVisible->setChecked(false);
        buttonBarVisible->setChecked(false);
        buttonsAsTabs->setChecked(true);
        connectOnStart->setChecked(true);
        coloredNicks->setChecked(false);
        showFormatting->setChecked(false);
        logging->setChecked(false);
        replay->setChecked(false);
        images->setChecked(false);
        media->setChecked(false);
        webcards->setChecked(false);

        const QVariantMap values = dialog.settings();
        QCOMPARE(values.value(QStringLiteral("theme")).toString(), QStringLiteral("system"));
        QCOMPARE(values.value(QStringLiteral("chat_theme")).toString(), QStringLiteral("green"));
        QCOMPARE(values.value(QStringLiteral("wallpaper")).toString(),
                 QStringLiteral("synthwave.png"));
        QCOMPARE(values.value(QStringLiteral("timestamp_format")).toString(),
                 QStringLiteral("%H:%M"));
        QCOMPARE(values.value(QStringLiteral("spellcheck_enabled")).toBool(), false);
        QCOMPARE(values.value(QStringLiteral("auto_reconnect")).toBool(), false);
        QCOMPARE(values.value(QStringLiteral("flood_protect")).toBool(), true);
        QCOMPARE(values.value(QStringLiteral("flood_msgs")).toInt(), 7);
        QCOMPARE(values.value(QStringLiteral("flood_secs")).toInt(), 3);
        QCOMPARE(values.value(QStringLiteral("hide_joinpart")).toBool(), true);
        QCOMPARE(values.value(QStringLiteral("separator_line")).toBool(), false);
        QCOMPARE(values.value(QStringLiteral("nick_width")).toInt(), 22);
        QCOMPARE(values.value(QStringLiteral("server_list_visible")).toBool(), false);
        QCOMPARE(values.value(QStringLiteral("member_list_visible")).toBool(), false);
        QCOMPARE(values.value(QStringLiteral("show_button_bar")).toBool(), false);
        QCOMPARE(values.value(QStringLiteral("buffer_tabs")).toBool(), true);
        QCOMPARE(values.value(QStringLiteral("connect_on_start")).toBool(), true);
        QCOMPARE(values.value(QStringLiteral("colored_nicks")).toBool(), false);
        QCOMPARE(values.value(QStringLiteral("show_formatting")).toBool(), false);
        QCOMPARE(values.value(QStringLiteral("logging")).toBool(), false);
        QCOMPARE(values.value(QStringLiteral("replay_log")).toBool(), false);
        const QVariantMap services = values.value(QStringLiteral("content_services")).toMap();
        QCOMPARE(services.value(QStringLiteral("images")).toBool(), false);
        QCOMPARE(services.value(QStringLiteral("media")).toBool(), false);
        QCOMPARE(services.value(QStringLiteral("xcards")).toBool(), true);
        QCOMPARE(services.value(QStringLiteral("webcards")).toBool(), false);
    }

    void fontButtonsSetExpectedProfiles() {
        PreferencesDialog dialog(maxchat::core::SettingsStore::defaultSettings());

        auto* jetbrains = dialog.findChild<QPushButton*>(QStringLiteral("setAllJetBrains"));
        auto* system = dialog.findChild<QPushButton*>(QStringLiteral("setAllSystem"));
        auto* appFont = dialog.findChild<QFontComboBox*>(QStringLiteral("appFontFamily"));
        auto* chatFont = dialog.findChild<QFontComboBox*>(QStringLiteral("chatFontFamily"));
        auto* appBold = dialog.findChild<QCheckBox*>(QStringLiteral("appFontBold"));
        auto* chatBold = dialog.findChild<QCheckBox*>(QStringLiteral("chatFontBold"));
        QVERIFY(jetbrains != nullptr);
        QVERIFY(system != nullptr);
        QVERIFY(appFont != nullptr);
        QVERIFY(chatFont != nullptr);
        QVERIFY(appBold != nullptr);
        QVERIFY(chatBold != nullptr);

        // System preset: chrome areas (app/nick/status/topic) use the UI font;
        // chat + list use the fixed-width font; nothing bold.
        QTest::mouseClick(system, Qt::LeftButton);
        QVERIFY(!appFont->currentFont().family().trimmed().isEmpty());
        QCOMPARE(appBold->isChecked(), false);
        QCOMPARE(chatBold->isChecked(), false);
        QVariantMap values = dialog.settings();
        QCOMPARE(values.value(QStringLiteral("list_font_family")).toString(),
                 chatFont->currentFont().family());
        QCOMPARE(values.value(QStringLiteral("list_font_bold")).toBool(), false);
        QCOMPARE(values.value(QStringLiteral("status_font_family")).toString(),
                 appFont->currentFont().family());
        QCOMPARE(values.value(QStringLiteral("topic_font_family")).toString(),
                 appFont->currentFont().family());

        // JetBrains preset: every area goes bold and settings() faithfully reports
        // each combo's resolved family. (QFontComboBox snaps an uninstalled family to
        // the nearest installed one in a headless test env, so assert that settings()
        // mirrors the combos rather than a literal "JetBrains Mono".)
        QTest::mouseClick(jetbrains, Qt::LeftButton);
        QCOMPARE(appBold->isChecked(), true);
        QCOMPARE(chatBold->isChecked(), true);
        values = dialog.settings();
        QCOMPARE(values.value(QStringLiteral("app_font_family")).toString(),
                 appFont->currentFont().family());
        QCOMPARE(values.value(QStringLiteral("chat_font_family")).toString(),
                 chatFont->currentFont().family());
        QCOMPARE(values.value(QStringLiteral("list_font_bold")).toBool(), true);
        QCOMPARE(values.value(QStringLiteral("nick_font_bold")).toBool(), true);
    }

    void languageCombosPreserveCodes() {
        PreferencesDialog dialog(maxchat::core::SettingsStore::defaultSettings());

        auto* interfaceLanguage = dialog.findChild<QComboBox*>(QStringLiteral("interfaceLanguage"));
        auto* spellLanguage = dialog.findChild<QComboBox*>(QStringLiteral("spellLanguage"));
        QVERIFY(interfaceLanguage != nullptr);
        QVERIFY(spellLanguage != nullptr);
        QVERIFY(spellLanguage->findData(QStringLiteral("en")) >= 0);
        QVERIFY(spellLanguage->findData(QStringLiteral("es")) >= 0);
        QVERIFY(spellLanguage->findData(QStringLiteral("pt_BR")) >= 0);

        for (int index = 0; index < spellLanguage->count(); ++index) {
            QVERIFY(!spellLanguage->itemText(index).contains(QStringLiteral("dictionary pending")));
        }

        interfaceLanguage->setCurrentIndex(interfaceLanguage->findData(QStringLiteral("es")));
        spellLanguage->setCurrentIndex(spellLanguage->findData(QStringLiteral("fr")));

        const QVariantMap values = dialog.settings();
        QCOMPARE(values.value(QStringLiteral("interface_language")).toString(),
                 QStringLiteral("es"));
        QCOMPARE(values.value(QStringLiteral("spell_language")).toString(), QStringLiteral("fr"));
    }

    void dataButtonsEmitActionSignals() {
        PreferencesDialog dialog(maxchat::core::SettingsStore::defaultSettings());

        auto* exportSettings = dialog.findChild<QPushButton*>(QStringLiteral("exportSettings"));
        auto* importSettings = dialog.findChild<QPushButton*>(QStringLiteral("importSettings"));
        auto* resetServers = dialog.findChild<QPushButton*>(QStringLiteral("resetServerList"));
        QVERIFY(exportSettings != nullptr);
        QVERIFY(importSettings != nullptr);
        QVERIFY(resetServers != nullptr);

        QSignalSpy exportSpy(&dialog, &PreferencesDialog::exportSettingsRequested);
        QSignalSpy importSpy(&dialog, &PreferencesDialog::importSettingsRequested);
        QSignalSpy resetSpy(&dialog, &PreferencesDialog::resetServerListRequested);

        QTest::mouseClick(exportSettings, Qt::LeftButton);
        QTest::mouseClick(importSettings, Qt::LeftButton);
        QTest::mouseClick(resetServers, Qt::LeftButton);

        QCOMPARE(exportSpy.count(), 1);
        QCOMPARE(importSpy.count(), 1);
        QCOMPARE(resetSpy.count(), 1);
    }
};

QTEST_MAIN(PreferencesDialogTest)

#include "preferences_dialog_test.moc"
