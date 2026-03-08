#ifndef BLUETOOTHDIALOG_H
#define BLUETOOTHDIALOG_H

#include <QDialog>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothDeviceInfo>

class BluetoothController;

class BluetoothDialog : public QDialog
{
    Q_OBJECT
public:
    explicit BluetoothDialog(BluetoothController *controller, QWidget *parent = nullptr);
    ~BluetoothDialog() override;

private slots:
    void handleScan();
    void handleStop();
    void handleConnect();
    void handleDeviceDiscovered(const QBluetoothDeviceInfo &info);
    void handleScanFinished();
    void handleControllerConnected(bool ok);
    void handleControllerError(const QString &message);
    void handlePairedChanged(bool paired);
    void handleServicesResolvedChanged(bool resolved);
    void handleUuidsChanged(const QStringList &uuids);

private:
    void populateList();
    QString selectedAddress() const;

    BluetoothController *m_controller;
    QBluetoothDeviceDiscoveryAgent *m_agent;
    QListWidget *m_list;
    QPushButton *m_btnScan;
    QPushButton *m_btnStop;
    QPushButton *m_btnConnect;
    QPushButton *m_btnCancel;
    QLabel *m_statusLabel;
    QLabel *m_errorLabel;
    bool m_connected;
    bool m_paired;
    bool m_servicesResolved;
};

#endif
