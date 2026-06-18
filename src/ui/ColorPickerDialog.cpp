#include "ui/ColorPickerDialog.h"

#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace maxchat::ui {

namespace {

struct IrcColor {
    int code;
    const char* name;
    const char* hex;
};

constexpr IrcColor IrcColors[] = {
    {0, "White", "#FFFFFF"},      {1, "Black", "#000000"},
    {2, "Navy", "#00007F"},       {3, "Green", "#009300"},
    {4, "Red", "#FF0000"},        {5, "Maroon", "#7F0000"},
    {6, "Purple", "#9C009C"},     {7, "Orange", "#FC7F00"},
    {8, "Yellow", "#FFFF00"},     {9, "Light Green", "#00FC00"},
    {10, "Teal", "#009393"},      {11, "Cyan", "#00FFFF"},
    {12, "Royal Blue", "#0000FC"}, {13, "Pink", "#FF00FF"},
    {14, "Grey", "#7F7F7F"},      {15, "Light Grey", "#D2D2D2"},
};

} // namespace

ColorPickerDialog::ColorPickerDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Insert Color"));

    auto* root = new QVBoxLayout(this);
    auto* hint = new QLabel(tr("Pick a text color (IRC color code)."), this);
    root->addWidget(hint);

    auto* grid = new QGridLayout();
    grid->setSpacing(4);
    for (int index = 0; index < 16; ++index) {
        const IrcColor& color = IrcColors[index];
        auto* swatch = new QPushButton(QString::number(color.code), this);
        swatch->setToolTip(QString::fromLatin1(color.name));
        swatch->setFixedSize(44, 30);
        const bool lightBackground = index == 0 || index == 8 || index == 9 || index == 11 ||
                                     index == 15;
        swatch->setStyleSheet(QStringLiteral("background:%1;color:%2;border:1px solid grey;")
                                  .arg(QString::fromLatin1(color.hex),
                                       lightBackground ? QStringLiteral("black")
                                                       : QStringLiteral("white")));
        connect(swatch, &QPushButton::clicked, this, [this, code = color.code]() {
            selectedCode_ = QStringLiteral("%1").arg(code, 2, 10, QLatin1Char('0'));
            accept();
        });
        grid->addWidget(swatch, index / 8, index % 8);
    }
    root->addLayout(grid);

    auto* cancel = new QPushButton(tr("Cancel"), this);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    auto* bottom = new QVBoxLayout();
    bottom->addWidget(cancel, 0, Qt::AlignRight);
    root->addLayout(bottom);
}

QString ColorPickerDialog::selectedCode() const {
    return selectedCode_;
}

} // namespace maxchat::ui
