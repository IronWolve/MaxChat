#pragma once

#include <QImage>
#include <QObject>
#include <QString>

namespace maxchat::comic {
class Character;
}

namespace maxchat::ui {

class MainWindow;

// The Comic Mode backend (decomp Phase 5). Owns the comic-panel rendering
// pipeline lifted out of MainWindow: refreshComic + the art-loading
// (ensureComicArt), per-nick character / background / emotion resolution, the
// Comic Mode toggle, save-to-image, and the settings/help dialogs.
//
// Behaviour-preserving relocation: ComicController is a friend of MainWindow
// holding a MainWindow&, so the handlers still drive the window's comic state
// (m_comic* members, m_comicView via ChatPane) directly — every call gained an
// m_window. prefix, logic byte-identical. A later pass can give it its own state.
class ComicController : public QObject {
    Q_OBJECT

  public:
    ComicController(MainWindow& window, QObject* parent = nullptr)
        : QObject(parent), m_window(window) {}

    void setComicMode(bool enabled);
    void refreshComic();
    void ensureComicArt();
    void saveComic();
    void openComicSettings();
    void openComicHelp();

    [[nodiscard]] maxchat::comic::Character* comicCharacterForNick(const QString& nick);
    [[nodiscard]] QImage comicBackground();
    [[nodiscard]] QString comicEmotionForMessage(const QString& nick, const QString& text);

  private:
    MainWindow& m_window;
};

} // namespace maxchat::ui
