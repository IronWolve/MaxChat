#pragma once

#include <QColor>
#include <QString>
#include <QVector>
#include <QWidget>

namespace maxchat::ui {

// One speech entry in the comic strip.
struct ComicLine {
    QString nick;
    QString text;
    bool action = false;
};

// Foundational comic-strip renderer: lays recent messages out as panels, each
// with a deterministically-generated character (no external art needed), a name
// caption, and a speech bubble. A first pass the bundled-art pipeline can later
// enrich.
class ComicView final : public QWidget {
    Q_OBJECT

  public:
    explicit ComicView(QWidget* parent = nullptr);

    void setLines(const QVector<ComicLine>& lines);
    void setShowNames(bool show);
    void setPanelCount(int count); // max panels shown

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    [[nodiscard]] int columnsForWidth() const;

    QVector<ComicLine> lines_;
    bool showNames_ = true;
    int panelCount_ = 4;
};

} // namespace maxchat::ui
