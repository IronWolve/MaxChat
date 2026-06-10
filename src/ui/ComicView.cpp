#include "ui/ComicView.h"

#include <QPainter>

#include <cmath>

namespace maxchat::ui {

namespace {

// Choose rows/cols that maximise the square panel edge within (w,h).
void gridFor(int count, int w, int h, int gap, int& rows, int& cols, int& edge) {
    rows = 1;
    cols = count;
    edge = 0;
    for (int r = 1; r <= count; ++r) {
        const int c = (count + r - 1) / r;
        const int cellW = (w - gap * (c + 1)) / c;
        const int cellH = (h - gap * (r + 1)) / r;
        const int e = std::min(cellW, cellH);
        if (e > edge) {
            edge = e;
            rows = r;
            cols = c;
        }
    }
}

} // namespace

ComicView::ComicView(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("comicView"));
    setMinimumHeight(120);
}

void ComicView::setPanels(const QVector<QPixmap>& panels) {
    panels_ = panels;
    update();
}

void ComicView::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    if (panels_.isEmpty()) {
        painter.setPen(palette().color(QPalette::WindowText));
        painter.drawText(rect(), Qt::AlignCenter,
                         QStringLiteral("Comic Mode - recent chat appears as panels here.\n"
                                        "Set a comic art folder in Comic Settings."));
        return;
    }
    const int gap = 6;
    int rows;
    int cols;
    int edge;
    gridFor(panels_.size(), width(), height(), gap, rows, cols, edge);
    edge = std::max(16, edge);
    const int blockH = rows * edge + gap * (rows - 1);
    int top = std::max(gap, (height() - blockH) / 2);
    for (int i = 0; i < panels_.size(); ++i) {
        const int r = i / cols;
        const int c = i % cols;
        const int rowCount = std::min<int>(cols, static_cast<int>(panels_.size()) - r * cols);
        const int blockW = rowCount * edge + gap * (rowCount - 1);
        const int left = std::max(gap, (width() - blockW) / 2);
        const QRect cell(left + c * (edge + gap), top + r * (edge + gap), edge, edge);
        painter.drawPixmap(cell, panels_[i].scaled(edge, edge, Qt::KeepAspectRatio,
                                                   Qt::SmoothTransformation));
    }
}

QPixmap ComicView::sheet() const {
    if (panels_.isEmpty()) {
        return {};
    }
    const int n = panels_.size();
    const int cols = n <= 4 ? n : (n + 1) / 2;
    const int rows = (n + cols - 1) / cols;
    int cell = 0;
    for (const QPixmap& p : panels_) {
        cell = std::max({cell, p.width(), p.height()});
    }
    if (cell <= 0) {
        cell = 315;
    }
    const int gap = 8;
    QImage img(cols * cell + gap * (cols + 1), rows * cell + gap * (rows + 1),
               QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    for (int i = 0; i < n; ++i) {
        const int r = i / cols;
        const int c = i % cols;
        p.drawPixmap(gap + c * (cell + gap), gap + r * (cell + gap),
                     panels_[i].scaled(cell, cell, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    p.end();
    return QPixmap::fromImage(img);
}

} // namespace maxchat::ui
