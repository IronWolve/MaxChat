#include "ui/SoundPlayer.h"

#include <QDir>
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

QString notifySoundPath(const QString& soundsDir, const QString& bundledDir) {
    const QString user = resolveSoundPath(soundsDir, QStringLiteral("notify.wav"));
    if (!user.isEmpty()) {
        return user;
    }
    const QString bundled = QDir(bundledDir).filePath(QStringLiteral("notify.wav"));
    return QFileInfo(bundled).isFile() ? bundled : QString();
}

SoundPlayer::~SoundPlayer() {
    delete effect_;
}

bool SoundPlayer::play(const QString& path) {
    if (path.isEmpty() || !QFileInfo(path).isFile()) {
        return false;
    }
    if (effect_ == nullptr) {
        effect_ = new QSoundEffect();
    }
    effect_->setSource(QUrl::fromLocalFile(path));
    effect_->setVolume(0.9);
    effect_->play();
    return true;
}

} // namespace maxchat::ui
