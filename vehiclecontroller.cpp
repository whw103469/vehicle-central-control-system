#include "vehiclecontroller.h"

#include <QCanBus>
#include <QCanBusDevice>
#include <QCanBusFrame>
#include <QDateTime>
#include <QTimer>

namespace {
static const quint32 FrameStateEnv = 0x100;
static const quint32 FrameStateBody = 0x101;
static const quint32 FrameStateRadar = 0x102;
static const quint32 FrameHeartbeat = 0x700;
static const quint32 FrameControlBody = 0x200;
}

VehicleController::VehicleController(QObject *parent)
    : QObject(parent)
    , m_device(nullptr)
    , m_cmdSeq(0)
    , m_lastHeartbeatMs(0)
    , m_online(false)
{
    m_state.speedDeciKmh = 0;
    m_state.cabinTempHalf = 0;
    m_state.cabinHumidity = 0;
    m_state.ambientTempHalf = 0;
    m_state.ambientHumidity = 0;
    m_state.doors = 0;
    m_state.windows = 0;
    m_state.lights = 0;
    m_state.acFlags = 0;
    m_state.acSetTempHalf = 0;
    m_state.fanLevel = 0;
    m_state.gearAndPark = 0;
    for (int i = 0; i < 4; ++i) {
        m_state.rearDistances[i] = 0;
    }
    m_state.nodeOnline = false;

    QTimer *timer = new QTimer(this);
    timer->setInterval(1000);
    connect(timer, &QTimer::timeout, this, &VehicleController::handleOnlineCheck);
    timer->start();
}

VehicleController::~VehicleController()
{
    stop();
}

void VehicleController::start(const QString &interfaceName)
{
    if (m_device) {
        return;
    }

    QString error;
    QCanBus *bus = QCanBus::instance();
    if (!bus) {
        return;
    }

    m_device = bus->createDevice(QStringLiteral("socketcan"), interfaceName, &error);
    if (!m_device) {
        return;
    }

    if (!m_device->connectDevice()) {
        delete m_device;
        m_device = nullptr;
        return;
    }

    connect(m_device, &QCanBusDevice::framesReceived, this, &VehicleController::processReceivedFrames);
}

void VehicleController::stop()
{
    if (m_device) {
        disconnect(m_device, nullptr, this, nullptr);
        m_device->disconnectDevice();
        delete m_device;
        m_device = nullptr;
    }
}

const VehicleController::VehicleState &VehicleController::state() const
{
    return m_state;
}

void VehicleController::toggleAcPower()
{
    bool currentOn = (m_state.acFlags & 0x01) != 0;
    bool targetOn = !currentOn;
    quint8 acFlags = 0;
    if (targetOn) {
        acFlags |= 0x01;
    }
    quint8 acMask = 0x01;
    if (targetOn) {
        m_state.acFlags |= 0x01;
    } else {
        m_state.acFlags &= static_cast<quint8>(~0x01);
    }
    emit stateChanged();
    sendControlFrame200(0, 0, 0, 0, acFlags, acMask);
}

void VehicleController::toggleWindowFrontLeft()
{
    bool currentOpen = (m_state.windows & 0x01) != 0;
    bool targetOpen = !currentOpen;
    quint8 flags = 0;
    if (targetOpen) {
        flags |= 0x01;
    }
    quint8 mask = 0x01;
    if (targetOpen) {
        m_state.windows |= 0x01;
    } else {
        m_state.windows &= static_cast<quint8>(~0x01);
    }
    emit stateChanged();
    sendControlFrame200(flags, mask, 0, 0, 0, 0);
}

void VehicleController::toggleWindowFrontRight()
{
    bool currentOpen = (m_state.windows & 0x02) != 0;
    bool targetOpen = !currentOpen;
    quint8 flags = 0;
    if (targetOpen) {
        flags |= 0x02;
    }
    quint8 mask = 0x02;
    if (targetOpen) {
        m_state.windows |= 0x02;
    } else {
        m_state.windows &= static_cast<quint8>(~0x02);
    }
    emit stateChanged();
    sendControlFrame200(flags, mask, 0, 0, 0, 0);
}

void VehicleController::toggleWindowRearLeft()
{
    bool currentOpen = (m_state.windows & 0x04) != 0;
    bool targetOpen = !currentOpen;
    quint8 flags = 0;
    if (targetOpen) {
        flags |= 0x04;
    }
    quint8 mask = 0x04;
    if (targetOpen) {
        m_state.windows |= 0x04;
    } else {
        m_state.windows &= static_cast<quint8>(~0x04);
    }
    emit stateChanged();
    sendControlFrame200(flags, mask, 0, 0, 0, 0);
}

void VehicleController::toggleWindowRearRight()
{
    bool currentOpen = (m_state.windows & 0x08) != 0;
    bool targetOpen = !currentOpen;
    quint8 flags = 0;
    if (targetOpen) {
        flags |= 0x08;
    }
    quint8 mask = 0x08;
    if (targetOpen) {
        m_state.windows |= 0x08;
    } else {
        m_state.windows &= static_cast<quint8>(~0x08);
    }
    emit stateChanged();
    sendControlFrame200(flags, mask, 0, 0, 0, 0);
}

void VehicleController::toggleLowBeam()
{
    bool currentOn = (m_state.lights & 0x01) != 0;
    bool targetOn = !currentOn;
    quint8 flags = 0;
    if (targetOn) {
        flags |= 0x01;
    }
    quint8 mask = 0x01;
    if (targetOn) {
        m_state.lights |= 0x01;
    } else {
        m_state.lights &= static_cast<quint8>(~0x01);
    }
    emit stateChanged();
    sendControlFrame200(0, 0, flags, mask, 0, 0);
}

