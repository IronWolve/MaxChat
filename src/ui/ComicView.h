#pragma once

#include <QPixmap>
#include <QRect>
#include <QVector>
#include <QWidget>

namespace maxchat::ui {

// Displays rendered comic panels (square pixmaps) in a centred grid, scaling
// each to fit. The panels themselves are produced by the comic renderer and
// pushed in by MainWindow's comic engine.
class ComicView final : public QWidget {
    Q_OBJECT

  public:
    explicit ComicView(QWidget* parent = nullptr);

    void setPanels(const QVector<QPixmap>& panels);
    [[nodiscard]] QPixmap sheet() const; // all panels composited (for Save/Copy)
    [[nodiscard]] bool hasPanels() const { return !panels_.isEmpty(); }
    [[nodiscard]] const QVector<QPixmap>& panels() const { return panels_; }

  signals:
    void saveRequested(); // user picked "Save comic…" from the context menu

  protected:
    void paintEvent(QPaintEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

  private:
    // On-screen rect of each panel (shared by painting and hit-testing).
    [[nodiscard]] QVector<QRect> panelRects() const;
    [[nodiscard]] int panelAt(const QPoint& pos) const;
    void showZoom(int index); // lightbox: click/Esc closes, arrows flip panels

    QVector<QPixmap> panels_;
};

} // namespace maxchat::ui
