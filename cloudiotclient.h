#ifndef CLOUDIOTCLIENT_H
#define CLOUDIOTCLIENT_H

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QJsonObject>
#include <QJsonDocument>
#include <QDateTime>
#include "vehiclecontroller.h"

class CloudIoTClient : public QObject
{
    Q_OBJECT
public:
    explicit CloudIoTClient(QObject *parent = nullptr);
    ~CloudIoTClient() override;

public slots:
    void connectCloud();
    void disconnectCloud();
    void publishVehicleState(const VehicleController::VehicleState &state);
    void publishMusicState(const QString &title, const QString &artist, qint64 durationMs, qint64 positionMs, bool playing);
    void publishRadarFrame(const QByteArray &jpegBytes, qint64 timestampMs);
    void publishControlEvent(const QString &name, const QString &value);
    void handleInboundMessage(const QString &topic, const QByteArray &payload);
    void publishSentinelSegment(const QByteArray &bytes, qint64 timestampMs, int seq);
    void publishSentinelUrl(const QString &url, qint64 timestampMs, int seq);

signals:
    void connectedChanged(bool connected);
    void errorOccurred(const QString &message);
    void outboundMessage(const QString &topic, const QByteArray &payload);
    void requestSentinelMonitor(bool start);
    void remoteControlWindowFrontLeft(bool open);
    void remoteControlWindowFrontRight(bool open);
    void remoteControlWindowRearLeft(bool open);
    void remoteControlWindowRearRight(bool open);
    void remoteControlLowBeam(bool on);
    void remoteControlHazard(bool on);
    void remoteControlAcPower(bool on);
};

#endif
