#include "ui/ComicSettingsDialog.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFontComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <utility>

namespace maxchat::ui {

ComicSettingsDialog::ComicSettingsDialog(QVariantMap settings, const QStringList& characterStems,
                                         const QStringList& backgroundFiles,
                                         const QStringList& channelKeys, QWidget* parent)
    : QDialog(parent), settings_(std::move(settings)), characterStems_(characterStems),
      backgroundFiles_(backgroundFiles), channelKeys_(channelKeys) {
    setWindowTitle(tr("Comic Settings"));
    resize(640, 560);
    captionColor_ = settings_.value(QStringLiteral("comic_caption_color"),
                                    QStringLiteral("#363636")).toString();

    auto* root = new QVBoxLayout(this);
    auto* content = new QHBoxLayout();
    auto* nav = new QListWidget(this);
    nav->setObjectName(QStringLiteral("comicNav"));
    nav->setFixedWidth(140);
    auto* pages = new QStackedWidget(this);
    content->addWidget(nav);
    content->addWidget(pages, 1);
    root->addLayout(content, 1);

    const auto addPage = [nav, pages](const QString& title, QWidget* page) {
        nav->addItem(title);
        pages->addWidget(page);
    };

    // ---- Global ----
    auto* global = new QWidget(this);
    auto* gf = new QFormLayout(global);
    artDir_ = new QLineEdit(settings_.value(QStringLiteral("comic_art_dir")).toString(), global);
    auto* browse = new QPushButton(tr("Browse..."), global);
    connect(browse, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(
            this, QStringLiteral("Comic art folder"), artDir_->text());
        if (!dir.isEmpty()) {
            artDir_->setText(dir);
            emit artDirChanged(dir);
        }
    });
    auto* artRow = new QWidget(global);
    auto* artLayout = new QHBoxLayout(artRow);
    artLayout->setContentsMargins(0, 0, 0, 0);
    artLayout->addWidget(artDir_, 1);
    artLayout->addWidget(browse);
    gf->addRow(QStringLiteral("Comic art folder"), artRow);

    defaultBg_ = new QComboBox(global);
    defaultBg_->addItem(QStringLiteral("(first available)"), QString());
    for (const QString& file : backgroundFiles) {
        defaultBg_->addItem(QFileInfo(file).completeBaseName(), file);
    }
    const int bgIdx = defaultBg_->findData(settings_.value(QStringLiteral("comic_bg")).toString());
    defaultBg_->setCurrentIndex(bgIdx >= 0 ? bgIdx : 0);
    gf->addRow(QStringLiteral("Default background"), defaultBg_);

    selfChar_ = new QComboBox(global);
    selfChar_->addItem(QStringLiteral("(random per nick)"), QString());
    for (const QString& stem : characterStems) {
        selfChar_->addItem(stem, stem);
    }
    const int selfIdx =
        selfChar_->findData(settings_.value(QStringLiteral("comic_self_char")).toString());
    selfChar_->setCurrentIndex(selfIdx >= 0 ? selfIdx : 0);
    gf->addRow(QStringLiteral("Your character"), selfChar_);

    panels_ = new QSpinBox(global);
    panels_->setRange(1, 6);
    panels_->setValue(settings_.value(QStringLiteral("comic_panels"), 4).toInt());
    gf->addRow(QStringLiteral("Comic panels"), panels_);
    perPanel_ = new QSpinBox(global);
    perPanel_->setRange(1, 6);
    perPanel_->setValue(settings_.value(QStringLiteral("comic_per_panel"), 4).toInt());
    gf->addRow(QStringLiteral("Max bubbles per panel"), perPanel_);
    minFont_ = new QSpinBox(global);
    minFont_->setRange(6, 13);
    minFont_->setValue(settings_.value(QStringLiteral("comic_min_font"), 9).toInt());
    gf->addRow(QStringLiteral("Min text size (spill)"), minFont_);

