#ifndef VEHICLECONTROLLER_H
#define VEHICLECONTROLLER_H

#include <QObject>
#include <QByteArray>

class QCanBusDevice;

class VehicleController : public QObject
{
    Q_OBJECT

public:
    struct VehicleState {
        quint16 speedDeciKmh;
        qint8 cabinTempHalf;
        quint8 cabinHumidity;
        qint8 ambientTempHalf;
        quint8 ambientHumidity;
        quint8 doors;
        quint8 windows;
        quint8 lights;
        quint8 acFlags;
        quint8 acSetTempHalf;
        quint8 fanLevel;
        quint8 gearAndPark;
        quint8 rearDistances[4];
        bool nodeOnline;
    };

    explicit VehicleController(QObject *parent = nullptr);
    ~VehicleController() override;

    void start(const QString &interfaceName);
    void stop();

    const VehicleState &state() const;

public slots:
    void toggleAcPower();
    void toggleWindowFrontLeft();
    void toggleWindowFrontRight();
    void toggleWindowRearLeft();
    void toggleWindowRearRight();
    void toggleLowBeam();
    void toggleHazard();

signals:
    void stateChanged();
    void connectionStatusChanged(bool online);

private slots:
    void processReceivedFrames();
    void handleOnlineCheck();

private:
    void parseFrame(const QByteArray &payload, quint32 id);
    void updateNodeOnline(bool online);
    void sendControlFrame200(quint8 windowFlags, quint8 windowMask, quint8 lightFlags, quint8 lightMask, quint8 acFlags, quint8 acMask);

    QCanBusDevice *m_device;
    VehicleState m_state;
    quint8 m_cmdSeq;
    qint64 m_lastHeartbeatMs;
    bool m_online;
};

#endif
