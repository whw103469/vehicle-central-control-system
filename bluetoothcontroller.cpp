#include "bluetoothcontroller.h"

BluetoothController::BluetoothController(QObject *parent)
    : QObject(parent)
    , m_agent(new QBluetoothDeviceDiscoveryAgent(this))
    , m_connected(false)
    , m_positionMs(0)
    , m_durationMs(0)
    , m_playing(false)
{
    connect(m_agent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered, this, &BluetoothController::handleDeviceDiscovered);
    connect(m_agent, &QBluetoothDeviceDiscoveryAgent::finished, this, &BluetoothController::handleScanFinished);
    connect(&m_metaTimer, &QTimer::timeout, this, &BluetoothController::refreshMetadata);
    m_metaTimer.setInterval(1000);
}

BluetoothController::~BluetoothController()
{
}

void BluetoothController::startScan()
{
    m_devices.clear();
    m_agent->start();
}

void BluetoothController::stopScan()
{
    m_agent->stop();
}

QList<QBluetoothDeviceInfo> BluetoothController::devices() const
{
    return m_devices;
}

void BluetoothController::handleDeviceDiscovered(const QBluetoothDeviceInfo &info)
{
    for (const auto &d : m_devices) {
        if (d.address() == info.address()) return;
    }
    m_devices.append(info);
    emit scanUpdated();
}

void BluetoothController::handleScanFinished()
{
    emit scanUpdated();
}

bool BluetoothController::connectToAddress(const QString &address)
{
    m_adapterPath = findAdapterPath();
    if (m_adapterPath.isEmpty()) {
        emit errorOccurred(QStringLiteral("蓝牙适配器未找到"));
        return false;
    }
    m_devicePath = toDevicePath(m_adapterPath, address);
    deviceConnectAsync(m_devicePath);
    return true;
}

bool BluetoothController::isConnected() const
{
    return m_connected;
}

QString BluetoothController::connectedDeviceName() const
{
    return m_deviceName;
}

QString BluetoothController::findAdapterPath() const
{
    QDBusInterface obj(QStringLiteral("org.bluez"), QStringLiteral("/"),
                       QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                       QDBusConnection::systemBus());
    QDBusReply<QVariantMap> reply = obj.call(QStringLiteral("GetManagedObjects"));
    if (!reply.isValid()) {
        QDBusReply<QVariant> r = obj.call(QStringLiteral("GetManagedObjects"));
        if (!r.isValid()) return QString();
    }
    QDBusMessage msg = obj.call(QStringLiteral("GetManagedObjects"));
    if (msg.type() != QDBusMessage::ReplyMessage) return QString();
    const auto args = msg.arguments();
    if (args.isEmpty()) return QString();
    const auto map = qdbus_cast<QVariantMap>(args.at(0));
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
        const QString path = it.key();
        const QVariantMap ifaces = it.value().toMap();
        if (ifaces.contains(QStringLiteral("org.bluez.Adapter1"))) {
            return path;
        }
    }
    return QString();
}

QString BluetoothController::toDevicePath(const QString &adapterPath, const QString &addr) const
{
    QString a = addr;
    QString b = a;
    b.replace(':', '_');
    QString base = adapterPath;
    if (!base.startsWith(QStringLiteral("/org/bluez"))) base = QStringLiteral("/org/bluez");
    QString ap = adapterPath;
    if (!ap.startsWith(QStringLiteral("/org/bluez"))) ap = QStringLiteral("/org/bluez/hci0");
    QString hci = ap.section('/', 3, 3);
    if (hci.isEmpty()) hci = QStringLiteral("hci0");
    return QStringLiteral("/org/bluez/%1/dev_%2").arg(hci, b);
}

void BluetoothController::deviceConnectAsync(const QString &devicePath)
{
    QDBusInterface dev(QStringLiteral("org.bluez"), devicePath,
                       QStringLiteral("org.bluez.Device1"),
                       QDBusConnection::systemBus());
    QDBusPendingCall p = dev.asyncCall(QStringLiteral("Connect"));
    QDBusPendingCallWatcher *w = new QDBusPendingCallWatcher(p, this);
    connect(w, &QDBusPendingCallWatcher::finished, this, &BluetoothController::onConnectFinished);
}