    captions_ = new QCheckBox(global);
    captions_->setChecked(settings_.value(QStringLiteral("comic_captions"), true).toBool());
    gf->addRow(QStringLiteral("Show character names"), captions_);
    captionMode_ = new QComboBox(global);
    captionMode_->addItem(QStringLiteral("Match speaker color"), QStringLiteral("nick"));
    captionMode_->addItem(QStringLiteral("Fixed color"), QStringLiteral("fixed"));
    const int cmIdx = captionMode_->findData(
        settings_.value(QStringLiteral("comic_caption_mode"), QStringLiteral("nick")).toString());
    captionMode_->setCurrentIndex(cmIdx >= 0 ? cmIdx : 0);
    gf->addRow(QStringLiteral("Name color"), captionMode_);
    auto* fixedColorBtn = new QPushButton(tr("Fixed name color..."), global);
    connect(fixedColorBtn, &QPushButton::clicked, this, [this]() {
        const QColor c = QColorDialog::getColor(QColor(captionColor_), this);
        if (c.isValid()) {
            captionColor_ = c.name();
        }
    });
    gf->addRow(QString(), fixedColorBtn);
    captionScale_ = new QComboBox(global);
    captionScale_->addItem(QStringLiteral("Small"), 0.8);
    captionScale_->addItem(QStringLiteral("Normal"), 1.0);
    captionScale_->addItem(QStringLiteral("Large"), 1.3);
    const double scaleVal = settings_.value(QStringLiteral("comic_caption_scale"), 1.0).toDouble();
    captionScale_->setCurrentIndex(scaleVal < 0.9 ? 0 : scaleVal > 1.1 ? 2 : 1);
    gf->addRow(QStringLiteral("Name size"), captionScale_);
    randomBg_ = new QCheckBox(global);
    randomBg_->setChecked(settings_.value(QStringLiteral("comic_random_bg"), false).toBool());
    gf->addRow(QStringLiteral("Random background (unset rooms)"), randomBg_);

    balloonTint_ = new QCheckBox(global);
    balloonTint_->setChecked(settings_.value(QStringLiteral("comic_balloon_tint"), true).toBool());
    gf->addRow(QStringLiteral("Tint balloons by speaker color"), balloonTint_);

    comicFont_ = new QFontComboBox(global);
    comicFont_->setCurrentFont(QFont(
        settings_.value(QStringLiteral("comic_font"), QStringLiteral("Comic Relief")).toString()));
    gf->addRow(QStringLiteral("Balloon font"), comicFont_);

    addPage(QStringLiteral("Global"), global);

    // ---- Characters (global nick -> stem map, one per line "nick=stem") ----
    auto* charsPage = new QWidget(this);
    auto* cv = new QVBoxLayout(charsPage);
    cv->addWidget(new QLabel(tr("Global nick assignments, one per line as nick=character"),
                             charsPage));
    charMap_ = new QPlainTextEdit(charsPage);
    QStringList charLines;
    const QVariantMap chars = settings_.value(QStringLiteral("comic_chars")).toMap();
    for (auto it = chars.constBegin(); it != chars.constEnd(); ++it) {
        charLines.append(QStringLiteral("%1=%2").arg(it.key(), it.value().toString()));
    }
    charMap_->setPlainText(charLines.join(QLatin1Char('\n')));
    cv->addWidget(charMap_, 1);
    addPage(QStringLiteral("Characters"), charsPage);

    // ---- Channels (per-channel bg / chars / ignore overrides) ----
    addPage(QStringLiteral("Channels"), buildChannelsPage());

    // ---- Bots / filtering ----
    auto* bots = new QWidget(this);
    auto* bv = new QVBoxLayout(bots);
    ignoreCmds_ = new QCheckBox(tr("Filter bot commands and corrections"), bots);
    ignoreCmds_->setChecked(settings_.value(QStringLiteral("comic_ignore_cmds"), true).toBool());
    bv->addWidget(ignoreCmds_);
    bv->addWidget(new QLabel(tr("Command/correction prefixes (one per line):"), bots));
    botPatterns_ = new QPlainTextEdit(bots);
    botPatterns_->setPlainText(
        settings_.value(QStringLiteral("comic_bot_patterns")).toStringList().join(QLatin1Char('\n')));
    bv->addWidget(botPatterns_);
    bv->addWidget(new QLabel(tr("Bot nicks never drawn (one per line):"), bots));
    botNicks_ = new QPlainTextEdit(bots);
    botNicks_->setPlainText(
        settings_.value(QStringLiteral("comic_ignore")).toStringList().join(QLatin1Char('\n')));
    bv->addWidget(botNicks_);
    auto* reRow = new QFormLayout();
    excludeRegex_ = new QLineEdit(settings_.value(QStringLiteral("comic_exclude_regex")).toString(),
                                  bots);
    reRow->addRow(QStringLiteral("Advanced regex"), excludeRegex_);
    bv->addLayout(reRow);
    addPage(QStringLiteral("Bots"), bots);

