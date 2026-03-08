#ifndef SPEECHCONTROLLER_H
#define SPEECHCONTROLLER_H

#include <QObject>      // Qt对象基类
#include <QByteArray>   // 二进制数据容器，用于保存PCM和WAV数据
#include <QString>      // 文本字符串类型
#include <functional>   // std::function，用于回调函数包装

class LocalProcessor;           // 本地规则处理前向声明
class CloudClient;              // 云端客户端前向声明
class PocketSphinxWakeDetector; // 统一音频处理线程前向声明
class MusicController;          // 音乐控制模块前向声明，用于播放/暂停等操作
class VehicleController;        // 车身控制模块

class SpeechController : public QObject
{
    Q_OBJECT

public:
    // 负责把“录音、云端、本地规则、TTS播放”串成一条完整对话链路
    explicit SpeechController(QObject *parent = nullptr);
    ~SpeechController() override;

    // 为语音控制器注入音乐控制模块指针，便于在对话期间自动暂停/恢复音乐以及处理音乐相关本地指令
    void setMusicController(MusicController *controller) { m_musicController = controller; }
    // 注入车身控制模块
    void setVehicleController(VehicleController *controller) { m_vehicleController = controller; }

public slots:
    void startRecordingFromMic();
    void stopRecordingFromMic();
    void processTextInput(const QString &text);
    void setCloudMode(bool enable);

signals:
    void userUtteranceReady(const QString &text);
    void botReplyReady(const QString &text);
    void systemMessage(const QString &text);
    void statusChanged(const QString &status);
    void requestManualRadar(bool on);

private slots:
    void handleInitialized();                                      // 唤醒线程初始化完成时的处理（播报“已启动”）
    void handleAudioCaptured(const QByteArray &pcmData); // 统一音频线程完成一轮指令录音时的回调
    void handleCloudAudioResult(const QString &userText, const QString &botReply);
    void handleCloudTextResult(const QString &userText, const QString &botReply);
    void handleCloudError(const QString &message);
    void handleAsrPartialText(const QString &text);
    void handleAsrFinalText(const QString &text);
    void handleTtsWavReady(const QByteArray &wavData);

private:
    enum class VoiceState {
        Idle,               // 空闲/待机，等待唤醒
        StandbyWaitWake,    // 预留状态：等待唤醒（流式ASR模式下使用）
        SpeakingWakeAck,    // 播报唤醒应答
        ListeningCommand,   // 正在听取指令
        ProcessingAudio,    // 指令录音完成，准备发送到云端识别
        ProcessingCloud,    // 云端ASR处理中
        SpeakingReply       // 正在播报回复
    };

    void setState(VoiceState newState, const QString &statusMsg = QString()); // 统一状态与状态栏文本更新
    QByteArray stereoToMono16(const QByteArray &stereoPcm) const;             // 将双声道PCM转换为单声道PCM
    void startStandbyAsr();                                                   // 进入待机状态（保留接口，当前主要由唤醒线程驱动）
    void stopAsr();                                                           // 停止ASR（保留接口）
    void speakText(const QString &text, VoiceState nextState);                // 调用云端TTS播放文本
    void playWavAsync(const QByteArray &wavData, std::function<void()> onDone); // 异步播放WAV
    bool parseWavPcm(const QByteArray &wavData, int &channels, int &sampleRate, int &bitsPerSample, QByteArray &pcmOut) const; // 解析WAV为PCM
    void playPcmBlocking(const QByteArray &pcm, int sampleRate, int channels); // 阻塞方式播放PCM

    LocalProcessor *m_localProcessor;       // 本地规则引擎，用于处理固定指令
    CloudClient *m_cloudClient;             // 云端客户端，负责调用百度ASR/TTS
    PocketSphinxWakeDetector *m_wakeDetector; // 统一音频处理线程：唤醒检测 + 指令录音 + VAD
    MusicController *m_musicController;     // 音乐控制模块，用于语音控制和对话期间自动暂停/恢复
    VehicleController *m_vehicleController; // 车身控制模块：状态查询与控制
    bool m_cloudMode;                       // 是否启用云端模式（目前总是优先本地指令）
    VoiceState m_state;                     // 当前语音状态机状态
    VoiceState m_stateAfterSpeak;           // 播报结束后要跳转到的状态
    QString m_wakeWord;                     // 唤醒词文本（例如“小贝”）
};

#endif