void BluetoothController::locateMediaInterfacesAsync()
{
    QDBusInterface obj(QStringLiteral("org.bluez"), QStringLiteral("/"),
                       QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                       QDBusConnection::systemBus());
    QDBusPendingCall p = obj.asyncCall(QStringLiteral("GetManagedObjects"));
    QDBusPendingCallWatcher *w = new QDBusPendingCallWatcher(p, this);
    connect(w, &QDBusPendingCallWatcher::finished, this, &BluetoothController::onGetObjectsFinished);
}

QVariant BluetoothController::getProperty(const QString &path, const QString &interface, const QString &prop) const
{
    QDBusInterface props(QStringLiteral("org.bluez"), path,
                         QStringLiteral("org.freedesktop.DBus.Properties"),
                         QDBusConnection::systemBus());
    QDBusReply<QVariant> r = props.call(QStringLiteral("Get"), interface, prop);
    if (!r.isValid()) return QVariant();
    return r.value();
}

bool BluetoothController::callControl(const QString &method)
{
    QString ctrl = m_controlPath;
    if (ctrl.isEmpty()) ctrl = m_playerPath;
    if (ctrl.isEmpty()) return false;
    QDBusInterface iface(QStringLiteral("org.bluez"), ctrl,
                         ctrl == m_controlPath ? QStringLiteral("org.bluez.MediaControl1")
                                               : QStringLiteral("org.bluez.MediaPlayer1"),
                         QDBusConnection::systemBus());
    QDBusReply<void> r = iface.call(method);
    return r.isValid();
}

void BluetoothController::onConnectFinished(QDBusPendingCallWatcher *watcher)
{
    if (watcher->isError()) {
        emit errorOccurred(watcher->error().message());
        watcher->deleteLater();
        return;
    }
    watcher->deleteLater();
    locateMediaInterfacesAsync();
}

void BluetoothController::onGetObjectsFinished(QDBusPendingCallWatcher *watcher)
{
    if (watcher->isError()) {
        emit errorOccurred(watcher->error().message());
        watcher->deleteLater();
        return;
    }
    QDBusMessage msg = watcher->reply();
    watcher->deleteLater();
    if (msg.type() != QDBusMessage::ReplyMessage || msg.arguments().isEmpty()) {
        emit errorOccurred(QStringLiteral("获取对象失败"));
        return;
    }
    const auto map = qdbus_cast<QVariantMap>(msg.arguments().at(0));
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
        const QString path = it.key();
        if (!path.startsWith(m_devicePath)) continue;
        const QVariantMap ifaces = it.value().toMap();
        if (m_playerPath.isEmpty() && ifaces.contains(QStringLiteral("org.bluez.MediaPlayer1"))) {
            m_playerPath = path;
        }
        if (m_controlPath.isEmpty() && ifaces.contains(QStringLiteral("org.bluez.MediaControl1"))) {
            m_controlPath = path;
        }
    }
    if (m_playerPath.isEmpty() && m_controlPath.isEmpty()) {
        emit errorOccurred(QStringLiteral("未找到媒体接口"));
        return;
    }
    QVariant n = getProperty(m_devicePath, QStringLiteral("org.bluez.Device1"), QStringLiteral("Name"));
    if (n.isValid()) m_deviceName = n.toString();
    m_connected = true;
    emit connectionChanged(true);
    subscribeProperties();
    m_metaTimer.stop();
    refreshMetadata();
}

void BluetoothController::onGetPropTrackFinished(QDBusPendingCallWatcher *watcher)
{
    if (!watcher->isError()) {
        QDBusMessage msg = watcher->reply();
        if (msg.type() == QDBusMessage::ReplyMessage && !msg.arguments().isEmpty()) {
            QVariant v = msg.arguments().at(0);
            QVariantMap m = v.value<QDBusVariant>().variant().toMap();
            QString t = m.value(QStringLiteral("Title")).toString();
            QString a = m.value(QStringLiteral("Artist")).toString();
            qint64 d = m.value(QStringLiteral("Duration")).toLongLong();
            bool changed = false;
            if (t != m_title) { m_title = t; changed = true; }
            if (a != m_artist) { m_artist = a; changed = true; }
            if (d != m_durationMs) { m_durationMs = d; emit durationChanged(); changed = true; }
            if (changed) emit trackChanged();
        }
    }
    watcher->deleteLater();
}