    nav->setCurrentRow(0);
    connect(nav, &QListWidget::currentRowChanged, pages, &QStackedWidget::setCurrentIndex);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        saveCurrentChannel(); // capture the visible channel editor before closing
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
}

QWidget* ComicSettingsDialog::buildChannelsPage() {
    // Working copy of every saved per-channel override, so channels you don't
    // open in this session keep theirs.
    chanWork_ = settings_.value(QStringLiteral("comic_channels")).toMap();

    auto* page = new QWidget(this);
    auto* v = new QVBoxLayout(page);
    if (channelKeys_.isEmpty()) {
        v->addWidget(new QLabel(
            QStringLiteral("Join a channel to set its comic background / characters here."), page));
        v->addStretch(1);
        return page;
    }

    auto* sel = new QHBoxLayout();
    sel->addWidget(new QLabel(tr("Channel:"), page));
    chanSelect_ = new QComboBox(page);
    for (const QString& key : channelKeys_) {
        chanSelect_->addItem(key, key);
    }
    sel->addWidget(chanSelect_, 1);
    v->addLayout(sel);
    v->addWidget(new QLabel(
        QStringLiteral("Overrides for this channel — blank fields fall back to Global."), page));

    chanHost_ = new QWidget(page);
    auto* hostLayout = new QVBoxLayout(chanHost_);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    v->addWidget(chanHost_, 1);

    connect(chanSelect_, &QComboBox::currentIndexChanged, this, [this](int index) {
        saveCurrentChannel(); // keep edits when switching channels
        loadChannel(index);
    });
    loadChannel(0);
    return page;
}

void ComicSettingsDialog::loadChannel(int index) {
    if (chanHost_ == nullptr || index < 0 || index >= channelKeys_.size()) {
        return;
    }
    chanCurrentIndex_ = index;
    const QVariantMap cfg = chanWork_.value(channelKeys_.at(index)).toMap();

    // Clear the previous channel's editor widgets.
    QLayout* hostLayout = chanHost_->layout();
    while (QLayoutItem* item = hostLayout->takeAt(0)) {
        if (item->widget() != nullptr) {
            item->widget()->deleteLater();
        }
        delete item;
    }

    auto* holder = new QWidget(chanHost_);
    auto* form = new QFormLayout(holder);

    chanBg_ = new QComboBox(holder);
    chanBg_->addItem(QStringLiteral("(use global default)"), QString());
    for (const QString& file : backgroundFiles_) {
        chanBg_->addItem(QFileInfo(file).completeBaseName(), file);
    }
    const int bgIdx = chanBg_->findData(cfg.value(QStringLiteral("bg")).toString());
    chanBg_->setCurrentIndex(bgIdx >= 0 ? bgIdx : 0);
    form->addRow(QStringLiteral("Background"), chanBg_);

    form->addRow(new QLabel(tr("Assign characters (this channel), one per line as "
                                           "nick=character:"),
                            holder));
    chanChars_ = new QPlainTextEdit(holder);
    QStringList lines;
    const QVariantMap chars = cfg.value(QStringLiteral("chars")).toMap();
    for (auto it = chars.constBegin(); it != chars.constEnd(); ++it) {
        lines.append(QStringLiteral("%1=%2").arg(it.key(), it.value().toString()));
    }
    chanChars_->setPlainText(lines.join(QLatin1Char('\n')));
    form->addRow(chanChars_);

    chanIgnore_ = new QLineEdit(holder);
    QStringList ignore;
    for (const QVariant& n : cfg.value(QStringLiteral("ignore")).toList()) {
        ignore.append(n.toString());
    }
    chanIgnore_->setText(ignore.join(QLatin1Char(' ')));
    chanIgnore_->setPlaceholderText(
        QStringLiteral("nicks hidden from the comic in this channel — still shown in chat"));
    form->addRow(QStringLiteral("Hide from comic"), chanIgnore_);

    hostLayout->addWidget(holder);
}

