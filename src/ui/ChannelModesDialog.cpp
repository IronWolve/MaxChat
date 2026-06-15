#include "ui/ChannelModesDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QSpinBox>
#include <QVBoxLayout>

namespace maxchat::ui {

namespace {

struct ModeState {
    QSet<QChar> flags;
    QString key;
    int limit = 0;
};

struct SimpleMode {
    QChar mode;
    const char* label;
    const char* tooltip;
};

const SimpleMode kSimpleModes[] = {
    {QLatin1Char('t'), "Topic locked", "Only channel operators can change the topic"},
    {QLatin1Char('n'), "No external messages", "You must be in the channel to send messages to it"},
    {QLatin1Char('m'), "Moderated", "Only voiced users and operators may talk"},
    {QLatin1Char('i'), "Invite only", "Users can only join if invited"},
    {QLatin1Char('s'), "Secret", "Hide the channel from the public list and from whois"},
    {QLatin1Char('p'), "Private", "Do not advertise the channel"},
};

bool modeConsumesParameter(QChar mode, bool adding) {
    static const QSet<QChar> alwaysParam = {QLatin1Char('q'), QLatin1Char('a'), QLatin1Char('o'),
                                            QLatin1Char('h'), QLatin1Char('v'), QLatin1Char('b'),
                                            QLatin1Char('e'), QLatin1Char('I')};
    if (alwaysParam.contains(mode) || mode == QLatin1Char('k')) {
        return true;
    }
    return adding && mode == QLatin1Char('l');
}

ModeState parseModes(const QString& modeLine) {
    ModeState state;
    const QStringList parts = modeLine.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        return state;
    }

    const QString modes = parts.first();
    int paramIndex = 1;
    bool adding = true;
    for (const QChar mode : modes) {
        if (mode == QLatin1Char('+')) {
            adding = true;
            continue;
        }
        if (mode == QLatin1Char('-')) {
            adding = false;
            continue;
        }

        QString parameter;
        if (modeConsumesParameter(mode, adding) && paramIndex < parts.size()) {
            parameter = parts.at(paramIndex);
            ++paramIndex;
        }

        if (mode == QLatin1Char('k')) {
            if (adding) {
                state.key = parameter;
            } else {
                state.key.clear();
            }
            continue;
        }
        if (mode == QLatin1Char('l')) {
            if (adding) {
                bool ok = false;
                const int value = parameter.toInt(&ok);
                state.limit = ok ? value : 0;
            } else {
                state.limit = 0;
            }
            continue;
        }

        if (adding) {
            state.flags.insert(mode);
        } else {
            state.flags.remove(mode);
        }
    }
    return state;
}

} // namespace

