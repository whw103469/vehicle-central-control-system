#include "cloudclient.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QSysInfo>
#include <QUuid>
#include <QUrl>
#include <QUrlQuery>
#include <QProcess>
#include <QFile>
#include <QDir>
CloudClient::CloudClient(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
    , m_accessTokenExpireAtMs(0)
{
    , m_voskModel(nullptr)
    // m_network用于所有HTTP请求（获取token / 调用ASR / 调用TTS）
    m_voskModelPath = QStringLiteral("/opt/voice/models/vosk-model-cn-0.22");
    m_piperBinary = QStringLiteral("/opt/voice/tts/piper");
    m_piperModelPath = QStringLiteral("/opt/voice/tts/models/zh_CN-xiao_ya-medium.onnx");
    if (QFile::exists(m_voskModelPath)) {
        vosk_set_log_level(0);
        m_voskModel = vosk_model_new(m_voskModelPath.toUtf8().constData());
    }

CloudClient::~CloudClient()
{
}
    if (m_voskModel) {
        vosk_model_free(m_voskModel);
        m_voskModel = nullptr;
    }

void CloudClient::processAudio(const QByteArray &pcmData)
{
    // 语音识别入口：接收16k单声道PCM（来自麦克风），调用百度短语音HTTP识别接口
        return;
    }

    emit statusChanged(QStringLiteral("本地语音识别中"));
    std::thread([this, pcmData]() {
        if (!m_voskModel) {
            QMetaObject::invokeMethod(this, [this]() {
                emit errorOccurred(QStringLiteral("Vosk模型未加载"));
            }, Qt::QueuedConnection);
            return;
        }
        VoskRecognizer *rec = vosk_recognizer_new(m_voskModel, 16000.0f);
        vosk_recognizer_accept_waveform(rec, pcmData.constData(), pcmData.size());
        const char *json = vosk_recognizer_final_result(rec);
        QString userText;
        if (json) {
            QJsonParseError perr;
            QJsonDocument doc = QJsonDocument::fromJson(QByteArray(json), &perr);
            if (perr.error == QJsonParseError::NoError && doc.isObject()) {
                userText = doc.object().value(QStringLiteral("text")).toString();
            }
        }
        vosk_recognizer_free(rec);
        QMetaObject::invokeMethod(this, [this, userText]() {
            emit audioResultReady(userText, QString());
            emit statusChanged(QStringLiteral("本地语音识别完成"));
        }, Qt::QueuedConnection);
    }).detach();

void CloudClient::requestTtsWav(const QString &text, int sampleRate)
{
    // TTS入口：把要播报的中文文本交给百度TTS，拿回WAV二进制数据
    if (t.isEmpty()) {
        return;
    }

    Q_UNUSED(sampleRate);
    emit statusChanged(QStringLiteral("本地TTS生成中"));
    std::thread([this, t]() {
        if (!QFile::exists(m_piperBinary) || !QFile::exists(m_piperModelPath)) {
            QMetaObject::invokeMethod(this, [this]() {
                emit errorOccurred(QStringLiteral("Piper可执行或模型缺失"));
            }, Qt::QueuedConnection);
            return;
        }
        const QString outPath = QDir::temp().absoluteFilePath(QStringLiteral("piper_%1.wav").arg(QUuid::createUuid().toString(QUuid::Id128)));
        QProcess proc;
        QStringList args;
        args << QStringLiteral("--model") << m_piperModelPath
             << QStringLiteral("--output_file") << outPath;
        proc.start(m_piperBinary, args);
        if (!proc.waitForStarted(5000)) {
            QMetaObject::invokeMethod(this, [this]() {
                emit errorOccurred(QStringLiteral("Piper启动失败"));
            }, Qt::QueuedConnection);
            return;
        }
        proc.write(t.toUtf8());
        proc.write("\n");
        proc.closeWriteChannel();
        proc.waitForFinished(-1);
        QFile f(outPath);
        QByteArray bytes;
        if (f.open(QIODevice::ReadOnly)) {
            bytes = f.readAll();
            f.close();
        }
        QMetaObject::invokeMethod(this, [this, bytes]() {
            if (bytes.isEmpty()) {
                emit errorOccurred(QStringLiteral("Piper未生成音频"));
            } else {
                emit ttsWavReady(bytes);
                emit statusChanged(QStringLiteral("本地TTS完成"));
            }
        }, Qt::QueuedConnection);
    }).detach();

void CloudClient::ensureAccessToken(std::function<void(const QString &token)> onReady)
{
    const QByteArray id = QSysInfo::machineUniqueId();
    if (!id.isEmpty()) {
        return QString::fromLatin1(id.toHex());
    }
    }
    const QString host = QSysInfo::machineHostName().trimmed();
    if (!host.isEmpty()) {
        return host;
    }
    return QStringLiteral("qt-client");
}
