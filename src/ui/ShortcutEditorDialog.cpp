#include "ui/ShortcutEditorDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <utility>

namespace maxchat::ui {

QList<NavShortcutSpec> navShortcutSpecs() {
    QList<NavShortcutSpec> specs;
    for (int index = 1; index <= 9; ++index) {
        specs.append({QStringLiteral("nav%1").arg(index),
                      QStringLiteral("Switch to chat %1").arg(index),
                      QStringLiteral("Alt+%1").arg(index)});
    }
    specs.append({QStringLiteral("navActivity"), QStringLiteral("Jump to unread activity"),
                  QStringLiteral("Alt+`")});
    return specs;
}

ShortcutEditorDialog::ShortcutEditorDialog(QVariantMap overrides, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(QStringLiteral("Keyboard Shortcuts"));

    auto* root = new QVBoxLayout(this);
    auto* hint = new QLabel(
        QStringLiteral("Navigation shortcuts. Clear a binding to disable it; Default restores "
                       "the standard key."),
        this);
    hint->setWordWrap(true);
    root->addWidget(hint);

    auto* form = new QFormLayout();
    const QList<NavShortcutSpec> specs = navShortcutSpecs();
    edits_.reserve(specs.size());
    for (const NavShortcutSpec& spec : specs) {
        auto* edit = new QKeySequenceEdit(this);
        edit->setObjectName(spec.id);
        const QString bound = overrides.value(spec.id, spec.defaultKey).toString();
        edit->setKeySequence(QKeySequence(bound));
        auto* reset = new QPushButton(QStringLiteral("Default"), this);
        connect(reset, &QPushButton::clicked, edit, [edit, spec]() {
            edit->setKeySequence(QKeySequence(spec.defaultKey));
        });
        auto* row = new QWidget(this);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->addWidget(edit, 1);
        rowLayout->addWidget(reset);
        form->addRow(spec.label, row);
        edits_.append(edit);
    }
    root->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &ShortcutEditorDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &ShortcutEditorDialog::reject);
    root->addWidget(buttons);
    resize(420, 460);
}

QVariantMap ShortcutEditorDialog::overrides() const {
    QVariantMap result;
    const QList<NavShortcutSpec> specs = navShortcutSpecs();
    for (qsizetype index = 0; index < specs.size() && index < edits_.size(); ++index) {
        const QString bound = edits_.at(index)->keySequence().toString();
        if (bound != QKeySequence(specs.at(index).defaultKey).toString()) {
            result.insert(specs.at(index).id, bound);
        }
    }
    return result;
}

} // namespace maxchat::ui