void VehicleController::toggleHazard()
{
    bool currentLeft = (m_state.lights & 0x10) != 0;
    bool currentRight = (m_state.lights & 0x20) != 0;
    bool targetOn = !(currentLeft && currentRight);
    quint8 flags = 0;
    if (targetOn) {
        flags |= 0x10;
        flags |= 0x20;
    }
    quint8 mask = 0x10 | 0x20;
    if (targetOn) {
        m_state.lights |= static_cast<quint8>(0x10 | 0x20);
    } else {
        m_state.lights &= static_cast<quint8>(~(0x10 | 0x20));
    }
    emit stateChanged();
    sendControlFrame200(0, 0, flags, mask, 0, 0);
}

void VehicleController::processReceivedFrames()
{
    if (!m_device) {
        return;
    }
    while (m_device->framesAvailable() > 0) {
        const QCanBusFrame frame = m_device->readFrame();
        if (!frame.isValid()) {
            continue;
        }
        QByteArray payload = frame.payload();
        if (payload.size() < 8) {
            payload.resize(8);
        }
        parseFrame(payload, frame.frameId());
    }
}

void VehicleController::handleOnlineCheck()
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool online = m_lastHeartbeatMs > 0 && (now - m_lastHeartbeatMs <= 3000);
    updateNodeOnline(online);
}

void VehicleController::parseFrame(const QByteArray &payload, quint32 id)
{
    const unsigned char *d = reinterpret_cast<const unsigned char *>(payload.constData());

    if (id == FrameStateEnv) {
        quint16 speed = static_cast<quint16>(d[0]) | (static_cast<quint16>(d[1]) << 8);
        bool changed = false;
        if (m_state.speedDeciKmh != speed) {
            m_state.speedDeciKmh = speed;
            changed = true;
        }
        qint8 cabinTemp = static_cast<qint8>(d[2]);
        if (m_state.cabinTempHalf != cabinTemp) {
            m_state.cabinTempHalf = cabinTemp;
            changed = true;
        }
        quint8 cabinHum = d[3];
        if (m_state.cabinHumidity != cabinHum) {
            m_state.cabinHumidity = cabinHum;
            changed = true;
        }
        qint8 ambientTemp = static_cast<qint8>(d[4]);
        if (m_state.ambientTempHalf != ambientTemp) {
            m_state.ambientTempHalf = ambientTemp;
            changed = true;
        }
        quint8 ambientHum = d[5];
        if (ambientHum <= 100 && m_state.ambientHumidity != ambientHum) {
            m_state.ambientHumidity = ambientHum;
            changed = true;
        }
        if (changed) {
            emit stateChanged();
        }
        return;
    }

    if (id == FrameStateBody) {
        bool changed = false;
        quint8 doors = d[0];
        if (m_state.doors != doors) {
            m_state.doors = doors;
            changed = true;
        }
        quint8 windows = d[1];
        if (m_state.windows != windows) {
            m_state.windows = windows;
            changed = true;
        }
        quint8 lights = d[2];
        if (m_state.lights != lights) {
            m_state.lights = lights;
            changed = true;
        }
        quint8 acFlags = d[3];
        if (m_state.acFlags != acFlags) {
            m_state.acFlags = acFlags;
            changed = true;
        }
        quint8 acSetTemp = d[4];
        if (m_state.acSetTempHalf != acSetTemp) {
            m_state.acSetTempHalf = acSetTemp;
            changed = true;
        }
        quint8 fan = d[5];
        if (m_state.fanLevel != fan) {
            m_state.fanLevel = fan;
            changed = true;
        }
        quint8 gear = d[6];
        if (m_state.gearAndPark != gear) {
            m_state.gearAndPark = gear;
            changed = true;
        }
        if (changed) {
            emit stateChanged();
        }
        return;
    }

    if (id == FrameStateRadar) {
        bool changed = false;
        for (int i = 0; i < 4; ++i) {
            quint8 value = d[i];
            if (m_state.rearDistances[i] != value) {
                m_state.rearDistances[i] = value;
                changed = true;
            }
        }
        if (changed) {
            emit stateChanged();
        }
        return;
    }

    if (id == FrameHeartbeat) {
        m_lastHeartbeatMs = QDateTime::currentMSecsSinceEpoch();
        updateNodeOnline(true);
        return;
    }
}

void VehicleController::updateNodeOnline(bool online)
{
    if (m_state.nodeOnline == online) {
        return;
    }
    m_state.nodeOnline = online;
    if (m_online != online) {
        m_online = online;
        emit connectionStatusChanged(online);
    }
    emit stateChanged();
}

void VehicleController::sendControlFrame200(quint8 windowFlags, quint8 windowMask, quint8 lightFlags, quint8 lightMask, quint8 acFlags, quint8 acMask)
{
    if (!m_device) {
        return;
    }
    QByteArray payload(8, 0);
    unsigned char *d = reinterpret_cast<unsigned char *>(payload.data());
    d[0] = m_cmdSeq++;
    d[1] = windowFlags;
    d[2] = windowMask;
    d[3] = lightFlags;
    d[4] = lightMask;
    d[5] = acFlags;
    d[6] = acMask;

    QCanBusFrame frame(FrameControlBody, payload);
    m_device->writeFrame(frame);
}
