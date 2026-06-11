#include "ui/SoundPlayer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSoundEffect>
#include <QUrl>

namespace maxchat::ui {

QString resolveSoundPath(const QString& soundsDir, const QString& name) {
    if (name.isEmpty()) {
        return {};
    }
    // Basename only — strip any directory components (both separators) so a
    // remote "../../etc/x" can't escape the sounds folder.
    QString base = QFileInfo(QString(name).replace(QLatin1Char('\\'), QLatin1Char('/'))).fileName();
    base = base.trimmed();
    if (base.isEmpty()) {
        return {};
    }
    if (!base.endsWith(QStringLiteral(".wav"), Qt::CaseInsensitive)) {
        base += QStringLiteral(".wav");
    }
    const QString path = QDir(soundsDir).filePath(base);
    return QFileInfo(path).isFile() ? path : QString();
}

QString notifySoundPath(const QString& soundsDir, const QString& bundledDir, const QString& selectedFile) {
    const QString file = selectedFile.isEmpty() ? QStringLiteral("notify.wav") : selectedFile;
    // 1. User's custom file in the sounds directory
    const QString user = resolveSoundPath(soundsDir, file);
    if (!user.isEmpty()) {
        return user;
    }
    // 2. Bundled filesystem copy
    const QString bundled = QDir(bundledDir).filePath(file);
    if (QFileInfo(bundled).isFile()) {
        return bundled;
    }
    // 3. QRC resource fallback
    const QString qrc = QStringLiteral(":/sounds/") + file;
    return QFile::exists(qrc) ? qrc : QString();
}

SoundPlayer::~SoundPlayer() {
    delete effect_;
}

bool SoundPlayer::play(const QString& path) {
    if (path.isEmpty()) {
        return false;
    }
    const bool isQrc = path.startsWith(QStringLiteral(":/"));
    if (!isQrc && !QFileInfo(path).isFile()) {
        return false;
    }
    if (effect_ == nullptr) {
        effect_ = new QSoundEffect();
    }
    const QUrl source = isQrc ? QUrl(QStringLiteral("qrc") + path)
                               : QUrl::fromLocalFile(path);
    effect_->setSource(source);
    effect_->setVolume(0.9);
    effect_->play();
    return true;
}

} // namespace maxchat::ui
