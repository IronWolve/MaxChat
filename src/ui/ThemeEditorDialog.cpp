#include "ui/ThemeEditorDialog.h"

#include <QMessageBox>

#include "ui/ThemeCatalog.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <array>
#include <utility>

namespace maxchat::ui {

namespace {

struct Field {
    const char* key;
    const char* label;
};

// Mirrors the Python editor's APP_FIELDS / CHAT_FIELDS.
constexpr std::array<Field, 5> AppFields{{{"bg", "Window background"},
                                          {"panel", "Panels (tree / list)"},
                                          {"panel2", "Bars & headers"},
                                          {"text", "Text"},
                                          {"accent", "Accent / highlight"}}};

constexpr std::array<Field, 5> ChatFields{{{"bg", "Chat background"},
                                           {"fg", "Chat text"},
                                           {"ts", "Timestamp"},
                                           {"bracket", "Nick brackets"},
                                           {"system", "System / join-part lines"}}};

void styleSwatch(QPushButton* button, const QColor& color) {
    const bool light =
        0.299 * color.red() + 0.587 * color.green() + 0.114 * color.blue() > 140.0;
    button->setText(color.name());
    button->setStyleSheet(QStringLiteral("background:%1;color:%2;border:1px solid grey;padding:5px;")
                              .arg(color.name(), light ? QStringLiteral("black")
                                                       : QStringLiteral("white")));
}

} // namespace

ThemeEditorDialog::ThemeEditorDialog(Scope scope, const QString& baseId, QWidget* parent)
    : QDialog(parent), scope_(scope), baseId_(baseId) {
    setWindowTitle(scope == Scope::App ? QStringLiteral("Customize App Theme")
                                       : QStringLiteral("Customize Chat Theme"));

    auto* root = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    root->addLayout(form);

    const AppThemeDefinition appBase = appThemeById(baseId);
    const ChatThemeDefinition chatBase = chatThemeById(baseId);
    const QString baseLabel = scope == Scope::App ? appBase.label : chatBase.label;
    name_ = new QLineEdit(QStringLiteral("%1 (custom)").arg(baseLabel), this);
    form->addRow(tr("Name"), name_);

    // Seed editable colors from the base theme.
    if (scope == Scope::App) {
        colors_.insert(QStringLiteral("bg"), appBase.bg);
        colors_.insert(QStringLiteral("panel"), appBase.panel);
        colors_.insert(QStringLiteral("panel2"), appBase.panel2);
        colors_.insert(QStringLiteral("text"), appBase.text);
        colors_.insert(QStringLiteral("accent"), appBase.accent);
    } else {
        const QColor fg = chatBase.fg.isValid() ? chatBase.fg : QColor(208, 208, 208);
        const QColor bg = chatBase.bg.isValid() ? chatBase.bg : QColor(0, 0, 0);
        colors_.insert(QStringLiteral("bg"), bg);
        colors_.insert(QStringLiteral("fg"), fg);
        colors_.insert(QStringLiteral("ts"), chatBase.timestamp.isValid() ? chatBase.timestamp : fg);
        colors_.insert(QStringLiteral("bracket"),
                       chatBase.bracket.isValid() ? chatBase.bracket : fg);
        colors_.insert(QStringLiteral("system"), chatBase.system.isValid() ? chatBase.system : fg);
    }

    const auto& fields = scope == Scope::App
                             ? std::vector<Field>(AppFields.begin(), AppFields.end())
                             : std::vector<Field>(ChatFields.begin(), ChatFields.end());
    for (const Field& field : fields) {
        const QString key = QString::fromLatin1(field.key);
        auto* swatch = new QPushButton(this);
        swatch->setMinimumWidth(130);
        styleSwatch(swatch, colors_.value(key));
        connect(swatch, &QPushButton::clicked, this, [this, key, swatch]() {
            const QColor chosen = QColorDialog::getColor(colors_.value(key), this,
                                                         QStringLiteral("Pick a color"));
            if (chosen.isValid()) {
                colors_.insert(key, chosen);
                styleSwatch(swatch, chosen);
            }
        });
        form->addRow(QString::fromLatin1(field.label), swatch);
    }

    if (scope == Scope::Chat) {
        fixedFont_ = new QCheckBox(tr("Fixed-width (terminal) font"), this);
        fixedFont_->setChecked(chatBase.fixedFont);
        form->addRow(QString(), fixedFont_);
        monoNicks_ = new QCheckBox(tr("Monochrome nicks (irssi-style)"), this);
        monoNicks_->setChecked(chatBase.monoNicks);
        form->addRow(QString(), monoNicks_);
    }

    auto* hint = new QLabel(
        QStringLiteral("Saved as your own theme in the config folder - pick it from the dropdown "
                       "after."),
        this);
    hint->setWordWrap(true);
    root->addWidget(hint);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &ThemeEditorDialog::save);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
}

void ThemeEditorDialog::save() {
    const QString name = name_->text().trimmed().isEmpty() ? QStringLiteral("Custom")
                                                           : name_->text().trimmed();
    if (scope_ == Scope::App) {
        // Start from the base (keeps on/groove/scroll/scroll_hi) and overlay edits.
        AppThemeDefinition theme = appThemeById(baseId_);
        theme.bg = colors_.value(QStringLiteral("bg"));
        theme.panel = colors_.value(QStringLiteral("panel"));
        theme.panel2 = colors_.value(QStringLiteral("panel2"));
        theme.text = colors_.value(QStringLiteral("text"));
        theme.accent = colors_.value(QStringLiteral("accent"));
        resultId_ = saveUserAppTheme(name, theme);
    } else {
        ChatThemeDefinition theme = chatThemeById(baseId_);
        theme.bg = colors_.value(QStringLiteral("bg"));
        theme.fg = colors_.value(QStringLiteral("fg"));
        theme.timestamp = colors_.value(QStringLiteral("ts"));
        theme.bracket = colors_.value(QStringLiteral("bracket"));
        theme.system = colors_.value(QStringLiteral("system"));
        theme.fixedFont = fixedFont_ != nullptr && fixedFont_->isChecked();
        theme.monoNicks = monoNicks_ != nullptr && monoNicks_->isChecked();
        resultId_ = saveUserChatTheme(name, theme);
    }
    if (resultId_.isEmpty()) {
        // Write failure (unwritable config dir) used to accept() silently with
        // an empty id — the user's edits vanished without a word.
        QMessageBox::warning(this, tr("Save Theme"),
                             tr("Could not write the theme file - check that the "
                                "config folder is writable."));
        return;
    }
    accept();
}

} // namespace maxchat::ui