void BluetoothController::onGetPropPositionFinished(QDBusPendingCallWatcher *watcher)
{
    if (!watcher->isError()) {
        QDBusMessage msg = watcher->reply();
        if (msg.type() == QDBusMessage::ReplyMessage && !msg.arguments().isEmpty()) {
            QVariant v = msg.arguments().at(0);
            qint64 p = v.value<QDBusVariant>().variant().toLongLong();
            if (p != m_positionMs) {
                m_positionMs = p;
                emit positionChanged();
            }
        }
    }
    watcher->deleteLater();
}

void BluetoothController::onGetPropStatusFinished(QDBusPendingCallWatcher *watcher)
{
    if (!watcher->isError()) {
        QDBusMessage msg = watcher->reply();
        if (msg.type() == QDBusMessage::ReplyMessage && !msg.arguments().isEmpty()) {
            QVariant v = msg.arguments().at(0);
            QString s = v.value<QDBusVariant>().variant().toString().toLower();
            bool pl = (s == QStringLiteral("playing"));
            if (pl != m_playing) {
                m_playing = pl;
                emit playingChanged();
            }
        }
    }
    watcher->deleteLater();
}

void BluetoothController::subscribeProperties()
{
    if (m_playerPath.isEmpty()) return;
    QDBusConnection::systemBus().connect(QStringLiteral("org.bluez"),
                                         m_playerPath,
                                         QStringLiteral("org.freedesktop.DBus.Properties"),
                                         QStringLiteral("PropertiesChanged"),
                                         this,
                                         SLOT(onPropertiesChanged(QString,QVariantMap,QStringList)));
    if (!m_devicePath.isEmpty()) {
        QDBusConnection::systemBus().connect(QStringLiteral("org.bluez"),
                                             m_devicePath,
                                             QStringLiteral("org.freedesktop.DBus.Properties"),
                                             QStringLiteral("PropertiesChanged"),
                                             this,
                                             SLOT(onPropertiesChanged(QString,QVariantMap,QStringList)));
    }
}

void BluetoothController::onPropertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated)
{
    if (interface == QStringLiteral("org.bluez.Device1")) {
        if (changed.contains(QStringLiteral("Connected"))) {
            QVariant v = changed.value(QStringLiteral("Connected"));
            bool c = v.value<QDBusVariant>().variant().toBool();
            if (c != m_connected) {
                m_connected = c;
                emit connectionChanged(c);
            }
        }
        if (changed.contains(QStringLiteral("Paired"))) {
            QVariant v = changed.value(QStringLiteral("Paired"));
            bool p = v.value<QDBusVariant>().variant().toBool();
            emit pairingChanged(p);
        }
        if (changed.contains(QStringLiteral("ServicesResolved"))) {
            QVariant v = changed.value(QStringLiteral("ServicesResolved"));
            bool r = v.value<QDBusVariant>().variant().toBool();
            emit servicesResolvedChanged(r);
        }
        if (changed.contains(QStringLiteral("UUIDs"))) {
            QVariant v = changed.value(QStringLiteral("UUIDs"));
            QStringList list = v.value<QDBusVariant>().variant().toStringList();
            emit uuidsChanged(list);
        }
        return;
    }
    if (interface != QStringLiteral("org.bluez.MediaPlayer1")) return;
    bool emitTrack = false;
    if (changed.contains(QStringLiteral("Track"))) {
        QVariant v = changed.value(QStringLiteral("Track"));
        QVariantMap m = v.value<QDBusVariant>().variant().toMap();
        QString t = m.value(QStringLiteral("Title")).toString();
        QString a = m.value(QStringLiteral("Artist")).toString();
        qint64 d = m.value(QStringLiteral("Duration")).toLongLong();
        if (t != m_title) { m_title = t; emitTrack = true; }
        if (a != m_artist) { m_artist = a; emitTrack = true; }
        if (d != m_durationMs) { m_durationMs = d; emit durationChanged(); emitTrack = true; }
    }
    if (emitTrack) emit trackChanged();
    if (changed.contains(QStringLiteral("Position"))) {
        QVariant v = changed.value(QStringLiteral("Position"));
        qint64 p = v.value<QDBusVariant>().variant().toLongLong();
        if (p != m_positionMs) {
            m_positionMs = p;
            emit positionChanged();
        }
    }
    if (changed.contains(QStringLiteral("Status"))) {
        QVariant v = changed.value(QStringLiteral("Status"));
        QString s = v.value<QDBusVariant>().variant().toString().toLower();
        bool pl = (s == QStringLiteral("playing"));
        if (pl != m_playing) {
            m_playing = pl;
            emit playingChanged();
        }
    }
}

