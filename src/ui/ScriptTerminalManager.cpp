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
                                                          const TerminalProfile& profile) {
    if (ScriptTerminalDialog* existing = terminal(id); existing != nullptr) {
        existing->raise();
        existing->activateWindow();
        return existing;
    }

    auto* dlg = new ScriptTerminalDialog(id, title, profile, parentWidget_);
    dlg->setFontPreferences(fontFamily_, fontPointSize_, fontBold_);
    terminals_.insert(id, dlg);
    connect(dlg, &ScriptTerminalDialog::inputSubmitted, this,
            &ScriptTerminalManager::inputSubmitted);
    connect(dlg, &ScriptTerminalDialog::linkActivated, this,
            &ScriptTerminalManager::linkActivated);
    connect(dlg, &ScriptTerminalDialog::terminalClosed, this, [this](const QString& closedId) {
        terminals_.remove(closedId);
        emit terminalClosed(closedId);
    });
    dlg->show();
    return dlg;
}

void ScriptTerminalManager::closeTerminal(const QString& id) {
    if (ScriptTerminalDialog* dlg = terminal(id); dlg != nullptr) {
        dlg->close();
    }
}

void ScriptTerminalManager::closeAll() {
    const QList<QString> ids = terminals_.keys();
    for (const QString& id : ids) {
        closeTerminal(id);
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
