#pragma once

#include <QPixmap>
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

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    QVector<QPixmap> panels_;
};

} // namespace maxchat::ui
