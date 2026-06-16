#include "ui/ServerListDialog.h"

#include "core/SettingsStore.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>

#include <utility>

namespace maxchat::ui {

namespace {

QString primaryDomain(const QString& urlText) {
    const QUrl url(urlText);
    const QString host = url.host();
    return host.isEmpty() ? urlText : host;
}

class NetworkEditDialog final : public QDialog {
  public:
    explicit NetworkEditDialog(maxchat::core::NetworkConfig network, QWidget* parent = nullptr)
        : QDialog(parent), network_(std::move(network)) {
        setWindowTitle(network_.isEmpty() ? QStringLiteral("Add Network")
                                          : QStringLiteral("Edit Network"));

        auto* layout = new QVBoxLayout(this);
        auto* form = new QFormLayout();
        layout->addLayout(form);

        const auto section = [this, form](const QString& title) {
            auto* label = new QLabel(title, this);
            label->setStyleSheet(QStringLiteral("font-weight: 700; margin-top: 8px;"));
            form->addRow(label);
        };

        name_ = new QLineEdit(network_.value(QStringLiteral("name")).toString(), this);
        name_->setObjectName(QStringLiteral("networkName"));
        host_ = new QLineEdit(network_.value(QStringLiteral("host")).toString(), this);
        port_ = new QSpinBox(this);
        port_->setRange(1, 65535);
        port_->setValue(network_.value(QStringLiteral("port"), 6667).toInt());
        tls_ = new QCheckBox(tr("SSL/TLS"), this);
        tls_->setChecked(network_.value(QStringLiteral("tls")).toBool());
        acceptCert_ = new QCheckBox(tr("Accept unsigned"), this);
        acceptCert_->setObjectName(QStringLiteral("acceptInvalidCert"));
        acceptCert_->setChecked(network_.value(QStringLiteral("accept_invalid_cert")).toBool());
        acceptCert_->setEnabled(tls_->isChecked());
        acceptCert_->setToolTip(tr(
            "Allow invalid or self-signed TLS certificates for this network. Leave off unless "
            "needed."));
        connect(tls_, &QCheckBox::toggled, acceptCert_, &QCheckBox::setEnabled);
        auto* tlsRow = new QWidget(this);
        auto* tlsLayout = new QHBoxLayout(tlsRow);
        tlsLayout->setContentsMargins(0, 0, 0, 0);
        tlsLayout->addWidget(tls_);
        tlsLayout->addWidget(acceptCert_);
        tlsLayout->addStretch(1);
        website_ = new QLineEdit(network_.value(QStringLiteral("website")).toString(), this);
        servers_ = new QPlainTextEdit(this);
        servers_->setPlainText(
            network_.value(QStringLiteral("servers")).toStringList().join(QLatin1Char('\n')));
        servers_->setMinimumHeight(64);
        servers_->setPlaceholderText(tr("one server per line - host:port or host:+port"));

        nick_ = new QLineEdit(
            network_.value(QStringLiteral("nick"), QStringLiteral("comicfan")).toString(), this);
        realname_ = new QLineEdit(network_.value(QStringLiteral("realname")).toString(), this);
        realname_->setObjectName(QStringLiteral("realname"));
        realname_->setPlaceholderText(
            QStringLiteral("optional - shown in /whois (defaults to your nick)"));
        username_ = new QLineEdit(network_.value(QStringLiteral("username")).toString(), this);
        username_->setObjectName(QStringLiteral("username"));
        username_->setPlaceholderText(tr("optional ident / username"));

        account_ = new QLineEdit(network_.value(QStringLiteral("account")).toString(), this);
        account_->setObjectName(QStringLiteral("nickservAccount"));
        account_->setPlaceholderText(
            QStringLiteral("NickServ account - leave blank to use your nick"));
        password_ = new QLineEdit(network_.value(QStringLiteral("password")).toString(), this);
        password_->setObjectName(QStringLiteral("nickservPassword"));
        password_->setEchoMode(QLineEdit::Password);
        password_->setPlaceholderText(
            QStringLiteral("NickServ password (SASL, with /msg NickServ IDENTIFY fallback)"));
        serverPass_ = new QLineEdit(network_.value(QStringLiteral("server_pass")).toString(), this);
        serverPass_->setObjectName(QStringLiteral("serverPassword"));
        serverPass_->setEchoMode(QLineEdit::Password);
        serverPass_->setPlaceholderText(
            QStringLiteral("server PASS - bouncers/private servers (ZNC, soju, etc.)"));
        allowInsecureAuth_ = new QCheckBox(tr("Allow plaintext auth"), this);
        allowInsecureAuth_->setObjectName(QStringLiteral("allowInsecureAuth"));
        allowInsecureAuth_->setChecked(
            network_.value(QStringLiteral("allow_insecure_auth")).toBool());
        allowInsecureAuth_->setToolTip(tr(
            "Send PASS/SASL/NickServ passwords without SSL/TLS. Leave off unless this network "
            "has no TLS and you accept the risk."));

        channels_ = new QLineEdit(network_.value(QStringLiteral("channels")).toString(), this);
        channels_->setPlaceholderText(tr("#chan #other  (space/comma)"));
        autostart_ = new QCheckBox(tr("Connect on startup"), this);
        autostart_->setObjectName(QStringLiteral("autoconnect"));
        autostart_->setChecked(network_.value(QStringLiteral("autoconnect")).toBool());
        autostart_->setToolTip(tr(
            "Auto-connect this network at launch. The master on/off switch is the "
            "auto-connect checkbox in the Server List."));
        perform_ = new QPlainTextEdit(this);
        perform_->setObjectName(QStringLiteral("perform"));
        perform_->setPlainText(
            network_.value(QStringLiteral("perform")).toStringList().join(QLatin1Char('\n')));
        perform_->setMaximumHeight(56);
        perform_->setPlaceholderText(
            QStringLiteral("commands run on connect, one per line - e.g. /msg NickServ ..."));

        section(QStringLiteral("Connection"));
        form->addRow(tr("Name"), name_);
        form->addRow(tr("Primary server"), host_);
        form->addRow(tr("Port"), port_);
        form->addRow(QString(), tlsRow);
        form->addRow(tr("Failover servers"), servers_);
        form->addRow(tr("Homepage"), website_);
        section(QStringLiteral("Startup"));
        form->addRow(QString(), autostart_);
        form->addRow(tr("Channels"), channels_);
        form->addRow(tr("Perform"), perform_);
        section(QStringLiteral("Identity"));
        form->addRow(tr("Nickname"), nick_);
        form->addRow(tr("Real name"), realname_);
        form->addRow(tr("Username"), username_);
        section(QStringLiteral("Authentication / Bouncers"));
        form->addRow(tr("NickServ account"), account_);
        form->addRow(tr("NickServ password"), password_);
        form->addRow(tr("Server PASS / bouncer"), serverPass_);
        form->addRow(QString(), allowInsecureAuth_);

        proxyType_ = new QComboBox(this);
        proxyType_->setObjectName(QStringLiteral("proxyType"));
        proxyType_->addItem(QStringLiteral("No proxy"), QStringLiteral("none"));
        proxyType_->addItem(QStringLiteral("SOCKS5"), QStringLiteral("socks5"));
        proxyType_->addItem(QStringLiteral("HTTP CONNECT"), QStringLiteral("http"));
        const QString proxyType =
            network_.value(QStringLiteral("proxy_type")).toString().trimmed().toLower();
        const int proxyIndex = proxyType_->findData(proxyType);
        proxyType_->setCurrentIndex(proxyIndex >= 0 ? proxyIndex : 0);
        proxyHost_ = new QLineEdit(network_.value(QStringLiteral("proxy_host")).toString(), this);
        proxyHost_->setPlaceholderText(tr("127.0.0.1"));
        proxyPort_ = new QSpinBox(this);
        proxyPort_->setRange(1, 65535);
        proxyPort_->setValue(network_.value(QStringLiteral("proxy_port"), 1080).toInt());
        auto* proxyServer = new QWidget(this);
        auto* proxyServerLayout = new QHBoxLayout(proxyServer);
        proxyServerLayout->setContentsMargins(0, 0, 0, 0);
        proxyServerLayout->addWidget(proxyHost_, 1);
        proxyServerLayout->addWidget(proxyPort_);
        proxyUser_ =
            new QLineEdit(network_.value(QStringLiteral("proxy_username")).toString(), this);
        proxyUser_->setPlaceholderText(tr("optional"));
        proxyPassword_ =
            new QLineEdit(network_.value(QStringLiteral("proxy_password")).toString(), this);
        proxyPassword_->setEchoMode(QLineEdit::Password);
        proxyPassword_->setPlaceholderText(tr("optional"));
        const auto syncProxyFields = [this]() {
            const bool enabled = proxyType_->currentData().toString() != QStringLiteral("none");
            proxyHost_->setEnabled(enabled);
            proxyPort_->setEnabled(enabled);
            proxyUser_->setEnabled(enabled);
            proxyPassword_->setEnabled(enabled);
        };
        connect(proxyType_, &QComboBox::currentIndexChanged, this, syncProxyFields);
        syncProxyFields();

        section(QStringLiteral("Proxy"));
        form->addRow(tr("Proxy"), proxyType_);
        form->addRow(tr("Proxy server"), proxyServer);
        form->addRow(tr("Proxy username"), proxyUser_);
        form->addRow(tr("Proxy password"), proxyPassword_);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &NetworkEditDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &NetworkEditDialog::reject);
        layout->addWidget(buttons);
        resize(620, 720);
    }

