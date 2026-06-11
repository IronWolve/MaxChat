#include "ui/PreferencesDialog.h"

#include "core/SettingsStore.h"
#include "spell/SpellcheckDictionaryCatalog.h"
#include "ui/ThemeCatalog.h"
#include "ui/ThemeEditorDialog.h"
#include "ui/AppIcon.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QInputDialog>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QLabel>
#include <QUrl>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <utility>

namespace maxchat::ui {

namespace {

void setComboByData(QComboBox* combo, const QVariant& value) {
    const int index = combo->findData(value);
    combo->setCurrentIndex(index >= 0 ? index : 0);
}

QSpinBox* fontSizeSpinBox(QWidget* parent, int value) {
    auto* spin = new QSpinBox(parent);
    spin->setRange(8, 32);
    spin->setValue(value > 0 ? value : 14);
    return spin;
}

QCheckBox* plannedCheckBox(QWidget* parent, const QString& label) {
    auto* box = new QCheckBox(QStringLiteral("%1 (planned)").arg(label), parent);
    box->setEnabled(false);
    box->setToolTip(QStringLiteral("Planned - not implemented in the C++ port yet."));
    return box;
}

qint64 directorySizeBytes(const QString& path) {
    if (path.isEmpty() || !QDir(path).exists()) {
        return 0;
    }
    qint64 total = 0;
    QDirIterator it(path, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

QString humanBytes(const qint64 bytes) {
    constexpr double Kb = 1024.0;
    if (bytes >= Kb * Kb * Kb) {
        return QStringLiteral("%1 GB").arg(bytes / (Kb * Kb * Kb), 0, 'f', 1);
    }
    if (bytes >= Kb * Kb) {
        return QStringLiteral("%1 MB").arg(bytes / (Kb * Kb), 0, 'f', 1);
    }
    if (bytes >= Kb) {
        return QStringLiteral("%1 KB").arg(bytes / Kb, 0, 'f', 1);
    }
    return QStringLiteral("%1 B").arg(bytes);
}

} // namespace

// "Pick" + "Default" pair for a per-area color override; "" = follow theme.
class ColorPick final : public QWidget {
  public:
    explicit ColorPick(QString initial, QWidget* parent) : QWidget(parent),
                                                           value_(std::move(initial)) {
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        pick_ = new QPushButton(this);
        auto* reset = new QPushButton(QStringLiteral("Default"), this);
        reset->setToolTip(QStringLiteral("Back to the theme's own color."));
        layout->addWidget(pick_);
        layout->addWidget(reset);
        layout->addStretch(1);
        connect(pick_, &QPushButton::clicked, this, [this]() {
            const QColor chosen = QColorDialog::getColor(
                QColor(value_.isEmpty() ? QStringLiteral("#ffffff") : value_), this,
                QStringLiteral("Pick a color"));
            if (chosen.isValid()) {
                value_ = chosen.name();
                refresh();
            }
        });
        connect(reset, &QPushButton::clicked, this, [this]() {
            value_.clear();
            refresh();
        });
        refresh();
    }

    [[nodiscard]] QString value() const { return value_; }

  private:
    void refresh() {
        if (value_.isEmpty()) {
            pick_->setText(QStringLiteral("Pick"));
            pick_->setStyleSheet({});
            return;
        }
        const QColor color(value_);
        const bool light =
            0.299 * color.red() + 0.587 * color.green() + 0.114 * color.blue() > 150.0;
        pick_->setText(value_);
        pick_->setStyleSheet(QStringLiteral("background:%1;color:%2;")
                                 .arg(value_, light ? QStringLiteral("black")
                                                    : QStringLiteral("white")));
    }

    QString value_;
    QPushButton* pick_ = nullptr;
};

namespace {

QVariantMap contentServicesFromSettings(const QVariantMap& settings) {
    QVariantMap services = settings.value(QStringLiteral("content_services")).toMap();
    if (!services.contains(QStringLiteral("images"))) {
        services.insert(QStringLiteral("images"), true);
    }
    if (!services.contains(QStringLiteral("media"))) {
        services.insert(QStringLiteral("media"), true);
    }
    if (!services.contains(QStringLiteral("xcards"))) {
        services.insert(QStringLiteral("xcards"), true);
    }
    if (!services.contains(QStringLiteral("webcards"))) {
        services.insert(QStringLiteral("webcards"), true);
    }
    return services;
}

} // namespace

PreferencesDialog::PreferencesDialog(QVariantMap settings, QWidget* parent)
    : QDialog(parent), settings_(std::move(settings)) {
    setWindowTitle(QStringLiteral("Preferences"));
    resize(1000, 680);

    auto* root = new QVBoxLayout(this);
    auto* content = new QHBoxLayout();
    content->setSpacing(12);

    auto* navigation = new QListWidget(this);
    navigation->setObjectName(QStringLiteral("preferencesButtons"));
    navigation->setSelectionMode(QAbstractItemView::SingleSelection);
    navigation->setUniformItemSizes(true);

    auto* pages = new QStackedWidget(this);
    pages->setObjectName(QStringLiteral("preferencesPages"));

    const auto addPage = [this, navigation, pages](const QString& label, auto builder) {
        auto* page = new QWidget(this);
        builder(page);
        navigation->addItem(label);
        pages->addWidget(page);
    };

    // Page order mirrors the Python app, with the port-only Layout page kept
    // right after Appearance.
    addPage(QStringLiteral("Appearance"), [this](QWidget* page) { buildAppearanceTab(page); });

    addPage(QStringLiteral("Layout"), [this](QWidget* page) { buildLayoutTab(page); });

    addPage(QStringLiteral("Messages"), [this](QWidget* page) { buildMessagesTab(page); });

    addPage(QStringLiteral("Notifications"),
            [this](QWidget* page) { buildNotificationsTab(page); });

    addPage(QStringLiteral("Protection"), [this](QWidget* page) { buildProtectionTab(page); });

    addPage(QStringLiteral("Files (DCC)"), [this](QWidget* page) { buildFilesTab(page); });

    addPage(QStringLiteral("Themes"), [this](QWidget* page) { buildThemesTab(page); });

    addPage(QStringLiteral("Fonts"), [this](QWidget* page) { buildFontsTab(page); });

    addPage(QStringLiteral("Localization"), [this](QWidget* page) { buildLocalizationTab(page); });

    addPage(QStringLiteral("Comic"), [this](QWidget* page) { buildComicTab(page); });

    addPage(QStringLiteral("Scripts"), [this](QWidget* page) { buildScriptsTab(page); });

    addPage(QStringLiteral("Services"), [this](QWidget* page) { buildServicesTab(page); });

    addPage(QStringLiteral("Data"), [this](QWidget* page) { buildDataTab(page); });

    // Fit the nav to its longest label (plus the QSS item padding/margins) so
    // entries like "Localization" never truncate.
    int longestLabel = 0;
    for (int i = 0; i < navigation->count(); ++i) {
        longestLabel = qMax(longestLabel,
                            navigation->fontMetrics().horizontalAdvance(
                                navigation->item(i)->text()));
    }
    navigation->setFixedWidth(qMax(170, longestLabel + 64));

    connect(navigation, &QListWidget::currentRowChanged, pages, &QStackedWidget::setCurrentIndex);
    navigation->setCurrentRow(0);

    content->addWidget(navigation);
    content->addWidget(pages, 1);
    root->addLayout(content, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &PreferencesDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &PreferencesDialog::reject);
    root->addWidget(buttons);
}

QVariantMap PreferencesDialog::settings() const {
    QVariantMap out = settings_;
    if (scriptRead_ != nullptr) {
        QVariantMap perms;
        perms.insert(QStringLiteral("read"), scriptRead_->isChecked());
        perms.insert(QStringLiteral("write"), scriptWrite_->isChecked());
        perms.insert(QStringLiteral("exec"), scriptExec_->isChecked());
        perms.insert(QStringLiteral("modules"), scriptModules_->isChecked());
        perms.insert(QStringLiteral("network"), scriptNetwork_->isChecked());
        out.insert(QStringLiteral("script_perms"), perms);
        QVariantList dirs;
        for (int i = 0; i < scriptDirs_->count(); ++i) {
            dirs.append(scriptDirs_->item(i)->text());
        }
        out.insert(QStringLiteral("script_dirs"), dirs);
    }
    out.insert(QStringLiteral("theme"), theme_->currentData().toString());
    out.insert(QStringLiteral("chat_theme"), chatTheme_->currentData().toString());
    out.insert(QStringLiteral("wallpaper"), wallpaper_->currentData().toString());
    out.insert(QStringLiteral("app_font_family"), appFontFamily_->currentFont().family());
    out.insert(QStringLiteral("app_font_size"), appFontSize_->value());
    out.insert(QStringLiteral("app_font_bold"), appFontBold_->isChecked());
    out.insert(QStringLiteral("chat_font_family"), chatFontFamily_->currentFont().family());
    out.insert(QStringLiteral("chat_font_size"), chatFontSize_->value());
    out.insert(QStringLiteral("chat_font_bold"), chatFontBold_->isChecked());
    out.insert(QStringLiteral("list_font_family"), listFontFamily_->currentFont().family());
    out.insert(QStringLiteral("list_font_size"), listFontSize_->value());
    out.insert(QStringLiteral("list_font_bold"), listFontBold_->isChecked());
    out.insert(QStringLiteral("chat_text_color"), chatTextColor_->value());
    out.insert(QStringLiteral("event_color"), eventColor_->value());
    out.insert(QStringLiteral("tree_color"), treeColor_->value());
    out.insert(QStringLiteral("userlist_color"), userlistColor_->value());
    out.insert(QStringLiteral("nick_label_color"), nickLabelColor_->value());
    out.insert(QStringLiteral("status_text_color"), statusColor_->value());
    out.insert(QStringLiteral("topic_color"), topicColor_->value());
    out.insert(QStringLiteral("nick_font_family"), nickFontFamily_->currentFont().family());
    out.insert(QStringLiteral("nick_font_size"), nickFontSize_->value());
    out.insert(QStringLiteral("nick_font_bold"), nickFontBold_->isChecked());
    out.insert(QStringLiteral("status_font_family"), statusFontFamily_->currentFont().family());
    out.insert(QStringLiteral("status_font_size"), statusFontSize_->value());
    out.insert(QStringLiteral("status_font_bold"), statusFontBold_->isChecked());
    out.insert(QStringLiteral("topic_font_family"), topicFontFamily_->currentFont().family());
    out.insert(QStringLiteral("topic_font_size"), topicFontSize_->value());
    out.insert(QStringLiteral("topic_font_bold"), topicFontBold_->isChecked());
    out.insert(QStringLiteral("show_timestamps"), showTimestamps_->isChecked());
    {
        // Prefer the preset's strftime data; fall back to typed custom text.
        const int idx = timestampFormat_->currentIndex();
        const QString typed = timestampFormat_->currentText().trimmed();
        QString fmt = typed.isEmpty() ? QStringLiteral("%I:%M %p") : typed;
        if (idx >= 0 && timestampFormat_->itemText(idx) == typed) {
            fmt = timestampFormat_->itemData(idx).toString();
        }
        out.insert(QStringLiteral("timestamp_format"), fmt);
    }
    out.insert(QStringLiteral("word_wrap"), wordWrap_->isChecked());
    out.insert(QStringLiteral("align_nicks"), alignNicks_->isChecked());
    out.insert(QStringLiteral("separator_line"), separatorLine_->isChecked());
    out.insert(QStringLiteral("nick_width"), nickWidth_->value());
    out.insert(QStringLiteral("hide_joinpart"), hideJoinPart_->isChecked());
    out.insert(QStringLiteral("colored_nicks"), coloredNicks_->isChecked());
    out.insert(QStringLiteral("show_formatting"), showFormatting_->isChecked());
    out.insert(QStringLiteral("indent_wrap"), indentWrap_->isChecked());
    out.insert(QStringLiteral("marker_line"), markerLine_->isChecked());
    out.insert(QStringLiteral("show_mode"), showMode_->isChecked());
    out.insert(QStringLiteral("pm_echo"), pmEcho_->isChecked());
    out.insert(QStringLiteral("log_mask"), logMask_->text().trimmed().isEmpty()
                                               ? QStringLiteral("%network-%channel")
                                               : logMask_->text().trimmed());
    out.insert(QStringLiteral("replay_lines"), replayLines_->value());
    out.insert(QStringLiteral("show_input_hint"), inputHint_->isChecked());
    out.insert(QStringLiteral("strip_color_copy"), stripColorCopy_->isChecked());
    out.insert(QStringLiteral("sort_status"), sortByStatus_->isChecked());
    out.insert(QStringLiteral("tray_icon"), trayIcon_->currentData().toString());
    out.insert(QStringLiteral("server_list_visible"), serverListVisible_->isChecked());
    out.insert(QStringLiteral("member_list_visible"), memberListVisible_->isChecked());
    out.insert(QStringLiteral("show_button_bar"), buttonBarVisible_->isChecked());
    out.insert(QStringLiteral("buffer_tabs"), buttonsAsTabs_->isChecked());
    out.insert(QStringLiteral("connect_on_start"), connectOnStart_->isChecked());
    out.insert(QStringLiteral("auto_reconnect"), autoReconnect_->isChecked());
    out.insert(QStringLiteral("flood_protect"), floodProtect_->isChecked());
    out.insert(QStringLiteral("flood_msgs"), floodMessages_->value());
    out.insert(QStringLiteral("flood_secs"), floodSeconds_->value());
    out.insert(QStringLiteral("paste_guard"), pasteGuard_->isChecked());
    out.insert(QStringLiteral("paste_lines"), pasteLines_->value());
    out.insert(QStringLiteral("auto_rejoin"), autoRejoin_->isChecked());
    out.insert(QStringLiteral("ignore_invites"), ignoreInvites_->isChecked());
    out.insert(QStringLiteral("invite_protect"), inviteProtect_->isChecked());
    out.insert(QStringLiteral("confirm_quit"), confirmQuit_->isChecked());
    if (updateCheck_) out.insert(QStringLiteral("update_check"), updateCheck_->isChecked());
    out.insert(QStringLiteral("scrollback"), scrollback_->value());
    out.insert(QStringLiteral("hide_version"), hideVersion_->isChecked());
    out.insert(QStringLiteral("ctcp_version"), ctcpVersion_->text().trimmed());
    out.insert(QStringLiteral("logging"), loggingEnabled_->isChecked());
    out.insert(QStringLiteral("replay_log"), replayLogEnabled_->isChecked());
    out.insert(QStringLiteral("dcc_accept"), dccAccept_->currentData().toString());
    out.insert(QStringLiteral("dcc_trusted"),
               dccTrusted_->text().toLower().split(QRegularExpression(QStringLiteral("[,\\s]+")),
                                                   Qt::SkipEmptyParts));
    out.insert(QStringLiteral("dcc_passive"), dccPassive_->isChecked());
    out.insert(QStringLiteral("dcc_ip"), dccIp_->text().trimmed());
    out.insert(QStringLiteral("dcc_port_first"), dccPortFirst_->value());
    out.insert(QStringLiteral("dcc_port_last"), dccPortLast_->value());
    out.insert(QStringLiteral("dcc_dir"), dccDir_->text().trimmed());
    out.insert(QStringLiteral("interface_language"), interfaceLanguage_->currentData().toString());
    out.insert(QStringLiteral("spellcheck_enabled"), spellcheckEnabled_->isChecked());
    out.insert(QStringLiteral("spell_language"), spellLanguage_->currentData().toString());
    // Notifications
    if (dnd_) out.insert(QStringLiteral("dnd"), dnd_->isChecked());
    if (notifyPopup_) out.insert(QStringLiteral("notify_popup"), notifyPopup_->currentData().toString());
    if (notifyPm_) out.insert(QStringLiteral("notify_pm"), notifyPm_->isChecked());
    if (notifyHighlight_) out.insert(QStringLiteral("notify_highlight"), notifyHighlight_->isChecked());
    if (highlightWords_) out.insert(QStringLiteral("highlight_words"), highlightWords_->text().trimmed());
    if (notifyFlash_) out.insert(QStringLiteral("notify_flash"), notifyFlash_->isChecked());
    if (notifyCorner_) out.insert(QStringLiteral("notify_corner"), notifyCorner_->currentData().toString());
    if (notifyDuration_) out.insert(QStringLiteral("notify_duration"), notifyDuration_->value());
    if (notifyTheme_) out.insert(QStringLiteral("notify_theme"), notifyTheme_->currentData().toString());
    if (beepHighlight_) out.insert(QStringLiteral("beep_highlight"), beepHighlight_->isChecked());
    if (notifySound_) out.insert(QStringLiteral("notify_sound"), notifySound_->isChecked());
    if (ctcpSound_) out.insert(QStringLiteral("ctcp_sound"), ctcpSound_->isChecked());
    if (minimizeToTray_) out.insert(QStringLiteral("minimize_to_tray"), minimizeToTray_->isChecked());

    QVariantMap services = contentServicesFromSettings(out);
    services.insert(QStringLiteral("images"), linkImages_->isChecked());
    services.insert(QStringLiteral("media"), linkMedia_->isChecked());
    services.insert(QStringLiteral("xcards"), linkXCards_->isChecked());
    services.insert(QStringLiteral("webcards"), linkWebCards_->isChecked());
    out.insert(QStringLiteral("content_services"), services);
    return out;
}

void PreferencesDialog::refillThemeCombo(QComboBox* combo, const bool chat,
                                         const QString& selectId) {
    if (combo == nullptr) {
        return;
    }
    const QSignalBlocker blocker(combo);
    combo->clear();
    if (chat) {
        for (const ChatThemeDefinition& theme : chatThemes()) {
            combo->addItem(theme.label, theme.id);
        }
        setComboByData(combo, normalizeChatThemeId(selectId));
    } else {
        for (const AppThemeDefinition& theme : appThemes()) {
            combo->addItem(theme.label, theme.id);
        }
        setComboByData(combo, normalizeThemeId(selectId));
    }
}

void PreferencesDialog::setAllFonts(const QString& family, int size, bool bold) {
    const QFont font(family);
    for (QFontComboBox* combo : {appFontFamily_, chatFontFamily_, listFontFamily_,
                                 nickFontFamily_, statusFontFamily_, topicFontFamily_}) {
        if (combo != nullptr) {
            combo->setCurrentFont(font);
        }
    }
    for (QSpinBox* spin : {appFontSize_, chatFontSize_, listFontSize_, nickFontSize_,
                           statusFontSize_, topicFontSize_}) {
        if (spin != nullptr) {
            spin->setValue(size);
        }
    }
    for (QCheckBox* boldBox : {appFontBold_, chatFontBold_, listFontBold_, nickFontBold_,
                               statusFontBold_, topicFontBold_}) {
        if (boldBox != nullptr) {
            boldBox->setChecked(bold);
        }
    }
}

void PreferencesDialog::buildAppearanceTab(QWidget* tab) {
    auto* outer = new QVBoxLayout(tab);
    auto* columns = new QHBoxLayout();
    auto* leftColumn = new QVBoxLayout();
    auto* rightColumn = new QVBoxLayout();
    columns->addLayout(leftColumn);
    columns->addLayout(rightColumn);
    outer->addLayout(columns);
    outer->addStretch(1);

    auto* timestampBox = new QGroupBox(QStringLiteral("Timestamps"), tab);
    auto* timestampForm = new QFormLayout(timestampBox);
    showTimestamps_ = new QCheckBox(QString(), tab);
    showTimestamps_->setObjectName(QStringLiteral("showTimestamps"));
    showTimestamps_->setChecked(settings_.value(QStringLiteral("show_timestamps")).toBool());
    timestampFormat_ = new QComboBox(tab);
    timestampFormat_->setObjectName(QStringLiteral("timestampFormat"));
    timestampFormat_->setEditable(true);
    const QList<QPair<QString, QString>> clockPresets = {
        {QStringLiteral("3:45 PM"), QStringLiteral("%I:%M %p")},
        {QStringLiteral("3:45:09 PM"), QStringLiteral("%I:%M:%S %p")},
        {QStringLiteral("[3:45 PM]"), QStringLiteral("[%I:%M %p]")},
        {QStringLiteral("15:45"), QStringLiteral("%H:%M")},
        {QStringLiteral("15:45:09"), QStringLiteral("%H:%M:%S")},
        {QStringLiteral("[15:45]"), QStringLiteral("[%H:%M]")},
        {QStringLiteral("[2026-06-06 3:45 PM]"), QStringLiteral("[%Y-%m-%d %I:%M %p]")},
        {QStringLiteral("[2026-06-06 15:45]"), QStringLiteral("[%Y-%m-%d %H:%M]")},
    };
    for (const auto& preset : clockPresets) {
        timestampFormat_->addItem(preset.first, preset.second);
    }
    const QString currentFmt =
        settings_.value(QStringLiteral("timestamp_format"), QStringLiteral("%I:%M %p")).toString();
    int fmtIndex = timestampFormat_->findData(currentFmt);
    if (fmtIndex >= 0) {
        timestampFormat_->setCurrentIndex(fmtIndex);
    } else {
        timestampFormat_->setEditText(currentFmt);
    }
    timestampForm->addRow(QStringLiteral("Show timestamps"), showTimestamps_);
    timestampForm->addRow(QStringLiteral("Clock / format"), timestampFormat_);
    leftColumn->addWidget(timestampBox);

    auto* nickBox = new QGroupBox(QStringLiteral("Nicknames"), tab);
    auto* nickForm = new QFormLayout(nickBox);
    coloredNicks_ = new QCheckBox(QString(), tab);
    coloredNicks_->setObjectName(QStringLiteral("coloredNicks"));
    coloredNicks_->setChecked(settings_.value(QStringLiteral("colored_nicks"), true).toBool());
    alignNicks_ = new QCheckBox(QString(), tab);
    alignNicks_->setObjectName(QStringLiteral("alignNicks"));
    alignNicks_->setChecked(settings_.value(QStringLiteral("align_nicks")).toBool());
    nickWidth_ = new QSpinBox(tab);
    nickWidth_->setObjectName(QStringLiteral("nickWidth"));
    nickWidth_->setRange(4, 24);
    nickWidth_->setValue(settings_.value(QStringLiteral("nick_width"), 16).toInt());
    separatorLine_ = new QCheckBox(QString(), tab);
    separatorLine_->setObjectName(QStringLiteral("separatorLine"));
    separatorLine_->setChecked(settings_.value(QStringLiteral("separator_line"), true).toBool());
    nickForm->addRow(QStringLiteral("Color nicknames"), coloredNicks_);
    nickForm->addRow(QStringLiteral("Align nick column"), alignNicks_);
    nickForm->addRow(QStringLiteral("Nick column width"), nickWidth_);
    nickForm->addRow(QStringLiteral("Nick separator line"), separatorLine_);
    leftColumn->addWidget(nickBox);
    leftColumn->addStretch(1);

    auto* textBox = new QGroupBox(QStringLiteral("Text"), tab);
    auto* textForm = new QFormLayout(textBox);
    showFormatting_ = new QCheckBox(QString(), tab);
    showFormatting_->setObjectName(QStringLiteral("showFormatting"));
    showFormatting_->setChecked(settings_.value(QStringLiteral("show_formatting"), true).toBool());
    wordWrap_ = new QCheckBox(QString(), tab);
    wordWrap_->setObjectName(QStringLiteral("wordWrap"));
    wordWrap_->setChecked(settings_.value(QStringLiteral("word_wrap")).toBool());
    indentWrap_ = new QCheckBox(QString(), tab);
    indentWrap_->setObjectName(QStringLiteral("indentWrap"));
    indentWrap_->setChecked(settings_.value(QStringLiteral("indent_wrap"), true).toBool());
    stripColorCopy_ = new QCheckBox(QString(), tab);
    stripColorCopy_->setObjectName(QStringLiteral("stripColorCopy"));
    stripColorCopy_->setChecked(settings_.value(QStringLiteral("strip_color_copy"), true).toBool());
    textForm->addRow(QStringLiteral("Show colors && formatting"), showFormatting_);
    textForm->addRow(QStringLiteral("Word wrap"), wordWrap_);
    textForm->addRow(QStringLiteral("Indent wrapped lines"), indentWrap_);
    textForm->addRow(QStringLiteral("Strip colors on copy"), stripColorCopy_);
    rightColumn->addWidget(textBox);

    auto* windowBox = new QGroupBox(QStringLiteral("Window"), tab);
    auto* windowForm = new QFormLayout(windowBox);
    trayIcon_ = new QComboBox(tab);
    trayIcon_->setObjectName(QStringLiteral("trayIcon"));
    const QColor iconAccent = appThemeById(
        settings_.value(QStringLiteral("theme"), QStringLiteral("dark")).toString()).on;
    const QList<QPair<QString, QString>> iconChoices = {
        {QStringLiteral("bubble"), QStringLiteral("Speech bubble (theme color)")},
        {QString::fromUtf8("\xF0\x9F\x92\xAC"), QStringLiteral("Speech balloon")},
        {QString::fromUtf8("\xF0\x9F\x92\xAD"), QStringLiteral("Thought bubble")},
        {QString::fromUtf8("\xF0\x9F\x97\xA8\xEF\xB8\x8F"), QStringLiteral("Left speech bubble")},
        {QString::fromUtf8("\xF0\x9F\x98\x80"), QStringLiteral("Smiley")},
        {QString::fromUtf8("\xF0\x9F\x98\x8E"), QStringLiteral("Cool shades")},
        {QString::fromUtf8("\xF0\x9F\xA4\x96"), QStringLiteral("Robot")},
        {QString::fromUtf8("\xF0\x9F\x91\xBE"), QStringLiteral("Alien")},
        {QString::fromUtf8("\xF0\x9F\x90\xA7"), QStringLiteral("Penguin")},
        {QString::fromUtf8("\xE2\xAD\x90"), QStringLiteral("Star")},
        {QString::fromUtf8("\xF0\x9F\x94\x94"), QStringLiteral("Bell")},
        {QString::fromUtf8("\xF0\x9F\x92\xBB"), QStringLiteral("Computer")},
    };
    for (const auto& choice : iconChoices) {
        trayIcon_->addItem(ui::AppIcon::makeIcon(choice.first, iconAccent), choice.second,
                           choice.first);
    }
    setComboByData(trayIcon_,
                   settings_.value(QStringLiteral("tray_icon"), QStringLiteral("bubble")).toString());
    buttonBarVisible_ = new QCheckBox(QString(), tab);
    buttonBarVisible_->setObjectName(QStringLiteral("buttonBarVisible"));
    buttonBarVisible_->setChecked(
        settings_.value(QStringLiteral("show_button_bar"), false).toBool());
    inputHint_ = new QCheckBox(QString(), tab);
    inputHint_->setObjectName(QStringLiteral("inputHint"));
    inputHint_->setChecked(settings_.value(QStringLiteral("show_input_hint"), true).toBool());
    markerLine_ = new QCheckBox(QString(), tab);
    markerLine_->setObjectName(QStringLiteral("markerLine"));
    markerLine_->setChecked(settings_.value(QStringLiteral("marker_line"), true).toBool());
    sortByStatus_ = new QCheckBox(QString(), tab);
    sortByStatus_->setObjectName(QStringLiteral("sortByStatus"));
    sortByStatus_->setChecked(settings_.value(QStringLiteral("sort_status"), true).toBool());
    windowForm->addRow(QStringLiteral("Window / tray icon"), trayIcon_);
    windowForm->addRow(QStringLiteral("Show button bar"), buttonBarVisible_);
    windowForm->addRow(QStringLiteral("Message-box hint text"), inputHint_);
    windowForm->addRow(QStringLiteral("Unread marker line"), markerLine_);
    windowForm->addRow(QStringLiteral("Sort users by status"), sortByStatus_);
    rightColumn->addWidget(windowBox);
    rightColumn->addStretch(1);
}

void PreferencesDialog::buildNotificationsTab(QWidget* tab) {
    auto* form = new QFormLayout(tab);
    form->setContentsMargins(12, 12, 12, 12);

    // Do Not Disturb
    dnd_ = new QCheckBox(tab);
    dnd_->setChecked(settings_.value(QStringLiteral("dnd"), false).toBool());
    dnd_->setToolTip(QStringLiteral("Suppress ALL notifications. Also toggleable from the Tools menu and the tray."));

    // Popup style
    notifyPopup_ = new QComboBox(tab);
    notifyPopup_->addItem(QStringLiteral("Off"), QStringLiteral("off"));
    notifyPopup_->addItem(QStringLiteral("Custom toast (in-app)"), QStringLiteral("custom"));
    notifyPopup_->addItem(QStringLiteral("System / OS native"), QStringLiteral("system"));
    setComboByData(notifyPopup_, settings_.value(QStringLiteral("notify_popup"), QStringLiteral("custom")).toString());

    // Notify on PMs
    notifyPm_ = new QCheckBox(tab);
    notifyPm_->setChecked(settings_.value(QStringLiteral("notify_pm"), true).toBool());

    // Notify on highlights
    notifyHighlight_ = new QCheckBox(tab);
    notifyHighlight_->setChecked(settings_.value(QStringLiteral("notify_highlight"), true).toBool());

    // Highlight words
    highlightWords_ = new QLineEdit(tab);
    highlightWords_->setText(settings_.value(QStringLiteral("highlight_words")).toString());
    highlightWords_->setPlaceholderText(QStringLiteral("extra words that highlight you (space/comma-separated)"));

    // Taskbar flash
    notifyFlash_ = new QCheckBox(tab);
    notifyFlash_->setChecked(settings_.value(QStringLiteral("notify_flash"), true).toBool());

    // Toast corner
    notifyCorner_ = new QComboBox(tab);
    notifyCorner_->addItem(QStringLiteral("Top-left"), QStringLiteral("tl"));
    notifyCorner_->addItem(QStringLiteral("Top-right"), QStringLiteral("tr"));
    notifyCorner_->addItem(QStringLiteral("Bottom-left"), QStringLiteral("bl"));
    notifyCorner_->addItem(QStringLiteral("Bottom-right"), QStringLiteral("br"));
    setComboByData(notifyCorner_, settings_.value(QStringLiteral("notify_corner"), QStringLiteral("br")).toString());

    // Toast duration
    notifyDuration_ = new QSpinBox(tab);
    notifyDuration_->setRange(2, 30);
    notifyDuration_->setValue(settings_.value(QStringLiteral("notify_duration"), 6).toInt());
    notifyDuration_->setSuffix(QStringLiteral(" s"));

    // Toast theme
    notifyTheme_ = new QComboBox(tab);
    notifyTheme_->addItem(QStringLiteral("Follow app theme"), QStringLiteral("follow"));
    notifyTheme_->addItem(QStringLiteral("Dark"), QStringLiteral("dark"));
    notifyTheme_->addItem(QStringLiteral("Light"), QStringLiteral("light"));
    notifyTheme_->addItem(QStringLiteral("Solarized dark"), QStringLiteral("solar-dark"));
    notifyTheme_->addItem(QStringLiteral("Solarized light"), QStringLiteral("solar-light"));
    setComboByData(notifyTheme_, settings_.value(QStringLiteral("notify_theme"), QStringLiteral("follow")).toString());

    // Beep on highlight / PM
    beepHighlight_ = new QCheckBox(tab);
    beepHighlight_->setChecked(settings_.value(QStringLiteral("beep_highlight"), false).toBool());

    // Sound on notification
    notifySound_ = new QCheckBox(tab);
    notifySound_->setChecked(settings_.value(QStringLiteral("notify_sound"), false).toBool());
    notifySound_->setToolTip(QStringLiteral(
        "Play a chime when a notification fires \u2014 a built-in default, or your own "
        "if you drop a notify.wav in <config>/sounds/."));

    // Play CTCP sounds
    ctcpSound_ = new QCheckBox(tab);
    ctcpSound_->setChecked(settings_.value(QStringLiteral("ctcp_sound"), false).toBool());
    ctcpSound_->setToolTip(QStringLiteral(
        "Play .wav sounds others send via CTCP SOUND (mIRC/Comic Chat). Drop your own "
        ".wav files in the 'sounds' folder under your config directory; a sound only "
        "plays if you have that file."));

    // Minimize to tray
    minimizeToTray_ = new QCheckBox(tab);
    minimizeToTray_->setChecked(settings_.value(QStringLiteral("minimize_to_tray"), false).toBool());

    // Check for updates on startup (off until the public release — see the
    // update_check default in SettingsStore for the TODO to flip it on).
    updateCheck_ = new QCheckBox(tab);
    updateCheck_->setObjectName(QStringLiteral("updateCheck"));
    updateCheck_->setChecked(settings_.value(QStringLiteral("update_check"), false).toBool());
    updateCheck_->setToolTip(QStringLiteral(
        "Quietly check GitHub Releases for a newer MaxChat shortly after launch. "
        "Help > Check for Updates works regardless of this setting."));

    form->addRow(QStringLiteral("Do Not Disturb"), dnd_);
    form->addRow(QStringLiteral("Popup style"), notifyPopup_);
    form->addRow(QStringLiteral("Notify on PMs"), notifyPm_);
    form->addRow(QStringLiteral("Notify on highlights"), notifyHighlight_);
    form->addRow(QStringLiteral("Highlight words"), highlightWords_);
    form->addRow(QStringLiteral("Taskbar flash"), notifyFlash_);
    form->addRow(QStringLiteral("Toast corner"), notifyCorner_);
    form->addRow(QStringLiteral("Toast duration (s)"), notifyDuration_);
    form->addRow(QStringLiteral("Toast theme"), notifyTheme_);
    form->addRow(QStringLiteral("Beep on highlight / PM"), beepHighlight_);
    form->addRow(QStringLiteral("Sound on notification"), notifySound_);
    form->addRow(QStringLiteral("Play CTCP sounds"), ctcpSound_);
    form->addRow(QStringLiteral("Minimize to tray"), minimizeToTray_);
    form->addRow(QStringLiteral("Check for updates on startup"), updateCheck_);

    // Test notification button
    auto* testBtn = new QPushButton(QStringLiteral("Test notification"), tab);
    testBtn->setToolTip(QStringLiteral("Preview a toast with the settings selected above (no need to save first)."));
    connect(testBtn, &QPushButton::clicked, this, &PreferencesDialog::testNotificationRequested);
    form->addRow(QString(), testBtn);

    // Hint text
    auto* hint = new QLabel(QStringLiteral(
        "Alerts fire only when the window isn\u2019t focused (no sounds). "
        "Tray features need a system tray \u2013 most Linux desktops and "
        "Windows; not plain Wayland."), tab);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#888;"));
    form->addRow(hint);
}

void PreferencesDialog::buildComicTab(QWidget* tab) {
    auto* root = new QVBoxLayout(tab);
    auto* note = new QLabel(
        QStringLiteral("Comic Mode draws the chat as comic panels. Point it at your own comic "
                       "art folder and tune characters, backgrounds, and filtering in Comic "
                       "Settings."),
        tab);
    note->setWordWrap(true);
    root->addWidget(note);

    auto* buttons = new QHBoxLayout();
    auto* openSettings = new QPushButton(QStringLiteral("Open Comic Settings..."), tab);
    openSettings->setObjectName(QStringLiteral("openComicSettings"));
    auto* browseChars = new QPushButton(QStringLiteral("Browse characters..."), tab);
    browseChars->setObjectName(QStringLiteral("browseCharacters"));
    buttons->addWidget(openSettings);
    buttons->addWidget(browseChars);
    buttons->addStretch(1);
    root->addLayout(buttons);
    root->addStretch(1);

    connect(openSettings, &QPushButton::clicked, this,
            &PreferencesDialog::openComicSettingsRequested);
    connect(browseChars, &QPushButton::clicked, this,
            &PreferencesDialog::browseCharactersRequested);
}

void PreferencesDialog::buildScriptsTab(QWidget* tab) {
    auto* root = new QVBoxLayout(tab);
    auto* note = new QLabel(
        QStringLiteral("Lua scripts run sandboxed by default — they can react to chat, add "
                       "commands, and use their own private data folder. Grant extra abilities "
                       "below only for scripts you trust; each one widens what a script can do "
                       "to your computer. Changes reload your scripts."),
        tab);
    note->setWordWrap(true);
    root->addWidget(note);

    const QVariantMap perms = settings_.value(QStringLiteral("script_perms")).toMap();
    const auto makeCheck = [&](const QString& label, const QString& key, const QString& tip) {
        auto* box = new QCheckBox(label, tab);
        box->setChecked(perms.value(key, false).toBool());
        box->setToolTip(tip);
        root->addWidget(box);
        return box;
    };
    scriptRead_ = makeCheck(QStringLiteral("Read files"), QStringLiteral("read"),
                            QStringLiteral("Allow io.open() to read files inside the allowed "
                                           "folders below."));
    scriptWrite_ = makeCheck(QStringLiteral("Write files"), QStringLiteral("write"),
                             QStringLiteral("Allow io.open() to create/modify files inside the "
                                            "allowed folders below."));
    scriptExec_ = makeCheck(QStringLiteral("Run programs"), QStringLiteral("exec"),
                            QStringLiteral("Allow os.execute / io.popen — scripts can launch "
                                           "other programs. High risk."));
    scriptModules_ = makeCheck(QStringLiteral("Load modules"), QStringLiteral("modules"),
                               QStringLiteral("Allow require()/load() — scripts can pull in other "
                                              "Lua or native libraries."));
    scriptNetwork_ = makeCheck(QStringLiteral("Network access"), QStringLiteral("network"),
                               QStringLiteral("Adds api.http_get(url) for fetching web content."));

    auto* dirsLabel =
        new QLabel(QStringLiteral("Folders scripts may read/write (the script's own data folder "
                                  "is always allowed):"),
                   tab);
    dirsLabel->setWordWrap(true);
    root->addWidget(dirsLabel);

    scriptDirs_ = new QListWidget(tab);
    for (const QVariant& dir : settings_.value(QStringLiteral("script_dirs")).toList()) {
        const QString path = dir.toString().trimmed();
        if (!path.isEmpty()) {
            scriptDirs_->addItem(path);
        }
    }
    root->addWidget(scriptDirs_, 1);

    auto* dirButtons = new QHBoxLayout();
    auto* addFolder = new QPushButton(QStringLiteral("Add folder..."), tab);
    auto* addDrive = new QPushButton(QStringLiteral("Add drive/path..."), tab);
    auto* removeDir = new QPushButton(QStringLiteral("Remove"), tab);
    dirButtons->addWidget(addFolder);
    dirButtons->addWidget(addDrive);
    dirButtons->addWidget(removeDir);
    dirButtons->addStretch(1);
    root->addLayout(dirButtons);

    connect(addFolder, &QPushButton::clicked, this, [this, tab]() {
        const QString dir = QFileDialog::getExistingDirectory(tab, QStringLiteral("Allow folder"));
        if (!dir.isEmpty() && scriptDirs_->findItems(dir, Qt::MatchExactly).isEmpty()) {
            scriptDirs_->addItem(dir);
        }
    });
    connect(addDrive, &QPushButton::clicked, this, [this, tab]() {
        bool ok = false;
        const QString path = QInputDialog::getText(
            tab, QStringLiteral("Add drive or path"),
            QStringLiteral("Drive or folder (e.g. D:\\ or /mnt/data):"), QLineEdit::Normal,
            QString(), &ok);
        const QString trimmed = path.trimmed();
        if (ok && !trimmed.isEmpty() &&
            scriptDirs_->findItems(trimmed, Qt::MatchExactly).isEmpty()) {
            scriptDirs_->addItem(trimmed);
        }
    });
    connect(removeDir, &QPushButton::clicked, this, [this]() {
        delete scriptDirs_->takeItem(scriptDirs_->currentRow());
    });
}

void PreferencesDialog::buildFontsTab(QWidget* tab) {
    auto* root = new QVBoxLayout(tab);

    auto* presetRow = new QHBoxLayout();
    auto* jetbrains = new QPushButton(QStringLiteral("Set all to JetBrains Mono"), tab);
    jetbrains->setObjectName(QStringLiteral("setAllJetBrains"));
    auto* system = new QPushButton(QStringLiteral("Set all to System Default"), tab);
    system->setObjectName(QStringLiteral("setAllSystem"));
    system->setToolTip(QStringLiteral("Uses the system UI font for chrome and the system "
                                      "fixed-width font for chat alignment."));
    presetRow->addWidget(jetbrains);
    presetRow->addWidget(system);
    presetRow->addStretch(1);
    root->addLayout(presetRow);

    const auto makeFontGroup = [this, tab](const QString& title, const QString& familyKey,
                                           const QString& sizeKey, const QString& boldKey,
                                           QFontComboBox*& familyOut, QSpinBox*& sizeOut,
                                           QCheckBox*& boldOut) -> QGroupBox* {
        // objectName mirrors the settings key (chat_font_family -> chatFontFamily)
        // so tests and stylesheets can target each control.
        const auto camel = [](const QString& key) {
            QString out;
            const QStringList parts = key.split(QLatin1Char('_'));
            for (int i = 0; i < parts.size(); ++i) {
                QString p = parts.at(i);
                if (i > 0 && !p.isEmpty()) {
                    p[0] = p[0].toUpper();
                }
                out += p;
            }
            return out;
        };
        auto* box = new QGroupBox(title, tab);
        auto* form = new QFormLayout(box);
        familyOut = new QFontComboBox(box);
        familyOut->setObjectName(camel(familyKey));
        const QString family =
            settings_.value(familyKey, QStringLiteral("JetBrains Mono")).toString();
        familyOut->setCurrentFont(QFont(family.isEmpty() ? QStringLiteral("JetBrains Mono")
                                                         : family));
        sizeOut = new QSpinBox(box);
        sizeOut->setObjectName(camel(sizeKey));
        sizeOut->setRange(6, 48);
        const int size = settings_.value(sizeKey, 14).toInt();
        sizeOut->setValue(size > 0 ? size : 14);
        boldOut = new QCheckBox(QStringLiteral("Bold"), box);
        boldOut->setObjectName(camel(boldKey));
        boldOut->setChecked(settings_.value(boldKey, true).toBool());
        auto* sizeRow = new QWidget(box);
        auto* sizeLayout = new QHBoxLayout(sizeRow);
        sizeLayout->setContentsMargins(0, 0, 0, 0);
        sizeLayout->addWidget(sizeOut);
        sizeLayout->addWidget(boldOut);
        sizeLayout->addStretch(1);
        form->addRow(QStringLiteral("Font"), familyOut);
        form->addRow(QStringLiteral("Size"), sizeRow);
        return box;
    };

    auto* columns = new QHBoxLayout();
    auto* leftColumn = new QVBoxLayout();
    auto* rightColumn = new QVBoxLayout();
    columns->addLayout(leftColumn);
    columns->addLayout(rightColumn);
    root->addLayout(columns);

    // Chat - the message view + input (font + text/event colors)
    auto* chatBox = makeFontGroup(QString::fromUtf8("Chat  \xC2\xB7  the message view + input"),
                                  QStringLiteral("chat_font_family"),
                                  QStringLiteral("chat_font_size"),
                                  QStringLiteral("chat_font_bold"), chatFontFamily_,
                                  chatFontSize_, chatFontBold_);
    auto* chatForm = qobject_cast<QFormLayout*>(chatBox->layout());
    chatTextColor_ =
        new ColorPick(settings_.value(QStringLiteral("chat_text_color")).toString(), tab);
    eventColor_ = new ColorPick(settings_.value(QStringLiteral("event_color")).toString(), tab);
    chatForm->addRow(QStringLiteral("Text color"), chatTextColor_);
    chatForm->addRow(QStringLiteral("Event lines"), eventColor_);
    leftColumn->addWidget(chatBox);

    auto* appBox = makeFontGroup(QString::fromUtf8("App  \xC2\xB7  window / menus / chrome"),
                                 QStringLiteral("app_font_family"),
                                 QStringLiteral("app_font_size"), QStringLiteral("app_font_bold"),
                                 appFontFamily_, appFontSize_, appFontBold_);
    leftColumn->addWidget(appBox);

    auto* listBox = makeFontGroup(QStringLiteral("Channel + user list"),
                                  QStringLiteral("list_font_family"),
                                  QStringLiteral("list_font_size"),
                                  QStringLiteral("list_font_bold"), listFontFamily_,
                                  listFontSize_, listFontBold_);
    auto* listForm = qobject_cast<QFormLayout*>(listBox->layout());
    treeColor_ = new ColorPick(settings_.value(QStringLiteral("tree_color")).toString(), tab);
    userlistColor_ =
        new ColorPick(settings_.value(QStringLiteral("userlist_color")).toString(), tab);
    listForm->addRow(QStringLiteral("Tree color"), treeColor_);
    listForm->addRow(QStringLiteral("Users color"), userlistColor_);
    leftColumn->addWidget(listBox);
    leftColumn->addStretch(1);

    auto* nickBox = makeFontGroup(QString::fromUtf8("Your nick  \xC2\xB7  beside the input"),
                                  QStringLiteral("nick_font_family"),
                                  QStringLiteral("nick_font_size"),
                                  QStringLiteral("nick_font_bold"), nickFontFamily_,
                                  nickFontSize_, nickFontBold_);
    auto* nickForm = qobject_cast<QFormLayout*>(nickBox->layout());
    nickLabelColor_ =
        new ColorPick(settings_.value(QStringLiteral("nick_label_color")).toString(), tab);
    nickForm->addRow(QStringLiteral("Color"), nickLabelColor_);
    rightColumn->addWidget(nickBox);

    auto* statusBox = makeFontGroup(QStringLiteral("Status bar"),
                                    QStringLiteral("status_font_family"),
                                    QStringLiteral("status_font_size"),
                                    QStringLiteral("status_font_bold"), statusFontFamily_,
                                    statusFontSize_, statusFontBold_);
    auto* statusForm = qobject_cast<QFormLayout*>(statusBox->layout());
    statusColor_ =
        new ColorPick(settings_.value(QStringLiteral("status_text_color")).toString(), tab);
    statusForm->addRow(QStringLiteral("Color"), statusColor_);
    rightColumn->addWidget(statusBox);

    auto* topicBox = makeFontGroup(QStringLiteral("Channel topic"),
                                   QStringLiteral("topic_font_family"),
                                   QStringLiteral("topic_font_size"),
                                   QStringLiteral("topic_font_bold"), topicFontFamily_,
                                   topicFontSize_, topicFontBold_);
    auto* topicForm = qobject_cast<QFormLayout*>(topicBox->layout());
    topicColor_ = new ColorPick(settings_.value(QStringLiteral("topic_color")).toString(), tab);
    topicForm->addRow(QStringLiteral("Color"), topicColor_);
    rightColumn->addWidget(topicBox);
    rightColumn->addStretch(1);

    auto* note = new QLabel(
        QStringLiteral("Each area has its own font, size, bold and color. \"Pick\" sets a custom "
                       "color; \"Default\" returns to the theme. App chrome color follows the "
                       "theme (Themes > Customize)."),
        tab);
    note->setWordWrap(true);
    root->addWidget(note);
    root->addStretch(1);

    connect(jetbrains, &QPushButton::clicked, this,
            [this]() { setAllFonts(QStringLiteral("JetBrains Mono"), 14, true); });
    connect(system, &QPushButton::clicked, this, [this]() {
        const QFont uiFont = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
        const QFont fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        const int uiSize = uiFont.pointSize() > 0 ? uiFont.pointSize() : 10;
        const int fixedSize = fixedFont.pointSize() > 0 ? fixedFont.pointSize() : uiSize;
        // Chat + list use the fixed-width font (alignment); chrome areas the UI font.
        chatFontFamily_->setCurrentFont(fixedFont);
        chatFontSize_->setValue(fixedSize);
        chatFontBold_->setChecked(false);
        listFontFamily_->setCurrentFont(fixedFont);
        listFontSize_->setValue(fixedSize);
        listFontBold_->setChecked(false);
        for (QFontComboBox* combo : {appFontFamily_, nickFontFamily_, statusFontFamily_,
                                     topicFontFamily_}) {
            combo->setCurrentFont(uiFont);
        }
        for (QSpinBox* spin : {appFontSize_, nickFontSize_, statusFontSize_, topicFontSize_}) {
            spin->setValue(uiSize);
        }
        for (QCheckBox* bold : {appFontBold_, nickFontBold_, statusFontBold_, topicFontBold_}) {
            bold->setChecked(false);
        }
    });
}

void PreferencesDialog::buildThemesTab(QWidget* tab) {
    auto* root = new QVBoxLayout(tab);

    auto* appBox = new QGroupBox(QStringLiteral("App - window / menus / chrome"), tab);
    auto* appForm = new QFormLayout(appBox);
    theme_ = new QComboBox(appBox);
    theme_->setObjectName(QStringLiteral("theme"));
    for (const AppThemeDefinition& theme : appThemes()) {
        theme_->addItem(theme.label, theme.id);
    }
    setComboByData(
        theme_,
        normalizeThemeId(
            settings_.value(QStringLiteral("theme"), QStringLiteral("synthwave")).toString()));
    appForm->addRow(QStringLiteral("Theme"), theme_);
    auto* appButtons = new QHBoxLayout();
    auto* appDefault = new QPushButton(QStringLiteral("Default"), appBox);
    appDefault->setObjectName(QStringLiteral("themeDefault"));
    auto* appOff = new QPushButton(QStringLiteral("Turn themes off"), appBox);
    appOff->setObjectName(QStringLiteral("themeOff"));
    appButtons->addWidget(appDefault);
    appButtons->addWidget(appOff);
    appButtons->addStretch(1);
    appForm->addRow(QString(), appButtons);
    auto* appCustomize = new QPushButton(QStringLiteral("Customize..."), appBox);
    appCustomize->setObjectName(QStringLiteral("customizeAppTheme"));
    connect(appCustomize, &QPushButton::clicked, this, [this]() {
        ThemeEditorDialog editor(ThemeEditorDialog::Scope::App, theme_->currentData().toString(),
                                 this);
        if (editor.exec() == QDialog::Accepted && !editor.resultId().isEmpty()) {
            refillThemeCombo(theme_, false, editor.resultId());
        }
    });
    appForm->addRow(QString(), appCustomize);
    root->addWidget(appBox);

    auto* chatBox = new QGroupBox(QStringLiteral("Chat area - message view + input"), tab);
    auto* chatForm = new QFormLayout(chatBox);
    chatTheme_ = new QComboBox(chatBox);
    chatTheme_->setObjectName(QStringLiteral("chatTheme"));
    for (const ChatThemeDefinition& theme : chatThemes()) {
        chatTheme_->addItem(theme.label, theme.id);
    }
    setComboByData(
        chatTheme_,
        normalizeChatThemeId(
            settings_.value(QStringLiteral("chat_theme"), QStringLiteral("follow")).toString()));
    chatForm->addRow(QStringLiteral("Theme"), chatTheme_);
    auto* chatCustomize = new QPushButton(QStringLiteral("Customize..."), chatBox);
    chatCustomize->setObjectName(QStringLiteral("customizeChatTheme"));
    connect(chatCustomize, &QPushButton::clicked, this, [this]() {
        ThemeEditorDialog editor(ThemeEditorDialog::Scope::Chat,
                                 chatTheme_->currentData().toString(), this);
        if (editor.exec() == QDialog::Accepted && !editor.resultId().isEmpty()) {
            refillThemeCombo(chatTheme_, true, editor.resultId());
        }
    });
    chatForm->addRow(QString(), chatCustomize);
    root->addWidget(chatBox);

    auto* wallpaperBox = new QGroupBox(QStringLiteral("Wallpaper"), tab);
    auto* wallpaperForm = new QFormLayout(wallpaperBox);
    wallpaper_ = new QComboBox(wallpaperBox);
    wallpaper_->setObjectName(QStringLiteral("wallpaper"));
    for (const WallpaperDefinition& wallpaper : wallpaperChoices()) {
        wallpaper_->addItem(wallpaper.label, wallpaper.value);
    }
    setComboByData(wallpaper_, normalizeWallpaperValue(
                                   settings_.value(QStringLiteral("wallpaper")).toString()));
    wallpaperForm->addRow(QStringLiteral("Image"), wallpaper_);
    root->addWidget(wallpaperBox);
    root->addStretch(1);

    connect(appDefault, &QPushButton::clicked, this,
            [this]() { setComboByData(theme_, defaultThemeId()); });
    connect(appOff, &QPushButton::clicked, this,
            [this]() { setComboByData(theme_, systemThemeId()); });
}

void PreferencesDialog::buildMessagesTab(QWidget* tab) {
    auto* root = new QVBoxLayout(tab);
    auto* form = new QFormLayout();

    hideJoinPart_ = new QCheckBox(QString(), tab);
    hideJoinPart_->setObjectName(QStringLiteral("hideJoinPart"));
    hideJoinPart_->setChecked(settings_.value(QStringLiteral("hide_joinpart")).toBool());
    showMode_ = new QCheckBox(QString(), tab);
    showMode_->setObjectName(QStringLiteral("showMode"));
    showMode_->setChecked(settings_.value(QStringLiteral("show_mode"), true).toBool());
    pmEcho_ = new QCheckBox(QString(), tab);
    pmEcho_->setObjectName(QStringLiteral("pmEcho"));
    pmEcho_->setChecked(settings_.value(QStringLiteral("pm_echo"), true).toBool());
    loggingEnabled_ = new QCheckBox(QString(), tab);
    loggingEnabled_->setObjectName(QStringLiteral("loggingEnabled"));
    loggingEnabled_->setChecked(settings_.value(QStringLiteral("logging"), true).toBool());
    logMask_ = new QLineEdit(settings_.value(QStringLiteral("log_mask")).toString(), tab);
    logMask_->setObjectName(QStringLiteral("logMask"));
    logMask_->setPlaceholderText(
        QStringLiteral("%network-%channel  (+ strftime: %Y %m %d; / = subfolder)"));
    replayLogEnabled_ = new QCheckBox(QString(), tab);
    replayLogEnabled_->setObjectName(QStringLiteral("replayLogEnabled"));
    replayLogEnabled_->setChecked(settings_.value(QStringLiteral("replay_log"), true).toBool());
    replayLines_ = new QSpinBox(tab);
    replayLines_->setObjectName(QStringLiteral("replayLines"));
    replayLines_->setRange(0, 1000);
    replayLines_->setSpecialValueText(QStringLiteral("all"));
    replayLines_->setValue(settings_.value(QStringLiteral("replay_lines"), 0).toInt());
    autoReconnect_ = new QCheckBox(QString(), tab);
    autoReconnect_->setObjectName(QStringLiteral("autoReconnect"));
    autoReconnect_->setChecked(settings_.value(QStringLiteral("auto_reconnect"), true).toBool());
    autoRejoin_ = new QCheckBox(QString(), tab);
    autoRejoin_->setObjectName(QStringLiteral("autoRejoin"));
    autoRejoin_->setChecked(settings_.value(QStringLiteral("auto_rejoin"), false).toBool());
    hideVersion_ = new QCheckBox(QString(), tab);
    hideVersion_->setObjectName(QStringLiteral("hideVersion"));
    hideVersion_->setChecked(settings_.value(QStringLiteral("hide_version"), false).toBool());
    ctcpVersion_ = new QLineEdit(settings_.value(QStringLiteral("ctcp_version")).toString(), tab);
    ctcpVersion_->setObjectName(QStringLiteral("ctcpVersion"));
    ctcpVersion_->setPlaceholderText(
        QStringLiteral("custom CTCP VERSION reply (blank = MaxChat <version>)"));
    confirmQuit_ = new QCheckBox(QString(), tab);
    confirmQuit_->setObjectName(QStringLiteral("confirmQuit"));
    confirmQuit_->setChecked(settings_.value(QStringLiteral("confirm_quit"), true).toBool());
    scrollback_ = new QSpinBox(tab);
    scrollback_->setObjectName(QStringLiteral("scrollback"));
    scrollback_->setRange(100, 100000);
    scrollback_->setSingleStep(100);
    scrollback_->setValue(settings_.value(QStringLiteral("scrollback"), 2000).toInt());

    form->addRow(QStringLiteral("Hide join / part / quit"), hideJoinPart_);
    form->addRow(QStringLiteral("Show mode changes"), showMode_);
    form->addRow(QStringLiteral("Private msgs in server tab && chat"), pmEcho_);
    form->addRow(QStringLiteral("Log conversations to disk"), loggingEnabled_);
    form->addRow(QStringLiteral("Log filename mask"), logMask_);
    form->addRow(QStringLiteral("Replay last log on open"), replayLogEnabled_);
    form->addRow(QStringLiteral("Lines to replay"), replayLines_);
    form->addRow(QStringLiteral("Auto-reconnect on disconnect"), autoReconnect_);
    form->addRow(QStringLiteral("Auto-rejoin after kick"), autoRejoin_);
    form->addRow(QStringLiteral("Hide CTCP VERSION replies"), hideVersion_);
    form->addRow(QStringLiteral("Custom CTCP VERSION"), ctcpVersion_);
    form->addRow(QStringLiteral("Confirm before quitting"), confirmQuit_);
    form->addRow(QStringLiteral("Scrollback (lines)"), scrollback_);
    root->addLayout(form);
    root->addStretch(1);
}

void PreferencesDialog::buildLayoutTab(QWidget* tab) {
    auto* root = new QVBoxLayout(tab);

    serverListVisible_ = new QCheckBox(QStringLiteral("Show server list on startup"), tab);
    serverListVisible_->setObjectName(QStringLiteral("serverListVisible"));
    serverListVisible_->setChecked(
        settings_.value(QStringLiteral("server_list_visible"), true).toBool());

    memberListVisible_ = new QCheckBox(QStringLiteral("Show member list on startup"), tab);
    memberListVisible_->setObjectName(QStringLiteral("memberListVisible"));
    memberListVisible_->setChecked(
        settings_.value(QStringLiteral("member_list_visible"), true).toBool());

    buttonsAsTabs_ = new QCheckBox(QStringLiteral("Show buttons as tabs on startup"), tab);
    buttonsAsTabs_->setObjectName(QStringLiteral("buttonsAsTabs"));
    buttonsAsTabs_->setChecked(settings_.value(QStringLiteral("buffer_tabs"), false).toBool());

    connectOnStart_ = new QCheckBox(QStringLiteral("Auto-connect on startup"), tab);
    connectOnStart_->setObjectName(QStringLiteral("connectOnStart"));
    connectOnStart_->setChecked(
        settings_.value(QStringLiteral("connect_on_start"), false).toBool());

    root->addWidget(serverListVisible_);
    root->addWidget(memberListVisible_);
    root->addWidget(buttonsAsTabs_);
    root->addWidget(connectOnStart_);
    root->addStretch(1);
}

void PreferencesDialog::buildProtectionTab(QWidget* tab) {
    auto* root = new QVBoxLayout(tab);
    auto* form = new QFormLayout();

    floodProtect_ = new QCheckBox(QString(), tab);
    floodProtect_->setObjectName(QStringLiteral("floodProtect"));
    floodProtect_->setChecked(settings_.value(QStringLiteral("flood_protect"), false).toBool());
    floodMessages_ = new QSpinBox(tab);
    floodMessages_->setObjectName(QStringLiteral("floodMessages"));
    floodMessages_->setRange(2, 50);
    floodMessages_->setValue(settings_.value(QStringLiteral("flood_msgs"), 10).toInt());
    floodSeconds_ = new QSpinBox(tab);
    floodSeconds_->setObjectName(QStringLiteral("floodSeconds"));
    floodSeconds_->setRange(1, 60);
    floodSeconds_->setSuffix(QStringLiteral(" s"));
    floodSeconds_->setValue(settings_.value(QStringLiteral("flood_secs"), 4).toInt());
    pasteGuard_ = new QCheckBox(QString(), tab);
    pasteGuard_->setObjectName(QStringLiteral("pasteGuard"));
    pasteGuard_->setChecked(settings_.value(QStringLiteral("paste_guard"), true).toBool());
    pasteLines_ = new QSpinBox(tab);
    pasteLines_->setObjectName(QStringLiteral("pasteLines"));
    pasteLines_->setRange(2, 100);
    pasteLines_->setValue(settings_.value(QStringLiteral("paste_lines"), 4).toInt());
    inviteProtect_ = new QCheckBox(QString(), tab);
    inviteProtect_->setObjectName(QStringLiteral("inviteProtect"));
    inviteProtect_->setChecked(settings_.value(QStringLiteral("invite_protect"), true).toBool());
    ignoreInvites_ = new QCheckBox(QString(), tab);
    ignoreInvites_->setObjectName(QStringLiteral("ignoreInvites"));
    ignoreInvites_->setChecked(settings_.value(QStringLiteral("ignore_invites"), false).toBool());

    form->addRow(QStringLiteral("Flood protection (auto-ignore)"), floodProtect_);
    form->addRow(QStringLiteral("Flood: max messages"), floodMessages_);
    form->addRow(QStringLiteral("Flood: within"), floodSeconds_);
    form->addRow(QStringLiteral("Large-paste guard"), pasteGuard_);
    form->addRow(QStringLiteral("Paste guard: lines"), pasteLines_);
    form->addRow(QStringLiteral("Auto-ignore invite spam"), inviteProtect_);
    form->addRow(QStringLiteral("Ignore all channel invites"), ignoreInvites_);
    root->addLayout(form);

    auto* hint = new QLabel(
        QStringLiteral("Flood / invite-spam protection adds the offender to your ignore list "
                       "automatically (friends are never auto-ignored). The paste guard confirms "
                       "and throttles large pastes you send."),
        tab);
    hint->setWordWrap(true);
    root->addWidget(hint);
    root->addStretch(1);
}

void PreferencesDialog::buildFilesTab(QWidget* tab) {
    auto* root = new QVBoxLayout(tab);
    auto* form = new QFormLayout();

    dccAccept_ = new QComboBox(tab);
    dccAccept_->setObjectName(QStringLiteral("dccAccept"));
    dccAccept_->addItem(QStringLiteral("Ask each time"), QStringLiteral("ask"));
    dccAccept_->addItem(QStringLiteral("Auto-accept trusted nicks"), QStringLiteral("trusted"));
    dccAccept_->addItem(QStringLiteral("Auto-accept everything (careful)"), QStringLiteral("all"));
    setComboByData(dccAccept_,
                   settings_.value(QStringLiteral("dcc_accept"), QStringLiteral("ask")).toString());
    dccTrusted_ = new QLineEdit(
        settings_.value(QStringLiteral("dcc_trusted")).toStringList().join(QStringLiteral(", ")),
        tab);
    dccTrusted_->setObjectName(QStringLiteral("dccTrusted"));
    dccTrusted_->setPlaceholderText(
        QString::fromUtf8("alice, bob \xE2\x80\x94 auto-accepted when \xE2\x80\x9Ctrusted\xE2\x80\x9D"));
    dccPassive_ = new QCheckBox(QString(), tab);
    dccPassive_->setObjectName(QStringLiteral("dccPassive"));
    dccPassive_->setChecked(settings_.value(QStringLiteral("dcc_passive"), true).toBool());
    dccIp_ = new QLineEdit(settings_.value(QStringLiteral("dcc_ip")).toString(), tab);
    dccIp_->setObjectName(QStringLiteral("dccIp"));
    dccIp_->setPlaceholderText(QString::fromUtf8("auto \xE2\x80\x94 your connection's IP"));
    dccPortFirst_ = new QSpinBox(tab);
    dccPortFirst_->setObjectName(QStringLiteral("dccPortFirst"));
    dccPortFirst_->setRange(0, 65535);
    dccPortFirst_->setSpecialValueText(QStringLiteral("any"));
    dccPortFirst_->setValue(settings_.value(QStringLiteral("dcc_port_first"), 0).toInt());
    dccPortLast_ = new QSpinBox(tab);
    dccPortLast_->setObjectName(QStringLiteral("dccPortLast"));
    dccPortLast_->setRange(0, 65535);
    dccPortLast_->setSpecialValueText(QStringLiteral("any"));
    dccPortLast_->setValue(settings_.value(QStringLiteral("dcc_port_last"), 0).toInt());

    dccDir_ = new QLineEdit(settings_.value(QStringLiteral("dcc_dir")).toString(), tab);
    dccDir_->setObjectName(QStringLiteral("dccDir"));
    dccDir_->setPlaceholderText(QStringLiteral("<config>/downloads"));
    auto* browse = new QPushButton(QStringLiteral("Browse..."), tab);
    connect(browse, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(
            this, QStringLiteral("Choose download folder"), dccDir_->text());
        if (!dir.isEmpty()) {
            dccDir_->setText(dir);
        }
    });
    auto* dirRow = new QWidget(tab);
    auto* dirLayout = new QHBoxLayout(dirRow);
    dirLayout->setContentsMargins(0, 0, 0, 0);
    dirLayout->addWidget(dccDir_, 1);
    dirLayout->addWidget(browse);

    form->addRow(QStringLiteral("Incoming files"), dccAccept_);
    form->addRow(QStringLiteral("Trusted nicks (auto-accept)"), dccTrusted_);
    form->addRow(QStringLiteral("Passive / reverse DCC (NAT-friendly)"), dccPassive_);
    form->addRow(QStringLiteral("Your DCC IP (for active DCC)"), dccIp_);
    form->addRow(QStringLiteral("Listen port from"), dccPortFirst_);
    form->addRow(QStringLiteral("Listen port to"), dccPortLast_);
    form->addRow(QStringLiteral("Download folder"), dirRow);
    root->addLayout(form);

    auto* hint = new QLabel(
        QStringLiteral("Passive DCC asks the OTHER side to open the port - best when you're "
                       "behind NAT and they aren't. For active DCC (you open the port) behind "
                       "NAT, set your public IP + a forwarded port range here."),
        tab);
    hint->setWordWrap(true);
    root->addWidget(hint);
    root->addStretch(1);
}

void PreferencesDialog::buildServicesTab(QWidget* tab) {
    auto* root = new QVBoxLayout(tab);
    const QVariantMap services = contentServicesFromSettings(settings_);
    linkImages_ = new QCheckBox(QStringLiteral("Image previews"), tab);
    linkImages_->setObjectName(QStringLiteral("linkImages"));
    linkImages_->setChecked(services.value(QStringLiteral("images")).toBool());
    linkMedia_ = new QCheckBox(QStringLiteral("Audio/video previews"), tab);
    linkMedia_->setObjectName(QStringLiteral("linkMedia"));
    linkMedia_->setChecked(services.value(QStringLiteral("media")).toBool());
    linkXCards_ = new QCheckBox(QStringLiteral("X / Twitter cards"), tab);
    linkXCards_->setObjectName(QStringLiteral("linkXCards"));
    linkXCards_->setChecked(services.value(QStringLiteral("xcards")).toBool());
    linkWebCards_ = new QCheckBox(QStringLiteral("Website cards"), tab);
    linkWebCards_->setObjectName(QStringLiteral("linkWebCards"));
    linkWebCards_->setChecked(services.value(QStringLiteral("webcards")).toBool());
    root->addWidget(linkImages_);
    root->addWidget(linkMedia_);
    root->addWidget(linkXCards_);
    root->addWidget(linkWebCards_);
    root->addStretch(1);
}

void PreferencesDialog::buildLocalizationTab(QWidget* tab) {
    auto* root = new QVBoxLayout(tab);
    auto* form = new QFormLayout();

    interfaceLanguage_ = new QComboBox(tab);
    interfaceLanguage_->setObjectName(QStringLiteral("interfaceLanguage"));
    interfaceLanguage_->addItem(QStringLiteral("System default"), QStringLiteral("system"));
    interfaceLanguage_->addItem(QStringLiteral("English"), QStringLiteral("en"));
    interfaceLanguage_->addItem(QStringLiteral("Arabic"), QStringLiteral("ar"));
    interfaceLanguage_->addItem(QStringLiteral("German"), QStringLiteral("de"));
    interfaceLanguage_->addItem(QStringLiteral("Spanish"), QStringLiteral("es"));
    interfaceLanguage_->addItem(QStringLiteral("French"), QStringLiteral("fr"));
    interfaceLanguage_->addItem(QStringLiteral("Hindi"), QStringLiteral("hi"));
    interfaceLanguage_->addItem(QStringLiteral("Italian"), QStringLiteral("it"));
    interfaceLanguage_->addItem(QStringLiteral("Japanese"), QStringLiteral("ja"));
    interfaceLanguage_->addItem(QStringLiteral("Korean"), QStringLiteral("ko"));
    interfaceLanguage_->addItem(QStringLiteral("Dutch"), QStringLiteral("nl"));
    interfaceLanguage_->addItem(QStringLiteral("Polish"), QStringLiteral("pl"));
    interfaceLanguage_->addItem(QStringLiteral("Portuguese (Brazil)"), QStringLiteral("pt_BR"));
    interfaceLanguage_->addItem(QStringLiteral("Russian"), QStringLiteral("ru"));
    interfaceLanguage_->addItem(QStringLiteral("Turkish"), QStringLiteral("tr"));
    interfaceLanguage_->addItem(QStringLiteral("Ukrainian"), QStringLiteral("uk"));
    interfaceLanguage_->addItem(QStringLiteral("Chinese (Simplified)"), QStringLiteral("zh_CN"));
    interfaceLanguage_->addItem(QStringLiteral("Chinese (Traditional)"), QStringLiteral("zh_TW"));
    setComboByData(interfaceLanguage_,
                   settings_.value(QStringLiteral("interface_language"), QStringLiteral("system")));

    spellcheckEnabled_ = new QCheckBox(QStringLiteral("Spellcheck"), tab);
    spellcheckEnabled_->setObjectName(QStringLiteral("spellcheckEnabled"));
    spellcheckEnabled_->setChecked(settings_.value(QStringLiteral("spellcheck_enabled")).toBool());

    spellLanguage_ = new QComboBox(tab);
    spellLanguage_->setObjectName(QStringLiteral("spellLanguage"));
    const QList<maxchat::spell::SpellcheckLanguage> spellLanguages =
        maxchat::spell::spellcheckLanguages();
    for (const maxchat::spell::SpellcheckLanguage& language : spellLanguages) {
        spellLanguage_->addItem(language.displayLabel(), language.code);
    }
    setComboByData(spellLanguage_,
                   settings_.value(QStringLiteral("spell_language"), QStringLiteral("en")));

    form->addRow(QStringLiteral("Interface Language"), interfaceLanguage_);
    form->addRow(QString(), spellcheckEnabled_);
    form->addRow(QStringLiteral("Spell Language"), spellLanguage_);
    root->addLayout(form);
    root->addStretch(1);
}

void PreferencesDialog::buildDataTab(QWidget* tab) {
    auto* root = new QVBoxLayout(tab);
    const maxchat::core::SettingsPaths paths = maxchat::core::standardSettingsPaths();
    const QString logsDir = QDir(paths.configDir).filePath(QStringLiteral("logs"));
    const QString dccDirPref = settings_.value(QStringLiteral("dcc_dir")).toString().trimmed();
    const QString downloadsDir = dccDirPref.isEmpty()
                                     ? QDir(paths.configDir).filePath(QStringLiteral("downloads"))
                                     : dccDirPref;
    const QString scriptsDir = QDir(paths.configDir).filePath(QStringLiteral("scripts"));

    const auto dirRow = [tab](const QString& path) -> QWidget* {
        auto* row = new QWidget(tab);
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        auto* edit = new QLineEdit(QDir::toNativeSeparators(path), row);
        edit->setReadOnly(true);
        auto* open = new QPushButton(QStringLiteral("Open"), row);
        connect(open, &QPushButton::clicked, row, [path]() {
            QDir().mkpath(path);
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        });
        layout->addWidget(edit, 1);
        layout->addWidget(open);
        return row;
    };

    auto* foldersBox = new QGroupBox(QStringLiteral("Folders"), tab);
    auto* foldersForm = new QFormLayout(foldersBox);
    foldersForm->addRow(QStringLiteral("Config"), dirRow(paths.configDir));
    foldersForm->addRow(QStringLiteral("Cache"), dirRow(paths.cacheDir));
    foldersForm->addRow(QStringLiteral("Logs"), dirRow(logsDir));
    foldersForm->addRow(QStringLiteral("Downloads"), dirRow(downloadsDir));
    foldersForm->addRow(QStringLiteral("Scripts"), dirRow(scriptsDir));
    root->addWidget(foldersBox);

    auto* statsBox = new QGroupBox(QStringLiteral("Storage && stats"), tab);
    auto* statsForm = new QFormLayout(statsBox);
    statsForm->addRow(QStringLiteral("Config size"),
                      new QLabel(humanBytes(directorySizeBytes(paths.configDir)), tab));
    statsForm->addRow(QStringLiteral("Cache size"),
                      new QLabel(humanBytes(directorySizeBytes(paths.cacheDir)), tab));
    statsForm->addRow(QStringLiteral("Logs size"),
                      new QLabel(humanBytes(directorySizeBytes(logsDir)), tab));
    statsForm->addRow(
        QStringLiteral("Saved networks"),
        new QLabel(QString::number(settings_.value(QStringLiteral("networks")).toList().size()),
                   tab));
    statsForm->addRow(QStringLiteral("Ignored masks"),
                      new QLabel(QString::number(settings_.value(QStringLiteral("ignores"))
                                                     .toStringList()
                                                     .size()),
                                 tab));
    root->addWidget(statsBox);

    auto* configBox = new QGroupBox(QStringLiteral("Configuration"), tab);
    auto* configRow = new QHBoxLayout(configBox);
    auto* exportSettings = new QPushButton(QStringLiteral("Export..."), tab);
    exportSettings->setObjectName(QStringLiteral("exportSettings"));
    auto* importSettings = new QPushButton(QStringLiteral("Import..."), tab);
    importSettings->setObjectName(QStringLiteral("importSettings"));
    auto* resetServers = new QPushButton(QStringLiteral("Reset Server List..."), tab);
    resetServers->setObjectName(QStringLiteral("resetServerList"));
    auto* resetAll = new QPushButton(QStringLiteral("Reset to Defaults..."), tab);
    resetAll->setObjectName(QStringLiteral("resetAllSettings"));
    configRow->addWidget(exportSettings);
    configRow->addWidget(importSettings);
    configRow->addWidget(resetServers);
    configRow->addWidget(resetAll);
    configRow->addStretch(1);
    root->addWidget(configBox);
    root->addStretch(1);

    connect(exportSettings, &QPushButton::clicked, this,
            &PreferencesDialog::exportSettingsRequested);
    connect(importSettings, &QPushButton::clicked, this,
            &PreferencesDialog::importSettingsRequested);
    connect(resetServers, &QPushButton::clicked, this,
            &PreferencesDialog::resetServerListRequested);
    connect(resetAll, &QPushButton::clicked, this,
            &PreferencesDialog::resetAllSettingsRequested);
}

} // namespace maxchat::ui
