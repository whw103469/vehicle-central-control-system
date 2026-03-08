#ifndef CLOUDCLIENT_H
#define CLOUDCLIENT_H

#include <QObject>
#include <QByteArray>
#include <QString>
#include <functional>
#include <vosk_api.h>

class QNetworkAccessManager;
class QNetworkReply;
class QSslSocket;

// CloudClient负责和百度云交互：拿access_token、做语音识别ASR、做语音合成TTS
class CloudClient : public QObject
{
    Q_OBJECT

public:
    // 封装所有“云端”相关能力：语音识别ASR + 语音合成TTS
    explicit CloudClient(QObject *parent = nullptr);
    ~CloudClient() override;

public slots:
    void processAudio(const QByteArray &pcmData);
    // 仅保留语音相关能力：短语音HTTP识别 + 语音合成TTS
    void requestTtsWav(const QString &text, int sampleRate = 16000);

signals:
    void audioResultReady(const QString &userText, const QString &botReply);
    void ttsWavReady(const QByteArray &wavData);
    void errorOccurred(const QString &message);
    void statusChanged(const QString &status);

private:
    QString cuid() const;
    QNetworkAccessManager *m_network;
    QString m_accessToken;
    qint64 m_accessTokenExpireAtMs;
    VoskModel *m_voskModel;
    QString m_voskModelPath;
    QString m_piperBinary;
    QString m_piperModelPath;
};

#endif
