#ifndef POCKETSPHINXWAKEDETECTOR_H
#define POCKETSPHINXWAKEDETECTOR_H

#include <QThread>
#include <QByteArray>
#include <QString>
#include <alsa/asoundlib.h>
#include <atomic>

// PocketSphinxWakeDetector使用单线程统一处理“唤醒词检测”和“指令录音+VAD”
// 线程内部一直保持打开同一个ALSA设备和PocketSphinx解码器
// 通过状态切换在“唤醒模式”和“指令模式”之间切换，实现零延迟模式切换
class PocketSphinxWakeDetector : public QThread
{
    Q_OBJECT

public:
    explicit PocketSphinxWakeDetector(QObject *parent = nullptr); // 构造函数
    ~PocketSphinxWakeDetector() override;                         // 析构函数

    // 启动统一音频处理线程，deviceName形如"plughw:CARD=Pro,DEV=0"或"hw:2,0"
    void startDetection(const QString &deviceName);               // 启动线程并进入唤醒模式

    // 停止统一音频处理线程
    void stopDetection();                                         // 请求线程退出

    void enableCommandCapture();                                  // 在应答播报结束后，由上层调用以允许采集指令

signals:
    // 初始化完成（PocketSphinx配置、搜索模式和音频设备就绪并开始utterance）时发出
    void initialized();                                         // 用于触发一次性启动播报
    // 当检测到唤醒词“小贝”时发出
    void wakeWordDetected();                                      // 唤醒词检测成功信号
    // 当完成一轮指令录音（经VAD自动结束）时发出，PCM为立体声16k S16_LE
    void commandAudioCaptured(const QByteArray &pcmData);         // 指令音频就绪信号
    // 底层音频或识别出错时发出
    void errorOccurred(const QString &message);                   // 错误信息信号
    // 检测状态变更（例如“待机监听中”、“录音中”）时发出（当前项目已不再使用，仅保留兼容）
    void statusChanged(const QString &status);                    // 状态文本信号

protected:
    void run() override;                                          // 线程入口函数

private:
    // 内部工作模式：唤醒模式只做PocketSphinx唤醒检测，指令模式只做VAD录音
    enum class Mode {
        WakeupMode,                                               // 唤醒模式：检测“小贝”
        CommandMode                                               // 指令模式：录音并通过VAD判断结束
    };

    QString m_deviceName;                                         // ALSA设备名称
    bool m_running;                                               // 线程运行标志
    snd_pcm_t *m_alsaHandle;                                      // ALSA采集句柄
    Mode m_mode;                                                  // 当前工作模式
    std::atomic<bool> m_commandCaptureEnabled;                    // 是否允许指令模式下采集和VAD
    std::atomic<bool> m_wakeWordEnabled;                          // 是否允许进行唤醒词检测

    // VAD相关状态
    bool m_hasSpeech;                                             // 是否已经检测到语音起点
    int m_silentFrames;                                           // 语音之后累计静音帧数
    int m_silenceThreshold;                                       // 静音阈值（平均振幅）
    int m_maxSilentFramesAfterSpeech;                             // 语音结束后允许的最大静音帧数
    int m_speechFrames;                                           // 当前指令中累计的语音帧数（用于过滤过短片段）
    QByteArray m_commandBuffer;                                   // 指令模式下累积的原始立体声PCM

    // 预录音缓冲：在唤醒模式下持续缓存最近一小段原始PCM，切换到指令模式时
    // 会将这段数据一并拼接到指令缓冲，避免“小贝打开空调”中“打开”被截断
    QByteArray m_preRollBuffer;                                   // 最近一小段原始立体声PCM
    int m_preRollMaxBytes;                                        // 预录音缓冲区最大容量（字节数）
};

#endif
