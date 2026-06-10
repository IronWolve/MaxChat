#include "ui/ChannelModesDialog.h"

#include <QCheckBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QtTest/QtTest>

using maxchat::ui::ChannelModesDialog;

class ChannelModesDialogTest final : public QObject {
    Q_OBJECT

  private slots:
    void parsesCurrentModeLine() {
        QStringList changes;
        ChannelModesDialog dialog(QStringLiteral("#chat"), QStringLiteral("TestNet"),
                                  QStringLiteral("+ntkl secret 50"),
                                  [&changes](const QString& change) { changes.append(change); });

        auto* topicLocked = dialog.findChild<QCheckBox*>(QStringLiteral("mode_t"));
        auto* noExternal = dialog.findChild<QCheckBox*>(QStringLiteral("mode_n"));
        auto* keyEnabled = dialog.findChild<QCheckBox*>(QStringLiteral("key_enabled"));
        auto* keyEdit = dialog.findChild<QLineEdit*>(QStringLiteral("key_edit"));
        auto* limitEnabled = dialog.findChild<QCheckBox*>(QStringLiteral("limit_enabled"));
        auto* limitSpin = dialog.findChild<QSpinBox*>(QStringLiteral("limit_spin"));

        QVERIFY(topicLocked != nullptr);
        QVERIFY(noExternal != nullptr);
        QVERIFY(keyEnabled != nullptr);
        QVERIFY(keyEdit != nullptr);
        QVERIFY(limitEnabled != nullptr);
        QVERIFY(limitSpin != nullptr);
        QVERIFY(topicLocked->isChecked());
        QVERIFY(noExternal->isChecked());
        QVERIFY(keyEnabled->isChecked());
        QCOMPARE(keyEdit->text(), QStringLiteral("secret"));
        QVERIFY(limitEnabled->isChecked());
        QCOMPARE(limitSpin->value(), 50);
    }

    void emitsModeChangesFromControls() {
        QStringList changes;
        ChannelModesDialog dialog(QStringLiteral("#chat"), QStringLiteral("TestNet"), QString(),
                                  [&changes](const QString& change) { changes.append(change); });

        auto* moderated = dialog.findChild<QCheckBox*>(QStringLiteral("mode_m"));
        auto* limitEnabled = dialog.findChild<QCheckBox*>(QStringLiteral("limit_enabled"));
        auto* limitSpin = dialog.findChild<QSpinBox*>(QStringLiteral("limit_spin"));

        QVERIFY(moderated != nullptr);
        QVERIFY(limitEnabled != nullptr);
        QVERIFY(limitSpin != nullptr);

        moderated->setChecked(true);
        QCOMPARE(changes.takeLast(), QStringLiteral("+m"));

        limitSpin->setValue(25);
        limitEnabled->setChecked(true);
        QCOMPARE(changes.takeLast(), QStringLiteral("+l 25"));
    }
};

QTEST_MAIN(ChannelModesDialogTest)

#include "channel_modes_dialog_test.moc"
