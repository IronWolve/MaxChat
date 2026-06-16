#include "ui/QuickConnectDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace maxchat::ui {

QuickConnectDialog::QuickConnectDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Quick Connect"));
    resize(420, 220);

    auto* root = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    host_ = new QLineEdit(this);
    host_->setObjectName(QStringLiteral("host"));
    host_->setPlaceholderText(tr("irc.example.net"));

    port_ = new QSpinBox(this);
    port_->setObjectName(QStringLiteral("port"));
    port_->setRange(1, 65535);
    port_->setValue(6697);

    tls_ = new QCheckBox(tr("SSL/TLS"), this);
    tls_->setObjectName(QStringLiteral("tls"));
    tls_->setChecked(true);

    nick_ = new QLineEdit(QStringLiteral("comicfan"), this);
    nick_->setObjectName(QStringLiteral("nick"));

    channels_ = new QLineEdit(this);
    channels_->setObjectName(QStringLiteral("channels"));
    channels_->setPlaceholderText(tr("#channel #other"));

    form->addRow(tr("Server"), host_);
    form->addRow(tr("Port"), port_);
    form->addRow(QString(), tls_);
    form->addRow(tr("Nickname"), nick_);
    form->addRow(tr("Channels"), channels_);
    root->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connectButton_ = buttons->button(QDialogButtonBox::Ok);
    connectButton_->setText(QStringLiteral("Connect"));
    connectButton_->setEnabled(false);
    connect(buttons, &QDialogButtonBox::accepted, this, &QuickConnectDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QuickConnectDialog::reject);
    root->addWidget(buttons);

    connect(host_, &QLineEdit::textChanged, this, &QuickConnectDialog::syncButtons);
}

maxchat::core::NetworkConfig QuickConnectDialog::network() const {
    const QString host = host_->text().trimmed();
    const QString nick = nick_->text().trimmed();

    maxchat::core::NetworkConfig network;
    network.insert(QStringLiteral("name"), host);
    network.insert(QStringLiteral("host"), host);
    network.insert(QStringLiteral("port"), port_->value());
    network.insert(QStringLiteral("tls"), tls_->isChecked());
    network.insert(QStringLiteral("nick"), nick.isEmpty() ? QStringLiteral("comicfan") : nick);
    network.insert(QStringLiteral("username"), QString());
    network.insert(QStringLiteral("realname"), QString());
    network.insert(QStringLiteral("channels"), channels_->text().trimmed());
    network.insert(QStringLiteral("servers"), QStringList{});
    return network;
}

void QuickConnectDialog::setConnectionValues(const QString& host, int port, bool tls,
                                             const QString& nick, const QString& channels) {
    host_->setText(host);
    port_->setValue(port);
    tls_->setChecked(tls);
    nick_->setText(nick);
    channels_->setText(channels);
    syncButtons();
}

void QuickConnectDialog::syncButtons() {
    if (connectButton_ != nullptr) {
        connectButton_->setEnabled(!host_->text().trimmed().isEmpty());
    }
}

} // namespace maxchat::ui