    [[nodiscard]] maxchat::core::NetworkConfig network() const {
        maxchat::core::NetworkConfig network = network_;
        network.insert(QStringLiteral("name"), name_->text().trimmed());
        network.insert(QStringLiteral("host"), host_->text().trimmed());
        network.insert(QStringLiteral("port"), port_->value());
        network.insert(QStringLiteral("tls"), tls_->isChecked());
        network.insert(QStringLiteral("accept_invalid_cert"), acceptCert_->isChecked());
        network.insert(QStringLiteral("website"), website_->text().trimmed());
        network.insert(QStringLiteral("nick"), nick_->text().trimmed());
        network.insert(QStringLiteral("realname"), realname_->text().trimmed());
        network.insert(QStringLiteral("username"), username_->text().trimmed());
        network.insert(QStringLiteral("account"), account_->text().trimmed());
        network.insert(QStringLiteral("password"), password_->text());
        network.insert(QStringLiteral("server_pass"), serverPass_->text());
        network.insert(QStringLiteral("allow_insecure_auth"), allowInsecureAuth_->isChecked());
        network.insert(QStringLiteral("channels"), channels_->text().trimmed());
        network.insert(QStringLiteral("autoconnect"), autostart_->isChecked());

        QStringList performLines;
        const QStringList rawPerform = perform_->toPlainText().split(QLatin1Char('\n'));
        for (const QString& line : rawPerform) {
            const QString trimmedLine = line.trimmed();
            if (!trimmedLine.isEmpty()) {
                performLines.append(trimmedLine);
            }
        }
        network.insert(QStringLiteral("perform"), performLines);
        network.insert(QStringLiteral("proxy_type"), proxyType_->currentData().toString());
        network.insert(QStringLiteral("proxy_host"), proxyHost_->text().trimmed());
        network.insert(QStringLiteral("proxy_port"), proxyPort_->value());
        network.insert(QStringLiteral("proxy_username"), proxyUser_->text().trimmed());
        network.insert(QStringLiteral("proxy_password"), proxyPassword_->text());

        QStringList servers;
        const QStringList lines = servers_->toPlainText().split(QLatin1Char('\n'));
        for (const QString& line : lines) {
            const QString server = line.trimmed();
            if (!server.isEmpty()) {
                servers.append(server);
            }
        }
        network.insert(QStringLiteral("servers"), servers);
        return network;
    }

