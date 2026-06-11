#pragma once

#include <QString>

class QSoundEffect;

namespace maxchat::ui {

// Resolve a CTCP SOUND filename to a local .wav path within `soundsDir`, or an
// empty string. Basename only (no path traversal), `.wav` enforced, and the
// file must already exist — so a remote peer can never make us fetch or play
// anything we don't already own. Mirrors Python sounds.resolve().
[[nodiscard]] QString resolveSoundPath(const QString& soundsDir, const QString& name);

// The notification chime: the user's own <sounds>/notify.wav if present,
// otherwise the bundled default under `bundledDir`, otherwise empty.
[[nodiscard]] QString notifySoundPath(const QString& soundsDir, const QString& bundledDir);

// Low-latency .wav playback via QSoundEffect. A safe no-op if the path is empty
// or missing. One reusable effect, lazily created.
class SoundPlayer final {
  public:
    SoundPlayer() = default;
    ~SoundPlayer();
    SoundPlayer(const SoundPlayer&) = delete;
    SoundPlayer& operator=(const SoundPlayer&) = delete;

    bool play(const QString& path);

  private:
    QSoundEffect* effect_ = nullptr;
};

} // namespace maxchat::ui
