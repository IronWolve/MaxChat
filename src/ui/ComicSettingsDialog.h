#pragma once

#include <QDialog>
#include <QStringList>
#include <QVariantMap>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;

namespace maxchat::ui {

// Comic Settings: art folder, characters, backgrounds, panels, filtering.
// Reads/writes the comic_* keys. Returns the merged settings via settings().
class ComicSettingsDialog final : public QDialog {
    Q_OBJECT

  public:
    ComicSettingsDialog(QVariantMap settings, const QStringList& characterStems,
                        const QStringList& backgroundFiles, QWidget* parent = nullptr);

    [[nodiscard]] QVariantMap settings() const;

  signals:
    void artDirChanged(const QString& dir);

  private:
    QVariantMap settings_;
    QLineEdit* artDir_ = nullptr;
    QComboBox* defaultBg_ = nullptr;
    QComboBox* selfChar_ = nullptr;
    QSpinBox* panels_ = nullptr;
    QSpinBox* perPanel_ = nullptr;
    QSpinBox* minFont_ = nullptr;
    QCheckBox* captions_ = nullptr;
    QComboBox* captionMode_ = nullptr;
    QString captionColor_;
    QComboBox* captionScale_ = nullptr;
    QCheckBox* randomBg_ = nullptr;
    QCheckBox* ignoreCmds_ = nullptr;
    QPlainTextEdit* botPatterns_ = nullptr;
    QPlainTextEdit* botNicks_ = nullptr;
    QLineEdit* excludeRegex_ = nullptr;
    QPlainTextEdit* charMap_ = nullptr;
};

} // namespace maxchat::ui
