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
#include <QFontDatabase>
#include <QFormLayout>
#include <QLabel>
#include <QUrl>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
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
    resize(860, 640);

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
    out.insert(QStringLiteral("theme"), theme_->currentData().toString());
    out.insert(QStringLiteral("chat_theme"), chatTheme_->currentData().toString());
    out.insert(QStringLiteral("wallpaper"), wallpaper_->currentData().toString());
    out.insert(QStringLiteral("app_font_family"), appFontFamily_->text().trimmed());
    out.insert(QStringLiteral("app_font_size"), appFontSize_->value());
    out.insert(QStringLiteral("app_font_bold"), appFontBold_->isChecked());
    out.insert(QStringLiteral("chat_font_family"), chatFontFamily_->text().trimmed());
    out.insert(QStringLiteral("chat_font_size"), chatFontSize_->value());
    out.insert(QStringLiteral("chat_font_bold"), chatFontBold_->isChecked());
    out.insert(QStringLiteral("list_font_family"), listFontFamily_->text().trimmed());
    out.insert(QStringLiteral("list_font_size"), listFontSize_->value());
    out.insert(QStringLiteral("list_font_bold"), listFontBold_->isChecked());
    out.insert(QStringLiteral("chat_text_color"), chatTextColor_->value());
    out.insert(QStringLiteral("event_color"), eventColor_->value());
    out.insert(QStringLiteral("tree_color"), treeColor_->value());
    out.insert(QStringLiteral("userlist_color"), userlistColor_->value());
    out.insert(QStringLiteral("nick_label_color"), nickLabelColor_->value());
    out.insert(QStringLiteral("status_text_color"), statusColor_->value());
    out.insert(QStringLiteral("topic_color"), topicColor_->value());
    out.insert(QStringLiteral("nick_font_family"), chatFontFamily_->text().trimmed());
    out.insert(QStringLiteral("nick_font_size"), chatFontSize_->value());
    out.insert(QStringLiteral("nick_font_bold"), chatFontBold_->isChecked());
    out.insert(QStringLiteral("status_font_family"), appFontFamily_->text().trimmed());
    out.insert(QStringLiteral("status_font_size"), appFontSize_->value());
    out.insert(QStringLiteral("status_font_bold"), appFontBold_->isChecked());
    out.insert(QStringLiteral("topic_font_family"), appFontFamily_->text().trimmed());
    out.insert(QStringLiteral("topic_font_size"), appFontSize_->value());
    out.insert(QStringLiteral("topic_font_bold"), appFontBold_->isChecked());
    out.insert(QStringLiteral("show_timestamps"), showTimestamps_->isChecked());
    out.insert(QStringLiteral("timestamp_format"), timestampFormat_->text().trimmed());
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
    out.insert(QStringLiteral("log_mask"), logMask_->text().trimmed());
    out.insert(QStringLiteral("replay_lines"), replayLines_->value());
    out.insert(QStringLiteral("show_input_hint"), inputHint_->isChecked());
    out.insert(QStringLiteral("strip_color_copy"), stripColorCopy_->isChecked());
    out.insert(QStringLiteral("sort_users_by_status"), sortByStatus_->isChecked());
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
    out.insert(QStringLiteral("logging"), loggingEnabled_->isChecked());
    out.insert(QStringLiteral("replay_log"), replayLogEnabled_->isChecked());
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
    appFontFamily_->setText(family);
    chatFontFamily_->setText(family);
    appFontSize_->setValue(size);
    chatFontSize_->setValue(size);
    appFontBold_->setChecked(bold);
    chatFontBold_->setChecked(bold);
    if (listFontFamily_ != nullptr) {
        listFontFamily_->setText(family);
        listFontSize_->setValue(size);
        listFontBold_->setChecked(bold);
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
    timestampFormat_ = new QLineEdit(
        settings_.value(QStringLiteral("timestamp_format"), QStringLiteral("%I:%M %p")).toString(),
        tab);
    timestampFormat_->setObjectName(QStringLiteral("timestampFormat"));
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
    nickWidth_->setRange(4, 40);
    nickWidth_->setSuffix(QStringLiteral(" chars"));
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
    auto* textForm = new QVBoxLayout(textBox);
    showFormatting_ = new QCheckBox(QStringLiteral("Show colors && formatting"), tab);
    showFormatting_->setObjectName(QStringLiteral("showFormatting"));
    showFormatting_->setChecked(settings_.value(QStringLiteral("show_formatting"), true).toBool());
    wordWrap_ = new QCheckBox(QStringLiteral("Word wrap"), tab);
    wordWrap_->setObjectName(QStringLiteral("wordWrap"));
    wordWrap_->setChecked(settings_.value(QStringLiteral("word_wrap")).toBool());
    indentWrap_ = new QCheckBox(QStringLiteral("Indent wrapped lines"), tab);
    indentWrap_->setObjectName(QStringLiteral("indentWrap"));
    indentWrap_->setChecked(settings_.value(QStringLiteral("indent_wrap"), true).toBool());
    stripColorCopy_ = new QCheckBox(QStringLiteral("Strip colors on copy"), tab);
    stripColorCopy_->setObjectName(QStringLiteral("stripColorCopy"));
    stripColorCopy_->setChecked(settings_.value(QStringLiteral("strip_color_copy"), true).toBool());
    textForm->addWidget(showFormatting_);
    textForm->addWidget(wordWrap_);
    textForm->addWidget(indentWrap_);
    textForm->addWidget(stripColorCopy_);
    rightColumn->addWidget(textBox);

    auto* windowBox = new QGroupBox(QStringLiteral("Window"), tab);
    auto* windowForm = new QFormLayout(windowBox);
    markerLine_ = new QCheckBox(QStringLiteral("Unread marker line"), tab);
    markerLine_->setObjectName(QStringLiteral("markerLine"));
    markerLine_->setChecked(settings_.value(QStringLiteral("marker_line"), true).toBool());
    windowForm->addRow(QString(), markerLine_);
    inputHint_ = new QCheckBox(QStringLiteral("Message-box hint text"), tab);
    inputHint_->setObjectName(QStringLiteral("inputHint"));
    inputHint_->setChecked(settings_.value(QStringLiteral("show_input_hint"), true).toBool());
    windowForm->addRow(QString(), inputHint_);
    sortByStatus_ = new QCheckBox(QStringLiteral("Sort users by status"), tab);
    sortByStatus_->setObjectName(QStringLiteral("sortByStatus"));
    sortByStatus_->setChecked(settings_.value(QStringLiteral("sort_users_by_status"), true).toBool());
    windowForm->addRow(QString(), sortByStatus_);
    trayIcon_ = new QComboBox(tab);
    trayIcon_->setObjectName(QStringLiteral("trayIcon"));
    const QColor iconAccent = appThemeById(
        settings_.value(QStringLiteral("theme"), QStringLiteral("dark")).toString()).on;
    const QList<QPair<QString, QString>> iconChoices = {
        {QStringLiteral("bubble"), QStringLiteral("Speech bubble (theme color)")},
        {QString::fromUtf8("\xF0\x9F\x92\xAC"), QStringLiteral("Speech balloon")},
        {QString::fromUtf8("\xF0\x9F\x92\xAD"), QStringLiteral("Thought bubble")},
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
    windowForm->addRow(QStringLiteral("Window / tray icon"), trayIcon_);
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
        QStringLiteral("Comic Mode is planned for this port. Character, emotion, and "
                       "panel options will move here from the Python app."),
        tab);
    note->setWordWrap(true);
    root->addWidget(note);
    root->addWidget(plannedCheckBox(tab, QStringLiteral("Comic Mode")));
    root->addWidget(plannedCheckBox(tab, QStringLiteral("Character picker")));
    root->addWidget(plannedCheckBox(tab, QStringLiteral("Comic name labels")));
    root->addStretch(1);
}