void ComicSettingsDialog::saveCurrentChannel() {
    if (chanCurrentIndex_ < 0 || chanCurrentIndex_ >= channelKeys_.size() || chanBg_ == nullptr) {
        return;
    }
    QVariantMap cfg;
    const QString bg = chanBg_->currentData().toString();
    if (!bg.isEmpty()) {
        cfg.insert(QStringLiteral("bg"), bg);
    }

    QVariantMap chars;
    for (const QString& line : chanChars_->toPlainText().split(QLatin1Char('\n'))) {
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0) {
            continue;
        }
        const QString nick = line.left(eq).trimmed().toLower();
        const QString stem = line.mid(eq + 1).trimmed();
        if (!nick.isEmpty() && !stem.isEmpty()) {
            chars.insert(nick, stem);
        }
    }
    if (!chars.isEmpty()) {
        cfg.insert(QStringLiteral("chars"), chars);
    }

    QStringList ignore;
    for (const QString& n :
         chanIgnore_->text().split(QRegularExpression(QStringLiteral("[,\\s]+")), Qt::SkipEmptyParts)) {
        ignore.append(n.toLower());
    }
    if (!ignore.isEmpty()) {
        cfg.insert(QStringLiteral("ignore"), QVariant(ignore).toList());
    }

    const QString key = channelKeys_.at(chanCurrentIndex_);
    if (cfg.isEmpty()) {
        chanWork_.remove(key); // all blank → no override stored
    } else {
        chanWork_.insert(key, cfg);
    }
}

QVariantMap ComicSettingsDialog::settings() const {
    QVariantMap out = settings_;
    out.insert(QStringLiteral("comic_art_dir"), artDir_->text().trimmed());
    out.insert(QStringLiteral("comic_bg"), defaultBg_->currentData().toString());
    out.insert(QStringLiteral("comic_self_char"), selfChar_->currentData().toString());
    out.insert(QStringLiteral("comic_panels"), panels_->value());
    out.insert(QStringLiteral("comic_per_panel"), perPanel_->value());
    out.insert(QStringLiteral("comic_min_font"), minFont_->value());
    out.insert(QStringLiteral("comic_captions"), captions_->isChecked());
    out.insert(QStringLiteral("comic_caption_mode"), captionMode_->currentData().toString());
    out.insert(QStringLiteral("comic_caption_color"), captionColor_);
    out.insert(QStringLiteral("comic_caption_scale"), captionScale_->currentData().toDouble());
    out.insert(QStringLiteral("comic_random_bg"), randomBg_->isChecked());
    out.insert(QStringLiteral("comic_balloon_tint"), balloonTint_->isChecked());
    out.insert(QStringLiteral("comic_font"), comicFont_->currentFont().family());
    out.insert(QStringLiteral("comic_ignore_cmds"), ignoreCmds_->isChecked());

    QStringList patterns;
    for (const QString& line : botPatterns_->toPlainText().split(QLatin1Char('\n'))) {
        const QString t = line.trimmed();
        if (!t.isEmpty()) {
            patterns.append(t);
        }
    }
    out.insert(QStringLiteral("comic_bot_patterns"), patterns);

    QStringList ignores;
    for (const QString& line : botNicks_->toPlainText().split(QRegularExpression(QStringLiteral("[,\\s]+")),
                                                              Qt::SkipEmptyParts)) {
        ignores.append(line.toLower());
    }
    out.insert(QStringLiteral("comic_ignore"), ignores);
    out.insert(QStringLiteral("comic_exclude_regex"), excludeRegex_->text().trimmed());

    QVariantMap chars;
    for (const QString& line : charMap_->toPlainText().split(QLatin1Char('\n'))) {
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0) {
            continue;
        }
        const QString nick = line.left(eq).trimmed().toLower();
        const QString stem = line.mid(eq + 1).trimmed();
        if (!nick.isEmpty() && !stem.isEmpty()) {
            chars.insert(nick, stem);
        }
    }
    out.insert(QStringLiteral("comic_chars"), chars);
    out.insert(QStringLiteral("comic_channels"), chanWork_);
    return out;
}

} // namespace maxchat::ui