  private:
    maxchat::core::NetworkConfig network_;
    QLineEdit* name_ = nullptr;
    QLineEdit* host_ = nullptr;
    QSpinBox* port_ = nullptr;
    QCheckBox* tls_ = nullptr;
    QCheckBox* acceptCert_ = nullptr;
    QLineEdit* website_ = nullptr;
    QLineEdit* nick_ = nullptr;
    QLineEdit* realname_ = nullptr;
    QLineEdit* username_ = nullptr;
    QLineEdit* account_ = nullptr;
    QLineEdit* password_ = nullptr;
    QLineEdit* serverPass_ = nullptr;
    QCheckBox* allowInsecureAuth_ = nullptr;
    QCheckBox* autostart_ = nullptr;
    QPlainTextEdit* perform_ = nullptr;
    QLineEdit* channels_ = nullptr;
    QPlainTextEdit* servers_ = nullptr;
    QComboBox* proxyType_ = nullptr;
    QLineEdit* proxyHost_ = nullptr;
    QSpinBox* proxyPort_ = nullptr;
    QLineEdit* proxyUser_ = nullptr;
    QLineEdit* proxyPassword_ = nullptr;
};

} // namespace

ServerListDialog::ServerListDialog(maxchat::core::NetworkConfigList networks, bool connectOnStart,
                                   QWidget* parent)
    : QDialog(parent), networks_(std::move(networks)) {
    buildUi();
    connectOnStart_->setChecked(connectOnStart);
    refreshList();
}

