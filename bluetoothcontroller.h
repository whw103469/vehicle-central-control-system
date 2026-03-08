#ifndef BLUETOOTHCONTROLLER_H
#define BLUETOOTHCONTROLLER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QTimer>
#include <QBluetoothDeviceInfo>
#include <QBluetoothAddress>
#include <QBluetoothDeviceDiscoveryAgent>
#include <QDBusInterface>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusReply>
#include <QDBusConnection>
#include <QDBusMessage>

class BluetoothController : public QObject
{
    Q_OBJECT
public:
    explicit BluetoothController(QObject *parent = nullptr);
    ~BluetoothController() override;

    void startScan();
    void stopScan();
    QList<QBluetoothDeviceInfo> devices() const;
    bool connectToAddress(const QString &address);
    bool isConnected() const;
    QString connectedDeviceName() const;

    bool play();
    bool pause();
    bool next();
    bool previous();
    bool seekMs(qint64 ms);

    bool playing() const;
    qint64 position() const;
    qint64 duration() const;
    QString trackTitle() const;
    QString trackArtist() const;

signals:
    void scanUpdated();
    void connectionChanged(bool connected);
    void pairingChanged(bool paired);
    void servicesResolvedChanged(bool resolved);
    void uuidsChanged(const QStringList &uuids);
    void trackChanged();
    void positionChanged();
    void durationChanged();
    void playingChanged();
    void errorOccurred(const QString &message);

private slots:
    void handleDeviceDiscovered(const QBluetoothDeviceInfo &info);
    void handleScanFinished();
    void refreshMetadata();
    void onConnectFinished(QDBusPendingCallWatcher *watcher);
    void onGetObjectsFinished(QDBusPendingCallWatcher *watcher);
    void onGetPropTrackFinished(QDBusPendingCallWatcher *watcher);
    void onGetPropPositionFinished(QDBusPendingCallWatcher *watcher);
    void onGetPropStatusFinished(QDBusPendingCallWatcher *watcher);
    void onPropertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);

private:
    QString findAdapterPath() const;
    QString toDevicePath(const QString &adapterPath, const QString &addr) const;
    void deviceConnectAsync(const QString &devicePath);
    void locateMediaInterfacesAsync();
    QVariant getProperty(const QString &path, const QString &interface, const QString &prop) const;
    bool callControl(const QString &method);
    void subscribeProperties();

    QBluetoothDeviceDiscoveryAgent *m_agent;
    QList<QBluetoothDeviceInfo> m_devices;
    QString m_adapterPath;
    QString m_devicePath;
    QString m_playerPath;
    QString m_controlPath;
    bool m_connected;
    QString m_deviceName;
    QString m_title;
    QString m_artist;
    qint64 m_positionMs;
    qint64 m_durationMs;
    bool m_playing;
    QTimer m_metaTimer;
};

#endif
