#include "pocketsphinxwakedetector.h"

extern "C" {
#include <pocketsphinx.h>         // PocketSphinx主头文件
#include <sphinxbase/err.h>       // PocketSphinx日志接口
}

#include <QVector>                // Qt容器，用于存放音频样本
#include <QFile>                  // 文件读写，用于生成关键词文件
#include <QTextStream>            // 文本流，用于写入UTF-8关键词
#include <QDebug>                 // 调试输出

PocketSphinxWakeDetector::PocketSphinxWakeDetector(QObject *parent)
    : QThread(parent)
    , m_running(false)
    , m_alsaHandle(nullptr)
    , m_mode(Mode::WakeupMode)
    , m_commandCaptureEnabled(false)
    , m_hasSpeech(false)
    , m_silentFrames(0)
    , m_silenceThreshold(300)
    , m_maxSilentFramesAfterSpeech(16000 * 8 / 10)
    , m_speechFrames(0)
    , m_preRollMaxBytes(16000 * 2 * 2 / 2)
{
}

PocketSphinxWakeDetector::~PocketSphinxWakeDetector()
{
    stopDetection();              // 请求线程结束
    wait();                       // 等待线程安全退出
    if (m_alsaHandle) {           // 如果ALSA设备还未关闭
        snd_pcm_close(m_alsaHandle); // 关闭ALSA设备
        m_alsaHandle = nullptr;   // 句柄置空
    }
}

void PocketSphinxWakeDetector::startDetection(const QString &deviceName)
{
    if (isRunning()) {            // 如果线程已经在运行，避免重复启动
        return;                   // 直接返回
    }
    m_deviceName = deviceName;    // 记录要使用的ALSA设备名称
    m_running = true;             // 将运行标志置为true
    start();                      // 启动线程，进入run()
}

void PocketSphinxWakeDetector::stopDetection()
{
    m_running = false;            // 将运行标志置为false，run循环会在下一次迭代退出
}

void PocketSphinxWakeDetector::enableCommandCapture()
{
    m_commandCaptureEnabled.store(true, std::memory_order_relaxed);
}

