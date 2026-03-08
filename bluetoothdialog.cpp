#include "bluetoothdialog.h"
#include "bluetoothcontroller.h"

BluetoothDialog::BluetoothDialog(BluetoothController *controller, QWidget *parent)
    : QDialog(parent)
    , m_controller(controller)
    , m_agent(new QBluetoothDeviceDiscoveryAgent(this))
    , m_list(new QListWidget(this))
    , m_btnScan(new QPushButton(QStringLiteral("扫描"), this))
    , m_btnStop(new QPushButton(QStringLiteral("停止"), this))
    , m_btnConnect(new QPushButton(QStringLiteral("连接"), this))
    , m_btnCancel(new QPushButton(QStringLiteral("取消"), this))
    , m_connected(false)
    , m_paired(false)
    , m_servicesResolved(false)
{
    setWindowTitle(QStringLiteral("蓝牙设备"));
    QVBoxLayout *vl = new QVBoxLayout(this);
    QHBoxLayout *hl = new QHBoxLayout;
    hl->addWidget(m_btnScan);
    hl->addWidget(m_btnStop);
    hl->addWidget(m_btnConnect);
    hl->addWidget(m_btnCancel);
    m_statusLabel = new QLabel(this);
    m_errorLabel = new QLabel(this);
    m_errorLabel->setStyleSheet(QStringLiteral("color: #F44336;"));
    vl->addWidget(new QLabel(QStringLiteral("可用设备"), this));
    vl->addWidget(m_list);
    vl->addWidget(m_statusLabel);
    vl->addWidget(m_errorLabel);
    vl->addLayout(hl);

    connect(m_btnScan, &QPushButton::clicked, this, &BluetoothDialog::handleScan);
    connect(m_btnStop, &QPushButton::clicked, this, &BluetoothDialog::handleStop);
    connect(m_btnConnect, &QPushButton::clicked, this, &BluetoothDialog::handleConnect);
    connect(m_btnCancel, &QPushButton::clicked, this, &BluetoothDialog::reject);
    connect(m_agent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered, this, &BluetoothDialog::handleDeviceDiscovered);
    connect(m_agent, &QBluetoothDeviceDiscoveryAgent::finished, this, &BluetoothDialog::handleScanFinished);
    connect(m_controller, &BluetoothController::connectionChanged, this, &BluetoothDialog::handleControllerConnected);
    connect(m_controller, &BluetoothController::errorOccurred, this, &BluetoothDialog::handleControllerError);
    connect(m_controller, &BluetoothController::pairingChanged, this, &BluetoothDialog::handlePairedChanged);
    connect(m_controller, &BluetoothController::servicesResolvedChanged, this, &BluetoothDialog::handleServicesResolvedChanged);
    connect(m_controller, &BluetoothController::uuidsChanged, this, &BluetoothDialog::handleUuidsChanged);
}

BluetoothDialog::~BluetoothDialog()
{
}

void BluetoothDialog::handleScan()
{
    m_list->clear();
    m_errorLabel->clear();
    m_statusLabel->setText(QStringLiteral("扫描中…"));
    m_agent->start();
}

void BluetoothDialog::handleStop()
{
    m_agent->stop();
    m_statusLabel->setText(QString());
}

void BluetoothDialog::handleConnect()
{
    QString addr = selectedAddress();
    if (addr.isEmpty()) return;
    m_errorLabel->clear();
    m_statusLabel->setText(QStringLiteral("连接中…"));
    m_btnConnect->setEnabled(false);
    m_btnScan->setEnabled(false);
    m_btnStop->setEnabled(false);
    m_controller->connectToAddress(addr);
}

void BluetoothDialog::handleDeviceDiscovered(const QBluetoothDeviceInfo &info)
{
    QString name = info.name();
    QString addr = info.address().toString();
    QList<QListWidgetItem*> found = m_list->findItems(addr, Qt::MatchContains);
    if (!found.isEmpty()) return;
    QListWidgetItem *it = new QListWidgetItem(QStringLiteral("%1  (%2)").arg(name, addr), m_list);
    it->setData(Qt::UserRole, addr);
}

void BluetoothDialog::handleScanFinished()
{
    m_statusLabel->setText(QString());
}

void BluetoothDialog::handleControllerConnected(bool ok)
{
    m_connected = ok;
    if (ok) {
        m_statusLabel->setText(QStringLiteral("已连接"));
        if (m_servicesResolved) {
            accept();
            return;
        }
    } else {
        m_statusLabel->setText(QString());
    }
    m_btnConnect->setEnabled(true);
    m_btnScan->setEnabled(true);
    m_btnStop->setEnabled(true);
}

void BluetoothDialog::handleControllerError(const QString &message)
{
    m_statusLabel->setText(QString());
    m_errorLabel->setText(message);
    m_btnConnect->setEnabled(true);
    m_btnScan->setEnabled(true);
    m_btnStop->setEnabled(true);
}

void BluetoothDialog::handlePairedChanged(bool paired)
{
    m_paired = paired;
    if (paired) {
        m_statusLabel->setText(QStringLiteral("已配对"));
    }
}

void BluetoothDialog::handleServicesResolvedChanged(bool resolved)
{
    m_servicesResolved = resolved;
    if (resolved) {
        m_statusLabel->setText(QStringLiteral("服务可用"));
        if (m_connected) {
            accept();
        }
    }
}

void BluetoothDialog::handleUuidsChanged(const QStringList &uuids)
{
    Q_UNUSED(uuids);
}

QString BluetoothDialog::selectedAddress() const
{
    QListWidgetItem *it = m_list->currentItem();
    if (!it) return QString();
    return it->data(Qt::UserRole).toString();
}
