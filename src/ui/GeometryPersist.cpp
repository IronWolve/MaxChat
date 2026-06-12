#include "ui/GeometryPersist.h"

#include "core/SettingsStore.h"

#include <QByteArray>
#include <QDialog>
#include <QString>

namespace maxchat::ui {

void attachGeometryPersist(QDialog* dlg, maxchat::core::SettingsStore& store,
                           const QString& key) {
    const QVariantMap settings = store.loadRaw();
    const QString saved = settings.value(key).toString();
    if (!saved.isEmpty()) {
        dlg->restoreGeometry(QByteArray::fromBase64(saved.toUtf8()));
    }

    QObject::connect(dlg, &QDialog::finished, dlg, [dlg, &store, key](int) {
        (void)store.setValue(key, QString::fromUtf8(dlg->saveGeometry().toBase64()));
    });
}

} // namespace maxchat::ui
