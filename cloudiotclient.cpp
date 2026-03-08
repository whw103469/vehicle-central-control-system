#include "cloudiotclient.h"

CloudIoTClient::CloudIoTClient(QObject *parent)
    : QObject(parent)
{
}

CloudIoTClient::~CloudIoTClient()
{
}

void CloudIoTClient::connectCloud()
{
    emit connectedChanged(true);
}

void CloudIoTClient::disconnectCloud()
{
    emit connectedChanged(false);
}

void CloudIoTClient::publishVehicleState(const VehicleController::VehicleState &state)
{
    QJsonObject obj;
    obj["ts"] = QDateTime::currentMSecsSinceEpoch();
    obj["speed_kmh"] = static_cast<double>(state.speedDeciKmh) / 10.0;
    obj["cabin_temp_c"] = static_cast<double>(state.cabinTempHalf) / 2.0;
    obj["cabin_humidity"] = static_cast<int>(state.cabinHumidity);
    obj["ambient_temp_c"] = static_cast<double>(state.ambientTempHalf) / 2.0;
    obj["ambient_humidity"] = static_cast<int>(state.ambientHumidity);
    obj["windows_bits"] = static_cast<int>(state.windows);
    obj["lights_bits"] = static_cast<int>(state.lights);
    obj["ac_flags"] = static_cast<int>(state.acFlags);
    obj["ac_set_temp_half"] = static_cast<int>(state.acSetTempHalf);
    obj["fan_level"] = static_cast<int>(state.fanLevel);
    obj["gear"] = static_cast<int>(state.gearAndPark);
    QJsonObject radar;
    radar["d0"] = static_cast<int>(state.rearDistances[0]);
    radar["d1"] = static_cast<int>(state.rearDistances[1]);
    radar["d2"] = static_cast<int>(state.rearDistances[2]);
    radar["d3"] = static_cast<int>(state.rearDistances[3]);
    obj["rear_radar"] = radar;
    QJsonDocument doc(obj);
    emit outboundMessage(QStringLiteral("vehicle/state"), doc.toJson(QJsonDocument::Compact));
}

void CloudIoTClient::publishMusicState(const QString &title, const QString &artist, qint64 durationMs, qint64 positionMs, bool playing)
{
    Q_UNUSED(title);
    Q_UNUSED(artist);
    Q_UNUSED(durationMs);
    Q_UNUSED(positionMs);
    Q_UNUSED(playing);
}

void CloudIoTClient::publishRadarFrame(const QByteArray &jpegBytes, qint64 timestampMs)
{
    QJsonObject obj;
    obj["ts"] = timestampMs;
    obj["content_type"] = QStringLiteral("image/jpeg");
    obj["bytes_b64"] = QString::fromLatin1(jpegBytes.toBase64());
    QJsonDocument doc(obj);
    emit outboundMessage(QStringLiteral("vehicle/sentinel"), doc.toJson(QJsonDocument::Compact));
}

void CloudIoTClient::publishControlEvent(const QString &name, const QString &value)
{
    QJsonObject obj;
    obj["ts"] = QDateTime::currentMSecsSinceEpoch();
    obj["event"] = name;
    obj["value"] = value;
    QJsonDocument doc(obj);
    emit outboundMessage(QStringLiteral("vehicle/events"), doc.toJson(QJsonDocument::Compact));
}

void CloudIoTClient::handleInboundMessage(const QString &topic, const QByteArray &payload)
{
    QJsonParseError perr;
    QJsonDocument doc = QJsonDocument::fromJson(payload, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        emit errorOccurred(QStringLiteral("消息解析失败"));
        return;
    }
    QJsonObject obj = doc.object();
    if (topic == QStringLiteral("vehicle/commands")) {
        const QString type = obj.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("window")) {
            const QString which = obj.value(QStringLiteral("which")).toString();
            const bool open = obj.value(QStringLiteral("open")).toBool();
            if (which == QStringLiteral("fl")) emit remoteControlWindowFrontLeft(open);
            else if (which == QStringLiteral("fr")) emit remoteControlWindowFrontRight(open);
            else if (which == QStringLiteral("rl")) emit remoteControlWindowRearLeft(open);
            else if (which == QStringLiteral("rr")) emit remoteControlWindowRearRight(open);
        } else if (type == QStringLiteral("light")) {
            const QString which = obj.value(QStringLiteral("which")).toString();
            const bool on = obj.value(QStringLiteral("on")).toBool();
            if (which == QStringLiteral("low_beam")) emit remoteControlLowBeam(on);
            else if (which == QStringLiteral("hazard")) emit remoteControlHazard(on);
        } else if (type == QStringLiteral("ac")) {
            const bool on = obj.value(QStringLiteral("on")).toBool();
            emit remoteControlAcPower(on);
        } else if (type == QStringLiteral("sentinel")) {
            const bool start = obj.value(QStringLiteral("start")).toBool();
            emit requestSentinelMonitor(start);
        }
    }
}

void CloudIoTClient::publishSentinelSegment(const QByteArray &bytes, qint64 timestampMs, int seq)
{
    QJsonObject obj;
    obj["ts"] = timestampMs;
    obj["seq"] = seq;
    obj["content_type"] = QStringLiteral("video/mp4");
    obj["bytes_b64"] = QString::fromLatin1(bytes.toBase64());
    QJsonDocument doc(obj);
    emit outboundMessage(QStringLiteral("vehicle/sentinel_h264"), doc.toJson(QJsonDocument::Compact));
}

void CloudIoTClient::publishSentinelUrl(const QString &url, qint64 timestampMs, int seq)
{
    QJsonObject obj;
    obj["ts"] = timestampMs;
    obj["seq"] = seq;
    obj["url"] = url;
    obj["content_type"] = QStringLiteral("video/mp4");
    QJsonDocument doc(obj);
    emit outboundMessage(QStringLiteral("vehicle/sentinel_h264"), doc.toJson(QJsonDocument::Compact));
}