maxchat::core::NetworkConfigList ServerListDialog::networks() const {
    return networks_;
}

maxchat::core::NetworkConfig ServerListDialog::selectedNetwork() const {
    const int row = currentRow();
    return row >= 0 && row < networks_.size() ? networks_.at(row) : maxchat::core::NetworkConfig{};
}

bool ServerListDialog::connectOnStart() const {
    return connectOnStart_->isChecked();
}

bool ServerListDialog::connectWasRequested() const {
    return connectRequested_;
}

QString ServerListDialog::currentHomepageButtonText() const {
    return homepageButton_->text();
}

void ServerListDialog::setCurrentRow(int row) {
    list_->setCurrentRow(row);
    syncSelection();
}

bool ServerListDialog::moveCurrentNetwork(int delta) {
    const int row = currentRow();
    const int next = row + delta;
    if (row < 0 || row >= networks_.size() || next < 0 || next >= networks_.size()) {
        return false;
    }
    networks_.swapItemsAt(row, next);
    refreshList(next);
    return true;
}

bool ServerListDialog::removeCurrentNetwork() {
    const int row = currentRow();
    if (row < 0 || row >= networks_.size()) {
        return false;
    }
    networks_.removeAt(row);
    refreshList(qMin(row, networks_.size() - 1));
    return true;
}

void ServerListDialog::resetToDefaults() {
    networks_ = maxchat::core::defaultNetworkConfigs();
    refreshList();
}

QString ServerListDialog::networkLabel(const maxchat::core::NetworkConfig& network) {
    const QString name = network.value(QStringLiteral("name")).toString();
    const QString host = network.value(QStringLiteral("host")).toString();
    const QString website = network.value(QStringLiteral("website")).toString();
    const QString title = name.isEmpty() ? host : name;
    const QString server = host.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(host);
    const QString url =
        website.isEmpty() ? QString() : QStringLiteral("  %1").arg(primaryDomain(website));
    return title + server + url;
}

QString ServerListDialog::homepageButtonText(const maxchat::core::NetworkConfig& network) {
    const QString website = network.value(QStringLiteral("website")).toString().trimmed();
    if (website.isEmpty()) {
        return QStringLiteral("Homepage");
    }
    return website.size() <= 60 ? website : primaryDomain(website);
}

void ServerListDialog::addNetwork() {
    maxchat::core::NetworkConfig network;
    network.insert(QStringLiteral("nick"), QStringLiteral("comicfan"));
    network.insert(QStringLiteral("port"), 6697);
    network.insert(QStringLiteral("tls"), true);
    if (editNetwork(network)) {
        networks_.append(network);
        refreshList(networks_.size() - 1);
    }
}

void ServerListDialog::editCurrentNetwork() {
    const int row = currentRow();
    if (row < 0 || row >= networks_.size()) {
        return;
    }
    maxchat::core::NetworkConfig network = networks_.at(row);
    if (editNetwork(network)) {
        networks_[row] = network;
        refreshList(row);
    }
}

void ServerListDialog::openHomepage() {
    const QString website = selectedNetwork().value(QStringLiteral("website")).toString().trimmed();
    if (website.isEmpty()) {
        return;
    }
    QUrl url = QUrl(website);
    if (url.scheme().isEmpty()) {
        url = QUrl(QStringLiteral("https://") + website);
    }
    if (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https")) {
        return;
    }
    QDesktopServices::openUrl(url);
}

void ServerListDialog::requestConnect() {
    connectRequested_ = true;
    accept();
}

