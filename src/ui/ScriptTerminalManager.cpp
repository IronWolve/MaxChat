#include "ui/ScriptTerminalManager.h"

#include "ui/ScriptTerminalDialog.h"

#include <QWidget>

namespace maxchat::ui {

ScriptTerminalManager::ScriptTerminalManager(QWidget* parent)
    : QObject(parent), parentWidget_(parent) {}

bool ScriptTerminalManager::hasTerminal(const QString& id) const {
    return terminal(id) != nullptr;
}

QSize ScriptTerminalManager::terminalSize(const QString& id) const {
    if (ScriptTerminalDialog* dlg = terminal(id); dlg != nullptr) {
        return dlg->terminalSize();
    }
    return {};
}

ScriptTerminalDialog* ScriptTerminalManager::openTerminal(const QString& id,
                                                          const QString& title,
                                                          const TerminalProfile& profile,
                                                          const QString& network,
                                                          const QString& scriptName) {
    if (ScriptTerminalDialog* existing = terminal(id); existing != nullptr) {
        existing->show();
        existing->raise();
        existing->activateWindow();
        return existing;
    }

    auto* dlg = new ScriptTerminalDialog(id, title, profile, parentWidget_);
    dlg->setFontPreferences(fontFamily_, fontPointSize_, fontBold_);
    terminals_.insert(id, dlg);
    order_.append(id);
    TerminalInfo info;
    info.id = id;
    info.network = network.trimmed();
    info.scriptName = scriptName.trimmed();
    info.label = QStringLiteral("Term %1").arg(nextOrdinal_++);
    meta_.insert(id, info);
    connect(dlg, &ScriptTerminalDialog::inputSubmitted, this,
            &ScriptTerminalManager::inputSubmitted);
    connect(dlg, &ScriptTerminalDialog::linkActivated, this,
            &ScriptTerminalManager::linkActivated);
    connect(dlg, &ScriptTerminalDialog::killRequested, this,
            [this](const QString& killId) { killTerminal(killId); });
    connect(dlg, &ScriptTerminalDialog::fontPreferenceChanged, this,
            &ScriptTerminalManager::fontPreferenceChanged);
    connect(dlg, &ScriptTerminalDialog::gridSizeChanged, this,
            &ScriptTerminalManager::gridSizeChanged);
    // Re-size to the grid now that the font preference (family/size) is applied,
    // so the window opens fitting the whole grid instead of a tiny default.
    dlg->sizeToGrid();
    dlg->show();
    emit terminalsChanged();
    return dlg;
}

void ScriptTerminalManager::showTerminal(const QString& id) {
    if (ScriptTerminalDialog* dlg = terminal(id); dlg != nullptr) {
        dlg->show();
        dlg->raise();
        dlg->activateWindow();
    }
}

void ScriptTerminalManager::closeTerminal(const QString& id) {
    // Hide only — the session lives until killed.
    if (ScriptTerminalDialog* dlg = terminal(id); dlg != nullptr) {
        dlg->hide();
    }
}

void ScriptTerminalManager::killTerminal(const QString& id) {
    ScriptTerminalDialog* dlg = terminal(id);
    const bool known = terminals_.contains(id) || meta_.contains(id);
    terminals_.remove(id);
    meta_.remove(id);
    order_.removeAll(id);
    if (dlg != nullptr) {
        dlg->deleteLater();
    }
    if (known) {
        emit terminalClosed(id);
        emit terminalsChanged();
    }
}

QList<TerminalInfo> ScriptTerminalManager::terminals() const {
    QList<TerminalInfo> out;
    out.reserve(order_.size());
    for (const QString& id : order_) {
        if (meta_.contains(id)) {
            out.append(meta_.value(id));
        }
    }
    return out;
}

void ScriptTerminalManager::closeAll() {
    const QList<QString> ids = order_;
    for (const QString& id : ids) {
        killTerminal(id);
    }
}

void ScriptTerminalManager::writeText(const QString& id, const QString& text) {
    if (ScriptTerminalDialog* dlg = terminal(id); dlg != nullptr) {
        dlg->writeText(text);
    }
}

void ScriptTerminalManager::clear(const QString& id) {
    if (ScriptTerminalDialog* dlg = terminal(id); dlg != nullptr) {
        dlg->clear();
    }
}

bool ScriptTerminalManager::applyFrame(const QString& id, const QString& ops, QString* error) {
    if (ScriptTerminalDialog* dlg = terminal(id); dlg != nullptr) {
        return dlg->applyFrame(ops, error);
    }
    if (error != nullptr) {
        *error = QStringLiteral("terminal not open");
    }
    return false;
}

void ScriptTerminalManager::setStatusText(const QString& id, const QString& text) {
    if (ScriptTerminalDialog* dlg = terminal(id); dlg != nullptr) {
        dlg->setStatusText(text);
    }
}

void ScriptTerminalManager::setPromptText(const QString& id, const QString& text) {
    if (ScriptTerminalDialog* dlg = terminal(id); dlg != nullptr) {
        dlg->setPromptText(text);
    }
}

void ScriptTerminalManager::setProfile(const QString& id, const TerminalProfile& profile) {
    if (ScriptTerminalDialog* dlg = terminal(id); dlg != nullptr) {
        dlg->setProfile(profile);
    }
}

void ScriptTerminalManager::setFitMode(const QString& id, const QString& mode) {
    if (ScriptTerminalDialog* dlg = terminal(id); dlg != nullptr) {
        dlg->setFitMode(mode);
    }
}

void ScriptTerminalManager::setTerminalFont(const QString& family, const int pointSize,
                                            const bool bold) {
    fontFamily_ = family.trimmed();
    fontPointSize_ = pointSize;
    fontBold_ = bold;
    for (const QPointer<ScriptTerminalDialog>& dlg : terminals_) {
        if (!dlg.isNull()) {
            dlg->setFontPreferences(fontFamily_, fontPointSize_, fontBold_);
        }
    }
}

ScriptTerminalDialog* ScriptTerminalManager::terminal(const QString& id) const {
    const QPointer<ScriptTerminalDialog> dlg = terminals_.value(id);
    return dlg.isNull() ? nullptr : dlg.data();
}

} // namespace maxchat::ui