void PreferencesDialog::buildFontsTab(QWidget* tab) {
    auto* root = new QVBoxLayout(tab);
    auto* form = new QFormLayout();

    appFontFamily_ =
        new QLineEdit(settings_.value(QStringLiteral("app_font_family")).toString(), tab);
    appFontFamily_->setObjectName(QStringLiteral("appFontFamily"));
    appFontSize_ = fontSizeSpinBox(tab, settings_.value(QStringLiteral("app_font_size")).toInt());
    appFontSize_->setObjectName(QStringLiteral("appFontSize"));
    appFontBold_ = new QCheckBox(QStringLiteral("Bold"), tab);
    appFontBold_->setObjectName(QStringLiteral("appFontBold"));
    appFontBold_->setChecked(settings_.value(QStringLiteral("app_font_bold")).toBool());

    chatFontFamily_ =
        new QLineEdit(settings_.value(QStringLiteral("chat_font_family")).toString(), tab);
    chatFontFamily_->setObjectName(QStringLiteral("chatFontFamily"));
    chatFontSize_ = fontSizeSpinBox(tab, settings_.value(QStringLiteral("chat_font_size")).toInt());
    chatFontSize_->setObjectName(QStringLiteral("chatFontSize"));
    chatFontBold_ = new QCheckBox(QStringLiteral("Bold"), tab);
    chatFontBold_->setObjectName(QStringLiteral("chatFontBold"));
    chatFontBold_->setChecked(settings_.value(QStringLiteral("chat_font_bold")).toBool());

    listFontFamily_ =
        new QLineEdit(settings_.value(QStringLiteral("list_font_family")).toString(), tab);
    listFontFamily_->setObjectName(QStringLiteral("listFontFamily"));
    listFontSize_ = fontSizeSpinBox(tab, settings_.value(QStringLiteral("list_font_size")).toInt());
    listFontSize_->setObjectName(QStringLiteral("listFontSize"));
    listFontBold_ = new QCheckBox(QStringLiteral("Bold"), tab);
    listFontBold_->setObjectName(QStringLiteral("listFontBold"));
    listFontBold_->setChecked(settings_.value(QStringLiteral("list_font_bold")).toBool());

    chatTextColor_ =
        new ColorPick(settings_.value(QStringLiteral("chat_text_color")).toString(), tab);
    eventColor_ = new ColorPick(settings_.value(QStringLiteral("event_color")).toString(), tab);
    treeColor_ = new ColorPick(settings_.value(QStringLiteral("tree_color")).toString(), tab);
    userlistColor_ =
        new ColorPick(settings_.value(QStringLiteral("userlist_color")).toString(), tab);
    nickLabelColor_ =
        new ColorPick(settings_.value(QStringLiteral("nick_label_color")).toString(), tab);
    statusColor_ =
        new ColorPick(settings_.value(QStringLiteral("status_text_color")).toString(), tab);
    topicColor_ = new ColorPick(settings_.value(QStringLiteral("topic_color")).toString(), tab);

    form->addRow(QStringLiteral("App Font"), appFontFamily_);
    form->addRow(QStringLiteral("App Size"), appFontSize_);
    form->addRow(QString(), appFontBold_);
    form->addRow(QStringLiteral("Chat Font"), chatFontFamily_);
    form->addRow(QStringLiteral("Chat Size"), chatFontSize_);
    form->addRow(QString(), chatFontBold_);
    form->addRow(QStringLiteral("Chat Text Color"), chatTextColor_);
    form->addRow(QStringLiteral("Event Lines Color"), eventColor_);
    form->addRow(QStringLiteral("List Font"), listFontFamily_);
    form->addRow(QStringLiteral("List Size"), listFontSize_);
    form->addRow(QString(), listFontBold_);
    form->addRow(QStringLiteral("Tree Color"), treeColor_);
    form->addRow(QStringLiteral("Users Color"), userlistColor_);
    form->addRow(QStringLiteral("Nick Label Color"), nickLabelColor_);
    form->addRow(QStringLiteral("Status Bar Color"), statusColor_);
    form->addRow(QStringLiteral("Topic Color"), topicColor_);
    root->addLayout(form);

    auto* fontButtons = new QHBoxLayout();
    auto* jetbrains = new QPushButton(QStringLiteral("Set All to JetBrains Mono"), tab);
    jetbrains->setObjectName(QStringLiteral("setAllJetBrains"));
    auto* system = new QPushButton(QStringLiteral("System Default"), tab);
    system->setObjectName(QStringLiteral("setAllSystem"));
    fontButtons->addWidget(jetbrains);
    fontButtons->addWidget(system);
    fontButtons->addStretch(1);
    root->addLayout(fontButtons);

    auto* note = new QLabel(
        QStringLiteral("Per-area fonts and colors (channel tree, user list, nick label, "
                       "status bar, topic) are planned and will move here from the "
                       "Python app."),
        tab);
    note->setWordWrap(true);
    root->addWidget(note);
    root->addStretch(1);

    connect(jetbrains, &QPushButton::clicked, this,
            [this]() { setAllFonts(QStringLiteral("JetBrains Mono"), 14, true); });
    connect(system, &QPushButton::clicked, this, [this]() {
        const QFont systemFont = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
        setAllFonts(systemFont.family(), systemFont.pointSize() > 0 ? systemFont.pointSize() : 10,
                    false);
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
    hideJoinPart_ = new QCheckBox(QStringLiteral("Hide joins, parts, and quits"), tab);
    hideJoinPart_->setObjectName(QStringLiteral("hideJoinPart"));
    hideJoinPart_->setChecked(settings_.value(QStringLiteral("hide_joinpart")).toBool());
    root->addWidget(hideJoinPart_);
    showMode_ = new QCheckBox(QStringLiteral("Show mode changes"), tab);
    showMode_->setObjectName(QStringLiteral("showMode"));
    showMode_->setChecked(settings_.value(QStringLiteral("show_mode"), true).toBool());
    root->addWidget(showMode_);
    pmEcho_ = new QCheckBox(QStringLiteral("Echo private messages you send"), tab);
    pmEcho_->setObjectName(QStringLiteral("pmEcho"));
    pmEcho_->setChecked(settings_.value(QStringLiteral("pm_echo"), true).toBool());
    root->addWidget(pmEcho_);
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

    buttonBarVisible_ = new QCheckBox(QStringLiteral("Show button bar on startup"), tab);
    buttonBarVisible_->setObjectName(QStringLiteral("buttonBarVisible"));
    buttonBarVisible_->setChecked(
        settings_.value(QStringLiteral("show_button_bar"), true).toBool());

    buttonsAsTabs_ = new QCheckBox(QStringLiteral("Show buttons as tabs on startup"), tab);
    buttonsAsTabs_->setObjectName(QStringLiteral("buttonsAsTabs"));
    buttonsAsTabs_->setChecked(settings_.value(QStringLiteral("buffer_tabs"), false).toBool());

    connectOnStart_ = new QCheckBox(QStringLiteral("Auto-connect on startup"), tab);
    connectOnStart_->setObjectName(QStringLiteral("connectOnStart"));
    connectOnStart_->setChecked(
        settings_.value(QStringLiteral("connect_on_start"), false).toBool());

    root->addWidget(serverListVisible_);
    root->addWidget(memberListVisible_);
    root->addWidget(buttonBarVisible_);
    root->addWidget(buttonsAsTabs_);
    root->addWidget(connectOnStart_);
    root->addStretch(1);
}

void PreferencesDialog::buildProtectionTab(QWidget* tab) {
    auto* root = new QVBoxLayout(tab);
    auto* form = new QFormLayout();

    autoReconnect_ = new QCheckBox(QStringLiteral("Auto-reconnect on disconnect"), tab);
    autoReconnect_->setObjectName(QStringLiteral("autoReconnect"));
    autoReconnect_->setChecked(settings_.value(QStringLiteral("auto_reconnect"), true).toBool());

    floodProtect_ = new QCheckBox(QStringLiteral("Auto-ignore repeated messages"), tab);
    floodProtect_->setObjectName(QStringLiteral("floodProtect"));
    floodProtect_->setChecked(settings_.value(QStringLiteral("flood_protect"), false).toBool());

    floodMessages_ = new QSpinBox(tab);
    floodMessages_->setObjectName(QStringLiteral("floodMessages"));
    floodMessages_->setRange(2, 50);
    floodMessages_->setSuffix(QStringLiteral(" messages"));
    floodMessages_->setValue(settings_.value(QStringLiteral("flood_msgs"), 10).toInt());

    floodSeconds_ = new QSpinBox(tab);
    floodSeconds_->setObjectName(QStringLiteral("floodSeconds"));
    floodSeconds_->setRange(1, 60);
    floodSeconds_->setSuffix(QStringLiteral(" s"));
    floodSeconds_->setValue(settings_.value(QStringLiteral("flood_secs"), 4).toInt());

    form->addRow(QString(), autoReconnect_);
    form->addRow(QString(), floodProtect_);
    form->addRow(QStringLiteral("Threshold"), floodMessages_);
    form->addRow(QStringLiteral("Window"), floodSeconds_);
    root->addLayout(form);
    root->addStretch(1);
}

void PreferencesDialog::buildFilesTab(QWidget* tab) {
    auto* root = new QVBoxLayout(tab);
    loggingEnabled_ = new QCheckBox(QStringLiteral("Log chats"), tab);
    loggingEnabled_->setObjectName(QStringLiteral("loggingEnabled"));
    loggingEnabled_->setChecked(settings_.value(QStringLiteral("logging"), true).toBool());
    replayLogEnabled_ = new QCheckBox(QStringLiteral("Replay recent log on connect"), tab);
    replayLogEnabled_->setObjectName(QStringLiteral("replayLogEnabled"));
    replayLogEnabled_->setChecked(settings_.value(QStringLiteral("replay_log"), true).toBool());
    root->addWidget(loggingEnabled_);
    root->addWidget(replayLogEnabled_);

    auto* form = new QFormLayout();
    logMask_ = new QLineEdit(settings_.value(QStringLiteral("log_mask")).toString(), tab);
    logMask_->setObjectName(QStringLiteral("logMask"));
    logMask_->setPlaceholderText(
        QStringLiteral("%network/%channel/%Y-%m-%d  (/ = subfolder; date codes %Y %m %d)"));
    form->addRow(QStringLiteral("Log filename"), logMask_);
    replayLines_ = new QSpinBox(tab);
    replayLines_->setObjectName(QStringLiteral("replayLines"));
    replayLines_->setRange(0, 2000);
    replayLines_->setSuffix(QStringLiteral(" lines"));
    replayLines_->setSpecialValueText(QStringLiteral("Default (200)"));
    replayLines_->setValue(settings_.value(QStringLiteral("replay_lines"), 0).toInt());
    form->addRow(QStringLiteral("Replay amount"), replayLines_);
    root->addLayout(form);
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
    interfaceLanguage_->addItem(QStringLiteral("System"), QStringLiteral("system"));
    interfaceLanguage_->addItem(QStringLiteral("English"), QStringLiteral("en"));
    interfaceLanguage_->addItem(QStringLiteral("Spanish"), QStringLiteral("es"));
    interfaceLanguage_->addItem(QStringLiteral("French"), QStringLiteral("fr"));
    interfaceLanguage_->addItem(QStringLiteral("German"), QStringLiteral("de"));
    interfaceLanguage_->addItem(QStringLiteral("Portuguese"), QStringLiteral("pt"));
    interfaceLanguage_->addItem(QStringLiteral("Italian"), QStringLiteral("it"));
    interfaceLanguage_->addItem(QStringLiteral("Dutch"), QStringLiteral("nl"));
    interfaceLanguage_->addItem(QStringLiteral("Polish"), QStringLiteral("pl"));
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
    const QString downloadsDir = QDir(paths.configDir).filePath(QStringLiteral("downloads"));
    const QString scriptsDir = QDir(paths.configDir).filePath(QStringLiteral("scripts"));

    const auto dirRow = [tab](const QString& path) -> QWidget* {
        auto* row = new QWidget(tab);
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        auto* edit = new QLineEdit(QDir::toNativeSeparators(path), row);
        edit->setReadOnly(true);
        auto* open = new QPushButton(QStringLiteral("Open"), row);
        connect(open, &QPushButton::clicked, row, [path]() {
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