void PocketSphinxWakeDetector::run()
{
    const QString keywordsFile = QStringLiteral("/tmp/wakeup_keywords.txt"); // 关键词文件路径
    {
        QFile file(keywordsFile); // 打开文件对象
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) { // 以文本写入方式打开
            QTextStream out(&file); // 创建文本输出流
            out.setCodec("UTF-8");  // 设置编码为UTF-8
            out << QStringLiteral("小贝 /1e-30/\n"); // 写入唤醒词及阈值
            file.close();          // 关闭文件
            qDebug() << "创建关键词文件:" << keywordsFile; // 打印日志
        } else {
            emit errorOccurred(QStringLiteral("无法创建关键词文件")); // 发出错误信号
            m_running = false;    // 停止运行
            return;               // 直接退出线程
        }
    }

    QByteArray kwsPath = keywordsFile.toUtf8(); // 将关键词文件路径转为UTF-8字节数组

    cmd_ln_t *config = cmd_ln_init(nullptr, ps_args(), TRUE, // 初始化PocketSphinx配置
                                   "-hmm", "/usr/share/pocketsphinx/model/zh-cn/cmusphinx-zh-cn-5.2/zh_cn.cd_cont_5000/",
                                   "-dict", "/usr/share/pocketsphinx/model/zh-cn/cmusphinx-zh-cn-5.2/zh_cn.dic",
                                   "-kws", kwsPath.constData(),   // 指定关键词文件
                                   "-kws_threshold", "1e-20",     // 关键词阈值
                                   "-samprate", "16000",          // 采样率16k
                                   "-logfn", "/dev/null",         // 关闭PocketSphinx日志文件输出
                                   nullptr);
    if (!config) {                // 如果配置创建失败
        emit errorOccurred(QStringLiteral("PocketSphinx配置初始化失败")); // 发出错误信号
        m_running = false;        // 停止运行
        return;                   // 退出线程
    }

    ps_decoder_t *ps = ps_init(config); // 创建PocketSphinx解码器实例
    if (!ps) {                    // 如果解码器创建失败
        emit errorOccurred(QStringLiteral("PocketSphinx解码器创建失败")); // 发出错误信号
        cmd_ln_free_r(config);    // 释放配置对象
        m_running = false;        // 停止运行
        return;                   // 退出线程
    }
    qDebug() << "PocketSphinx 初始化成功"; // 打印初始化成功日志

    const char *searchName = "keywords"; // 搜索模式名称
    if (ps_set_kws(ps, searchName, kwsPath.constData()) < 0) { // 设置关键词搜索
        emit errorOccurred(QStringLiteral("PocketSphinx设置关键词失败")); // 失败则发出错误信号
        ps_free(ps);              // 释放解码器
        cmd_ln_free_r(config);    // 释放配置对象
        m_running = false;        // 停止运行
        return;                   // 退出线程
    }
    if (ps_set_search(ps, searchName) < 0) { // 设置当前搜索模式
        emit errorOccurred(QStringLiteral("PocketSphinx设置搜索模式失败")); // 失败则发出错误信号
        ps_free(ps);              // 释放解码器
        cmd_ln_free_r(config);    // 释放配置对象
        m_running = false;        // 停止运行
        return;                   // 退出线程
    }
    qDebug() << "设置搜索模式:" << searchName; // 打印搜索模式日志

    QByteArray devNameUtf8 = m_deviceName.toUtf8(); // 将设备名转为UTF-8
    const char *devName = devNameUtf8.isEmpty() ? "plughw:CARD=Pro,DEV=0" : devNameUtf8.constData(); // 若为空则使用默认设备plughw:CARD=Pro,DEV=0

    int err = snd_pcm_open(&m_alsaHandle, devName, SND_PCM_STREAM_CAPTURE, 0); // 打开ALSA采集设备
    if (err < 0) {                 // 如果打开失败
        emit errorOccurred(QStringLiteral("唤醒词设备打开失败: %1").arg(QString::fromLocal8Bit(snd_strerror(err)))); // 发出错误信息
        ps_free(ps);               // 释放解码器
        cmd_ln_free_r(config);     // 释放配置对象
        m_running = false;         // 停止运行
        return;                    // 退出线程
    }

    snd_pcm_hw_params_t *params = nullptr; // ALSA硬件参数结构指针
    snd_pcm_hw_params_alloca(&params);     // 在栈上分配参数结构
    err = snd_pcm_hw_params_any(m_alsaHandle, params); // 初始化参数为默认值
    if (err < 0) {                 // 如果初始化失败
        emit errorOccurred(QStringLiteral("唤醒词音频参数初始化失败: %1").arg(QString::fromLocal8Bit(snd_strerror(err)))); // 发出错误
        snd_pcm_close(m_alsaHandle); // 关闭ALSA设备
        m_alsaHandle = nullptr;    // 句柄置空
        ps_free(ps);               // 释放解码器
        cmd_ln_free_r(config);     // 释放配置对象
        m_running = false;         // 停止运行
        return;                    // 退出线程
    }

    err = snd_pcm_hw_params_set_access(m_alsaHandle, params, SND_PCM_ACCESS_RW_INTERLEAVED); // 设置交错访问模式
    if (err < 0) {                 // 如果设置失败
        emit errorOccurred(QStringLiteral("唤醒词访问模式设置失败: %1").arg(QString::fromLocal8Bit(snd_strerror(err)))); // 发出错误
        snd_pcm_close(m_alsaHandle); // 关闭ALSA设备
        m_alsaHandle = nullptr;    // 句柄置空
        ps_free(ps);               // 释放解码器
        cmd_ln_free_r(config);     // 释放配置对象
        m_running = false;         // 停止运行
        return;                    // 退出线程
    }

    err = snd_pcm_hw_params_set_format(m_alsaHandle, params, SND_PCM_FORMAT_S16_LE); // 设置采样格式为16bit小端
    if (err < 0) {                 // 如果设置失败
        emit errorOccurred(QStringLiteral("唤醒词采样格式设置失败: %1").arg(QString::fromLocal8Bit(snd_strerror(err)))); // 发出错误
        snd_pcm_close(m_alsaHandle); // 关闭ALSA设备
        m_alsaHandle = nullptr;    // 句柄置空
        ps_free(ps);               // 释放解码器
        cmd_ln_free_r(config);     // 释放配置对象
        m_running = false;         // 停止运行
        return;                    // 退出线程
    }

    unsigned int channels = 2;     // 期望使用双声道采集
    err = snd_pcm_hw_params_set_channels(m_alsaHandle, params, channels); // 设置通道数
    if (err < 0) {                 // 如果设置失败
        emit errorOccurred(QStringLiteral("唤醒词声道数设置失败: %1").arg(QString::fromLocal8Bit(snd_strerror(err)))); // 发出错误
        snd_pcm_close(m_alsaHandle); // 关闭ALSA设备
        m_alsaHandle = nullptr;    // 句柄置空
        ps_free(ps);               // 释放解码器
        cmd_ln_free_r(config);     // 释放配置对象
        m_running = false;         // 停止运行
        return;                    // 退出线程
    }

    unsigned int rate = 16000;     // 期望采样率16k
    err = snd_pcm_hw_params_set_rate_near(m_alsaHandle, params, &rate, nullptr); // 设置采样率（尽量接近16k）
    if (err < 0) {                 // 如果设置失败
        emit errorOccurred(QStringLiteral("唤醒词采样率设置失败: %1").arg(QString::fromLocal8Bit(snd_strerror(err)))); // 发出错误
        snd_pcm_close(m_alsaHandle); // 关闭ALSA设备
        m_alsaHandle = nullptr;    // 句柄置空
        ps_free(ps);               // 释放解码器
        cmd_ln_free_r(config);     // 释放配置对象
        m_running = false;         // 停止运行
        return;                    // 退出线程
    }

    err = snd_pcm_hw_params(m_alsaHandle, params); // 应用硬件参数到ALSA设备
    if (err < 0) {                 // 如果应用失败
        emit errorOccurred(QStringLiteral("唤醒词音频参数应用失败: %1").arg(QString::fromLocal8Bit(snd_strerror(err)))); // 发出错误
        snd_pcm_close(m_alsaHandle); // 关闭ALSA设备
        m_alsaHandle = nullptr;    // 句柄置空
        ps_free(ps);               // 释放解码器
        cmd_ln_free_r(config);     // 释放配置对象
        m_running = false;         // 停止运行
        return;                    // 退出线程
    }

    err = snd_pcm_prepare(m_alsaHandle); // 准备ALSA设备进入工作状态
    if (err < 0) {                 // 如果准备失败
        emit errorOccurred(QStringLiteral("唤醒词音频设备准备失败: %1").arg(QString::fromLocal8Bit(snd_strerror(err)))); // 发出错误
        snd_pcm_close(m_alsaHandle); // 关闭ALSA设备
        m_alsaHandle = nullptr;    // 句柄置空
        ps_free(ps);               // 释放解码器
        cmd_ln_free_r(config);     // 释放配置对象
        m_running = false;         // 停止运行
        return;                    // 退出线程
    }

    qDebug() << "音频设备打开成功:" << devName; // 打印设备打开成功日志

    const int framesPerChunk = 1024; // 每次从ALSA读取的帧数
    const int bytesPerSample = 2;    // 每个样本占用2字节（16bit）
    const int inChannels = 2;        // 输入为双声道
    QVector<int16_t> stereoBuf(framesPerChunk * inChannels); // 双声道采集缓冲区
    QVector<int16_t> monoBuf(framesPerChunk);                // 单声道缓冲区，用于唤醒检测和VAD

    ps_start_utt(ps);               // 开始一个新的utterance
    qDebug() << "开始 utterance";   // 打印日志

    m_mode = Mode::WakeupMode;      // 初始为唤醒模式
    m_hasSpeech = false;            // 尚未检测到语音
    m_silentFrames = 0;             // 静音帧数清零
    m_commandBuffer.clear();        // 清空指令缓冲
    m_speechFrames = 0;             // 语音帧计数清零

    emit initialized();             // 配置、搜索模式和音频设备就绪，通知上层可以播报“已启动”

    while (m_running) {             // 主循环：统一处理唤醒检测和指令录音
        snd_pcm_sframes_t frames = snd_pcm_readi(m_alsaHandle, stereoBuf.data(), framesPerChunk); // 从ALSA读取一块数据
        if (frames > 0) {           // 如果成功读取到数据
            int validFrames = static_cast<int>(frames); // 将返回的帧数转为int
            if (validFrames > framesPerChunk) { // 防御性检查，避免越界
                validFrames = framesPerChunk;   // 限制在缓冲区范围内
            }

            const int bytesThisChunk = validFrames * inChannels * bytesPerSample; // 当前块的字节数

            // 无论当前处于唤醒模式还是指令模式，都将原始立体声PCM追加到预录音缓冲区
            // 这样在检测到“小贝”后，可以把最近一小段历史音频一起拼接到指令缓冲，避免“打开”被截断
            m_preRollBuffer.append(reinterpret_cast<const char*>(stereoBuf.data()), bytesThisChunk); // 追加当前块到预录音缓冲
            if (m_preRollBuffer.size() > m_preRollMaxBytes) { // 如果缓冲超过最大容量
                m_preRollBuffer.remove(0, m_preRollBuffer.size() - m_preRollMaxBytes); // 丢弃最旧的数据，只保留最近一段
            }

            for (int i = 0; i < validFrames; ++i) { // 遍历每一帧
                monoBuf[i] = stereoBuf[i * inChannels]; // 从双声道中取左声道作为单声道样本
            }

            if (m_mode == Mode::WakeupMode) { // 唤醒模式：只做PocketSphinx唤醒检测
                ps_process_raw(ps, monoBuf.data(), static_cast<size_t>(validFrames), FALSE, FALSE); // 将单声道数据送入PocketSphinx

                const char *hyp = ps_get_hyp(ps, nullptr); // 获取当前识别文本
                if (hyp) {          // 如果存在识别结果
                    QString text = QString::fromUtf8(hyp); // 转为QString
                    if (text.contains(QStringLiteral("小贝"))) { // 如果包含唤醒词
                        qDebug() << "识别到唤醒词：小贝";        // 打印唤醒日志
                        emit wakeWordDetected();              // 发出唤醒信号
                        m_mode = Mode::CommandMode;           // 切换为指令模式
                        m_commandCaptureEnabled.store(false, std::memory_order_relaxed); // 暂不采集，等待“我在”播报结束
                        m_hasSpeech = false;                  // 重置VAD语音标志
                        m_silentFrames = 0;                   // 重置静音帧计数
                        m_commandBuffer = m_preRollBuffer;   // 将预录音缓冲复制到指令缓冲
                        m_speechFrames = 0;                  // 重置语音帧计数
                    }
                }
            } else {                // 指令模式：累积PCM并使用VAD检测结束
                if (!m_commandCaptureEnabled.load(std::memory_order_relaxed)) {
                    continue;       // 尚未允许采集指令（处于唤醒应答“我在”期间），直接忽略当前块
                }

                m_commandBuffer.append(reinterpret_cast<const char*>(stereoBuf.data()), bytesThisChunk); // 追加到指令缓冲

                long long sumAbs = 0; // 累计绝对值，用于计算平均振幅
                for (int i = 0; i < validFrames; ++i) { // 遍历当前块的单声道样本
                    sumAbs += std::abs(monoBuf[i]); // 累加样本绝对值
                }
                const int avgAmp = validFrames > 0 ? static_cast<int>(sumAbs / validFrames) : 0; // 计算平均振幅

                if (avgAmp > m_silenceThreshold) { // 如果当前块平均振幅高于静音阈值
                    m_hasSpeech = true;           // 标记已经检测到语音
                    m_silentFrames = 0;           // 静音帧计数清零
                    m_speechFrames += validFrames; // 累计语音帧数
                } else if (m_hasSpeech) {         // 如果之前已经检测到语音且当前块接近静音
                    m_silentFrames += validFrames; // 累加静音帧数
                    if (m_silentFrames >= m_maxSilentFramesAfterSpeech) { // 如果静音时间超过设定阈值
                        // 仅当语音帧数足够长时才认为是一条有效指令，防止用户尚未说话就发送到云端
                        const int minSpeechFrames = 16000 * 3 / 10; // 最小语音长度约0.3秒
                        if (!m_commandBuffer.isEmpty() && m_speechFrames >= minSpeechFrames) {
                            emit commandAudioCaptured(m_commandBuffer); // 发出指令音频就绪信号
                        }
                        m_mode = Mode::WakeupMode; // 切回唤醒模式
                        m_commandCaptureEnabled.store(false, std::memory_order_relaxed); // 指令结束后关闭采集
                        m_hasSpeech = false;       // 重置VAD状态
                        m_silentFrames = 0;        // 重置静音帧计数
                        m_commandBuffer.clear();   // 清空指令缓冲区
                        m_speechFrames = 0;        // 清空语音帧计数
                        ps_end_utt(ps);            // 结束当前utterance
                        ps_start_utt(ps);          // 重新开始新的utterance，继续唤醒检测
                    }
                }
            }
        } else if (frames == -EAGAIN) { // 如果暂时没有数据可读
            msleep(10);                // 休眠10ms后重试
            continue;                  // 继续下一次循环
        } else if (frames < 0) {       // 如果读取过程中发生错误
            int recoverErr = snd_pcm_recover(m_alsaHandle, static_cast<int>(frames), 1); // 尝试恢复ALSA设备
            if (recoverErr < 0) {      // 如果恢复失败
                emit errorOccurred(QStringLiteral("唤醒词录音错误: %1").arg(QString::fromLocal8Bit(snd_strerror(frames)))); // 发出错误
                break;                 // 退出循环
            }
            snd_pcm_prepare(m_alsaHandle); // 恢复成功后重新准备设备
            msleep(100);               // 稍作休眠后继续
        }
    }

    ps_end_utt(ps);              // 结束当前utterance
    if (m_alsaHandle) {          // 如果ALSA设备仍然打开
        snd_pcm_drain(m_alsaHandle); // 刷新缓冲区
        snd_pcm_close(m_alsaHandle); // 关闭ALSA设备
        m_alsaHandle = nullptr;  // 句柄置空
    }
    ps_free(ps);                 // 释放PocketSphinx解码器
    cmd_ln_free_r(config);       // 释放配置对象

    m_running = false;           // 将运行标志置为false
}