ChannelModesDialog::ChannelModesDialog(const QString& channel, const QString& networkName,
                                       const QString& modeLine, ApplyCallback apply,
                                       QWidget* parent)
    : QDialog(parent), channel_(channel), apply_(std::move(apply)) {
    const ModeState state = parseModes(modeLine);
    key_ = state.key;
    limit_ = state.limit;

    setWindowTitle(QStringLiteral("Channel Modes - %1").arg(channel_));

    auto* layout = new QVBoxLayout(this);
    auto* heading = new QLabel(QStringLiteral("<b>%1</b> - %2").arg(channel_, networkName), this);
    layout->addWidget(heading);

    for (const SimpleMode& simpleMode : kSimpleModes) {
        auto* checkBox =
            new QCheckBox(QStringLiteral("%1 (+%2)")
                              .arg(QString::fromLatin1(simpleMode.label), QString(simpleMode.mode)),
                          this);
        checkBox->setObjectName(QStringLiteral("mode_%1").arg(QString(simpleMode.mode)));
        checkBox->setToolTip(QString::fromLatin1(simpleMode.tooltip));
        checkBox->setChecked(state.flags.contains(simpleMode.mode));
        connect(checkBox, &QCheckBox::toggled, this, [this, mode = simpleMode.mode](bool enabled) {
            applyMode(QStringLiteral("%1%2")
                          .arg(enabled ? QLatin1Char('+') : QLatin1Char('-'))
                          .arg(QString(mode)));
        });
        layout->addWidget(checkBox);
    }

    auto* keyRow = new QHBoxLayout();
    keyEnabled_ = new QCheckBox(QStringLiteral("Key (+k)"), this);
    keyEnabled_->setObjectName(QStringLiteral("key_enabled"));
    keyEnabled_->setChecked(!key_.isEmpty());
    keyEdit_ = new QLineEdit(key_, this);
    keyEdit_->setObjectName(QStringLiteral("key_edit"));
    keyEdit_->setPlaceholderText(QStringLiteral("password"));
    auto* keySetButton = new QPushButton(QStringLiteral("Set"), this);
    connect(keyEnabled_, &QCheckBox::toggled, this, &ChannelModesDialog::setKeyMode);
    connect(keySetButton, &QPushButton::clicked, this, &ChannelModesDialog::applyKeyValue);
    keyRow->addWidget(keyEnabled_);
    keyRow->addWidget(keyEdit_, 1);
    keyRow->addWidget(keySetButton);
    layout->addLayout(keyRow);

    auto* limitRow = new QHBoxLayout();
    limitEnabled_ = new QCheckBox(QStringLiteral("User limit (+l)"), this);
    limitEnabled_->setObjectName(QStringLiteral("limit_enabled"));
    limitEnabled_->setChecked(limit_ > 0);
    limitSpin_ = new QSpinBox(this);
    limitSpin_->setObjectName(QStringLiteral("limit_spin"));
    limitSpin_->setRange(1, 99999);
    limitSpin_->setValue(limit_ > 0 ? limit_ : 50);
    auto* limitSetButton = new QPushButton(QStringLiteral("Set"), this);
    connect(limitEnabled_, &QCheckBox::toggled, this, &ChannelModesDialog::setLimitMode);
    connect(limitSetButton, &QPushButton::clicked, this, &ChannelModesDialog::applyLimitValue);
    limitRow->addWidget(limitEnabled_);
    limitRow->addWidget(limitSpin_, 1);
    limitRow->addWidget(limitSetButton);
    layout->addLayout(limitRow);

    auto* note = new QLabel(
        QStringLiteral("You must be a channel operator for changes to take effect."), this);
    note->setWordWrap(true);
    layout->addWidget(note);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    // A Close-only dialog: dismissing it is a reject, not an accept (changes
    // already applied live via the callback, so the result code is cosmetic).
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void ChannelModesDialog::applyMode(const QString& change) const {
    if (apply_) {
        apply_(change);
    }
}

void ChannelModesDialog::setKeyMode(bool enabled) {
    if (enabled) {
        applyKeyValue();
        return;
    }
    applyMode(QStringLiteral("-k %1").arg(key_.isEmpty() ? QStringLiteral("*") : key_));
}

void ChannelModesDialog::applyKeyValue() {
    const QString key = keyEdit_ == nullptr ? QString() : keyEdit_->text().trimmed();
    if (key.isEmpty()) {
        // Clearing the field with a key already set must tell the server to drop
        // it (-k), otherwise the UI shows "no key" while the channel stays +k.
        if (!key_.isEmpty()) {
            applyMode(QStringLiteral("-k %1").arg(key_));
            key_.clear();
        }
        if (keyEnabled_ != nullptr) {
            keyEnabled_->blockSignals(true);
            keyEnabled_->setChecked(false);
            keyEnabled_->blockSignals(false);
        }
        if (keyEdit_ != nullptr) {
            keyEdit_->setFocus();
        }
        return;
    }

    key_ = key;
    if (keyEnabled_ != nullptr) {
        keyEnabled_->blockSignals(true);
        keyEnabled_->setChecked(true);
        keyEnabled_->blockSignals(false);
    }
    applyMode(QStringLiteral("+k %1").arg(key_));
}

void ChannelModesDialog::setLimitMode(bool enabled) {
    if (enabled) {
        applyLimitValue();
        return;
    }
    limit_ = 0;
    applyMode(QStringLiteral("-l"));
}

void ChannelModesDialog::applyLimitValue() {
    if (limitSpin_ == nullptr) {
        return;
    }
    limit_ = limitSpin_->value();
    if (limitEnabled_ != nullptr) {
        limitEnabled_->blockSignals(true);
        limitEnabled_->setChecked(true);
        limitEnabled_->blockSignals(false);
    }
    applyMode(QStringLiteral("+l %1").arg(limit_));
}

} // namespace maxchat::ui
