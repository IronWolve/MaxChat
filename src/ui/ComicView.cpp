#include "ui/ComicView.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QVBoxLayout>

#include <cmath>

namespace maxchat::ui {

namespace {

// Lightbox for one panel: dark backdrop, panel scaled to ~80% of the screen,
// Left/Right flip through the strip, click or Esc closes.
class PanelZoomDialog final : public QDialog {
  public:
    PanelZoomDialog(QVector<QPixmap> panels, int index, QWidget* parent)
        : QDialog(parent, Qt::FramelessWindowHint | Qt::Dialog),
          panels_(std::move(panels)),
          index_(index) {
        setAttribute(Qt::WA_DeleteOnClose);
        setModal(true);
        setStyleSheet(QStringLiteral("QDialog { background-color: rgba(12, 12, 12, 235); }"));
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(16, 16, 16, 16);
        label_ = new QLabel(this);
        label_->setAlignment(Qt::AlignCenter);
        layout->addWidget(label_, 1);
        hint_ = new QLabel(this);
        hint_->setAlignment(Qt::AlignCenter);
        hint_->setStyleSheet(QStringLiteral("color: #9a9a9a;"));
        layout->addWidget(hint_);
        showPanel();
    }

  protected:
    void mousePressEvent(QMouseEvent* /*event*/) override { accept(); }
    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Left && index_ > 0) {
            --index_;
            showPanel();
            return;
        }
        if (event->key() == Qt::Key_Right && index_ < panels_.size() - 1) {
            ++index_;
            showPanel();
            return;
        }
        QDialog::keyPressEvent(event); // Esc closes
    }

  private:
    void showPanel() {
        const QSize avail = (screen() != nullptr ? screen()->availableSize()
                                                 : QSize(1280, 800)) * 0.8;
        label_->setPixmap(panels_.at(index_).scaled(avail, Qt::KeepAspectRatio,
                                                    Qt::SmoothTransformation));
        hint_->setText(QStringLiteral("Panel %1 / %2  -  arrows to flip, click or Esc to close")
                           .arg(index_ + 1)
                           .arg(panels_.size()));
        adjustSize();
        if (parentWidget() != nullptr) {
            const QRect host = parentWidget()->window()->frameGeometry();
            move(host.center() - QPoint(width() / 2, height() / 2));
        }
    }

    QVector<QPixmap> panels_;
    int index_ = 0;
    QLabel* label_ = nullptr;
    QLabel* hint_ = nullptr;
};

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

QVector<QRect> ComicView::panelRects() const {
    QVector<QRect> rects;
    if (panels_.isEmpty()) {
        return rects;
    }
    const int gap = 6;
    int rows;
    int cols;
    int edge;
    gridFor(panels_.size(), width(), height(), gap, rows, cols, edge);
    edge = std::max(16, edge);
    const int blockH = rows * edge + gap * (rows - 1);
    const int top = std::max(gap, (height() - blockH) / 2);
    rects.reserve(panels_.size());
    for (int i = 0; i < panels_.size(); ++i) {
        const int r = i / cols;
        const int c = i % cols;
        const int rowCount = std::min<int>(cols, static_cast<int>(panels_.size()) - r * cols);
        const int blockW = rowCount * edge + gap * (rowCount - 1);
        const int left = std::max(gap, (width() - blockW) / 2);
        rects.append(QRect(left + c * (edge + gap), top + r * (edge + gap), edge, edge));
    }
    return rects;
}

int ComicView::panelAt(const QPoint& pos) const {
    const QVector<QRect> rects = panelRects();
    for (int i = 0; i < rects.size(); ++i) {
        if (rects.at(i).contains(pos)) {
            return i;
        }
    }
    return -1;
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
    const QVector<QRect> rects = panelRects();
    for (int i = 0; i < panels_.size(); ++i) {
        painter.drawPixmap(rects.at(i),
                           panels_[i].scaled(rects.at(i).width(), rects.at(i).height(),
                                             Qt::KeepAspectRatio, Qt::SmoothTransformation));
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

void ComicView::mouseDoubleClickEvent(QMouseEvent* event) {
    const int index = panelAt(event->pos());
    if (index >= 0) {
        showZoom(index);
    }
}

void ComicView::showZoom(int index) {
    auto* dialog = new PanelZoomDialog(panels_, index, this);
    dialog->show();
}

void ComicView::contextMenuEvent(QContextMenuEvent* event) {
    if (panels_.isEmpty()) {
        return;
    }
    QMenu menu(this);
    const int index = panelAt(event->pos());
    if (index >= 0) {
        menu.addAction(QStringLiteral("Copy this panel"), this, [this, index]() {
            QApplication::clipboard()->setPixmap(panels_.at(index));
        });
        menu.addAction(QStringLiteral("Save this panel as PNG..."), this, [this, index]() {
            const QString path = QFileDialog::getSaveFileName(
                this, QStringLiteral("Save panel image"),
                QStringLiteral("comic-panel-%1.png").arg(index + 1),
                QStringLiteral("PNG image (*.png)"));
            if (!path.isEmpty()) {
                panels_.at(index).save(path, "PNG");
            }
        });
        menu.addSeparator();
    }
    menu.addAction(QStringLiteral("Copy comic"), this, [this]() {
        const QPixmap composed = sheet();
        if (!composed.isNull()) {
            QApplication::clipboard()->setPixmap(composed);
        }
    });
    menu.addAction(QStringLiteral("Save comic as PNG..."), this,
                   [this]() { emit saveRequested(); });
    if (panels_.size() > 1) {
        menu.addAction(QStringLiteral("Save all panels..."), this, [this]() {
            const QString dir = QFileDialog::getExistingDirectory(
                this, QStringLiteral("Save every panel into folder"));
            if (dir.isEmpty()) {
                return;
            }
            for (int i = 0; i < panels_.size(); ++i) {
                panels_.at(i).save(
                    QDir(dir).filePath(QStringLiteral("comic-panel-%1.png")
                                           .arg(i + 1, 2, 10, QLatin1Char('0'))),
                    "PNG");
            }
        });
    }
    menu.exec(event->globalPos());
}

} // namespace maxchat::ui