void ServerListDialog::syncSelection() {
    const int row = currentRow();
    const bool hasSelection = row >= 0 && row < networks_.size();
    const maxchat::core::NetworkConfig network = selectedNetwork();
    homepageButton_->setText(homepageButtonText(network));
    homepageButton_->setEnabled(hasSelection &&
                                !network.value(QStringLiteral("website")).toString().isEmpty());
    editButton_->setEnabled(hasSelection);
    deleteButton_->setEnabled(hasSelection);
    upButton_->setEnabled(hasSelection && row > 0);
    downButton_->setEnabled(hasSelection && row + 1 < networks_.size());
    connectButton_->setEnabled(hasSelection);
}

void ServerListDialog::buildUi() {
    setWindowTitle(tr("Server List"));
    auto* root = new QVBoxLayout(this);

    countLabel_ = new QLabel(this);
    root->addWidget(countLabel_);

    list_ = new QListWidget(this);
    list_->setMinimumSize(680, 420);
    connect(list_, &QListWidget::currentRowChanged, this, &ServerListDialog::syncSelection);
    connect(list_, &QListWidget::itemDoubleClicked, this, &ServerListDialog::requestConnect);
    root->addWidget(list_);

    auto* tools = new QHBoxLayout();
    auto* addButton = new QPushButton(tr("Add..."), this);
    editButton_ = new QPushButton(tr("Edit..."), this);
    deleteButton_ = new QPushButton(tr("Delete"), this);
    upButton_ = new QPushButton(tr("Up"), this);
    downButton_ = new QPushButton(tr("Down"), this);
    auto* resetButton = new QPushButton(tr("Reset Defaults"), this);
    tools->addWidget(addButton);
    tools->addWidget(editButton_);
    tools->addWidget(deleteButton_);
    tools->addSpacing(16);
    tools->addWidget(upButton_);
    tools->addWidget(downButton_);
    tools->addStretch(1);
    tools->addWidget(resetButton);
    root->addLayout(tools);

    connect(addButton, &QPushButton::clicked, this, &ServerListDialog::addNetwork);
    connect(editButton_, &QPushButton::clicked, this, &ServerListDialog::editCurrentNetwork);
    connect(deleteButton_, &QPushButton::clicked, this,
            [this]() { [[maybe_unused]] const bool removed = removeCurrentNetwork(); });
    connect(upButton_, &QPushButton::clicked, this,
            [this]() { [[maybe_unused]] const bool moved = moveCurrentNetwork(-1); });
    connect(downButton_, &QPushButton::clicked, this,
            [this]() { [[maybe_unused]] const bool moved = moveCurrentNetwork(1); });
    connect(resetButton, &QPushButton::clicked, this, &ServerListDialog::resetToDefaults);

    connectOnStart_ = new QCheckBox(tr("Auto-connect on startup"), this);
    root->addWidget(connectOnStart_);

    auto* bottom = new QHBoxLayout();
    connectButton_ = new QPushButton(tr("Connect"), this);
    homepageButton_ = new QPushButton(tr("Homepage"), this);
    bottom->addWidget(connectButton_);
    bottom->addStretch(1);
    bottom->addWidget(homepageButton_);
    bottom->addStretch(1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    bottom->addWidget(buttons);
    root->addLayout(bottom);

    connect(connectButton_, &QPushButton::clicked, this, &ServerListDialog::requestConnect);
    connect(homepageButton_, &QPushButton::clicked, this, &ServerListDialog::openHomepage);
    connect(buttons, &QDialogButtonBox::accepted, this, &ServerListDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &ServerListDialog::reject);
}

void ServerListDialog::refreshList(int rowToSelect) {
    list_->clear();
    for (const auto& network : networks_) {
        list_->addItem(networkLabel(network));
    }
    countLabel_->setText(QStringLiteral("%1 networks").arg(networks_.size()));
    if (!networks_.isEmpty()) {
        list_->setCurrentRow(qBound(0, rowToSelect, networks_.size() - 1));
    }
    syncSelection();
}

int ServerListDialog::currentRow() const {
    return list_->currentRow();
}

bool ServerListDialog::editNetwork(maxchat::core::NetworkConfig& network) {
    NetworkEditDialog dialog(network, this);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    network = dialog.network();
    return true;
}

} // namespace maxchat::ui