bool BluetoothController::play()
{
    bool ok = callControl(QStringLiteral("Play"));
    refreshMetadata();
    return ok;
}

bool BluetoothController::pause()
{
    bool ok = callControl(QStringLiteral("Pause"));
    refreshMetadata();
    return ok;
}

bool BluetoothController::next()
{
    bool ok = callControl(QStringLiteral("Next"));
    refreshMetadata();
    return ok;
}

bool BluetoothController::previous()
{
    bool ok = callControl(QStringLiteral("Previous"));
    refreshMetadata();
    return ok;
}

bool BluetoothController::seekMs(qint64 ms)
{
    if (m_playerPath.isEmpty()) return false;
    QDBusInterface iface(QStringLiteral("org.bluez"), m_playerPath,
                         QStringLiteral("org.bluez.MediaPlayer1"),
                         QDBusConnection::systemBus());
    QDBusReply<void> r = iface.call(QStringLiteral("Seek"), static_cast<qlonglong>(ms));
    bool ok = r.isValid();
    refreshMetadata();
    return ok;
}

bool BluetoothController::playing() const
{
    QVariant v = getProperty(m_playerPath, QStringLiteral("org.bluez.MediaPlayer1"), QStringLiteral("Status"));
    if (!v.isValid()) return m_playing;
    QString s = v.toString().toLower();
    return s == QStringLiteral("playing");
}

qint64 BluetoothController::position() const
{
    QVariant v = getProperty(m_playerPath, QStringLiteral("org.bluez.MediaPlayer1"), QStringLiteral("Position"));
    if (!v.isValid()) return m_positionMs;
    return v.toLongLong();
}

qint64 BluetoothController::duration() const
{
    QVariant v = getProperty(m_playerPath, QStringLiteral("org.bluez.MediaPlayer1"), QStringLiteral("Track"));
    if (!v.isValid()) return m_durationMs;
    QVariantMap m = v.toMap();
    if (m.contains(QStringLiteral("Duration"))) return m.value(QStringLiteral("Duration")).toLongLong();
    return m_durationMs;
}

QString BluetoothController::trackTitle() const
{
    QVariant v = getProperty(m_playerPath, QStringLiteral("org.bluez.MediaPlayer1"), QStringLiteral("Track"));
    if (!v.isValid()) return m_title;
    QVariantMap m = v.toMap();
    if (m.contains(QStringLiteral("Title"))) return m.value(QStringLiteral("Title")).toString();
    return m_title;
}

QString BluetoothController::trackArtist() const
{
    QVariant v = getProperty(m_playerPath, QStringLiteral("org.bluez.MediaPlayer1"), QStringLiteral("Track"));
    if (!v.isValid()) return m_artist;
    QVariantMap m = v.toMap();
    if (m.contains(QStringLiteral("Artist"))) return m.value(QStringLiteral("Artist")).toString();
    return m_artist;
}

void BluetoothController::refreshMetadata()
{
    if (m_playerPath.isEmpty()) return;
    QDBusInterface props(QStringLiteral("org.bluez"), m_playerPath,
                         QStringLiteral("org.freedesktop.DBus.Properties"),
                         QDBusConnection::systemBus());
    QDBusPendingCall p1 = props.asyncCall(QStringLiteral("Get"), QStringLiteral("org.bluez.MediaPlayer1"), QStringLiteral("Track"));
    QDBusPendingCall p2 = props.asyncCall(QStringLiteral("Get"), QStringLiteral("org.bluez.MediaPlayer1"), QStringLiteral("Position"));
    QDBusPendingCall p3 = props.asyncCall(QStringLiteral("Get"), QStringLiteral("org.bluez.MediaPlayer1"), QStringLiteral("Status"));
    auto *w1 = new QDBusPendingCallWatcher(p1, this);
    auto *w2 = new QDBusPendingCallWatcher(p2, this);
    auto *w3 = new QDBusPendingCallWatcher(p3, this);
    connect(w1, &QDBusPendingCallWatcher::finished, this, &BluetoothController::onGetPropTrackFinished);
    connect(w2, &QDBusPendingCallWatcher::finished, this, &BluetoothController::onGetPropPositionFinished);
    connect(w3, &QDBusPendingCallWatcher::finished, this, &BluetoothController::onGetPropStatusFinished);
}
