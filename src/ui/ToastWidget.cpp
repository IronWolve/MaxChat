#include "ToastWidget.h"

#include <QApplication>
#include <QCloseEvent>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>

namespace maxchat::ui {

static constexpr int ToastWidth = 340;

ToastWidget::ToastWidget(const QString& title, const QString& body,
                         const QColor& bg, const QColor& fg, const QColor& accent,
                         int durationMs, const QIcon& icon,
                         std::function<void()> onClick,
                         QWidget* parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint)
    , m_onClick(std::move(onClick))
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_DeleteOnClose);

    auto* card = new QFrame(this);
    card->setObjectName(QStringLiteral("toastCard"));
    card->setStyleSheet(
        QStringLiteral("#toastCard{background:%1;border-radius:10px;border-left:4px solid %2;}"
                       "QLabel{background:transparent;color:%3;}")
            .arg(bg.name(), accent.name(), fg.name()));

    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(26);
    shadow->setColor(QColor(0, 0, 0, 170));
    shadow->setOffset(0, 3);
    card->setGraphicsEffect(shadow);

    auto* row = new QHBoxLayout(card);
    row->setContentsMargins(14, 11, 13, 11);
    row->setSpacing(11);

    if (!icon.isNull()) {
        auto* iconLabel = new QLabel(card);
        iconLabel->setPixmap(icon.pixmap(28, 28));
        iconLabel->setFixedSize(30, 30);
        iconLabel->setAlignment(Qt::AlignTop);
        row->addWidget(iconLabel);
    }

    auto* textCol = new QVBoxLayout();
    textCol->setSpacing(2);

    auto* titleLabel = new QLabel(title, card);
    titleLabel->setTextFormat(Qt::PlainText); // nick is remote text — never rich
    QFont titleFont = titleLabel->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 1);
    titleLabel->setFont(titleFont);
    titleLabel->setStyleSheet(QStringLiteral("color:%1;background:transparent;").arg(accent.name()));
    textCol->addWidget(titleLabel);

    auto* bodyLabel = new QLabel(body, card);
    bodyLabel->setTextFormat(Qt::PlainText); // message body likewise
    bodyLabel->setWordWrap(true);
    bodyLabel->setStyleSheet(QStringLiteral("background:transparent;"));
    textCol->addWidget(bodyLabel);

    row->addLayout(textCol, 1);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(13, 13, 13, 13);
    outer->addWidget(card);

    setFixedWidth(ToastWidth);
    adjustSize();

    QTimer::singleShot(qMax(1000, durationMs), this, &QWidget::close);
}

void ToastWidget::mousePressEvent(QMouseEvent* event) {
    Q_UNUSED(event);
    auto cb = std::move(m_onClick);
    close();
    if (cb) cb();
}

void ToastWidget::closeEvent(QCloseEvent* event) {
    emit done(this);
    QWidget::closeEvent(event);
}

} // namespace maxchat::ui