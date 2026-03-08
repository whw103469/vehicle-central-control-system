#include "speechcontroller.h"       // 语音控制器头文件

#include "localprocessor.h"         // 本地规则处理模块
#include "cloudclient.h"            // 云端ASR/TTS模块
#include "pocketsphinxwakedetector.h" // 统一音频处理线程
#include "musiccontroller.h"        // 音乐控制模块
#include "vehiclecontroller.h"      // 车身控制模块

#include <QMetaObject>              // Qt元对象系统
#include <QtEndian>                 // 大小端转换工具
#include <QTimer>                   // 定时器，用于解耦底层回调与上层处理

#include <alsa/asoundlib.h>         // ALSA音频库

#include <cstring>                  // C字符串工具
#include <thread>                   // 标准线程库（保留用于音频播放线程）

SpeechController::SpeechController(QObject *parent)
    : QObject(parent)
    , m_localProcessor(new LocalProcessor(this))       // 创建本地规则处理模块
    , m_cloudClient(new CloudClient(this))             // 创建云端客户端模块
    , m_wakeDetector(new PocketSphinxWakeDetector(this)) // 创建统一音频处理线程
    , m_musicController(nullptr)                       // 音乐控制模块指针默认为空，稍后由外部注入
    , m_cloudMode(false)                               // 默认关闭云端优先模式
    , m_state(VoiceState::Idle)                        // 初始状态为空闲
    , m_stateAfterSpeak(VoiceState::Idle)              // 播报结束后默认回到空闲
    , m_wakeWord(QStringLiteral("小贝"))               // 设置唤醒词为“小贝”
{
    // 连接云端ASR结果回调：当百度短语音识别完成后，通过handleCloudAudioResult统一处理结果和状态机
    connect(m_cloudClient, &CloudClient::audioResultReady, this, &SpeechController::handleCloudAudioResult);
    // 连接云端TTS结果回调：当百度TTS生成WAV后，通过handleTtsWavReady播放并在结束时更新状态
    connect(m_cloudClient, &CloudClient::ttsWavReady, this, &SpeechController::handleTtsWavReady);
    // 连接云端错误回调：统一显示错误并回到待机状态
    connect(m_cloudClient, &CloudClient::errorOccurred, this, &SpeechController::handleCloudError);

    // 唤醒线程初始化完成：PocketSphinx与音频设备就绪后，播报一次“语音助手小贝已启动”
    connect(m_wakeDetector, &PocketSphinxWakeDetector::initialized,
            this, &SpeechController::handleInitialized);

    // 唤醒词检测：负责在单独线程中持续监听“小贝”，检测到后先播报“我在”，再进入听指令状态
    connect(m_wakeDetector, &PocketSphinxWakeDetector::wakeWordDetected, this, [this]() {
        emit systemMessage(QStringLiteral("检测到唤醒词：小贝"));              // 系统消息提示唤醒成功
        setState(VoiceState::SpeakingWakeAck, QStringLiteral("已唤醒"));     // 状态机切到“播放唤醒应答”
        // 播报“我在”，播报结束后进入ListeningCommand状态，用户再说具体指令
        speakText(QStringLiteral("我在"), VoiceState::ListeningCommand);
    });
    // 指令PCM就绪：用QTimer::singleShot把底层线程回调转到GUI线程异步处理，彻底解耦
    connect(m_wakeDetector, &PocketSphinxWakeDetector::commandAudioCaptured, this, [this](const QByteArray &pcm) {
        setState(VoiceState::ProcessingAudio, QStringLiteral("发送语音到云端识别")); // 状态机切到“准备发送到云端”
        QTimer::singleShot(0, this, [this, pcm]() {                               // 使用单次定时器，将处理逻辑排到事件队列中
            handleAudioCaptured(pcm);                                            // 在GUI线程中执行音频处理和云端调用
        });
    });
    // 唤醒线程错误：统一通过系统消息提示，不改变当前语音状态机
    connect(m_wakeDetector, &PocketSphinxWakeDetector::errorOccurred, this, [this](const QString &msg) {
        emit systemMessage(QStringLiteral("唤醒词错误: ") + msg);
    });

    // 系统启动时默认直接进入待机唤醒模式，使用USB麦克风plughw:CARD=Pro,DEV=0
    m_wakeDetector->startDetection(QStringLiteral("plughw:CARD=Pro,DEV=0")); // 启动统一音频处理线程
}

SpeechController::~SpeechController()
{
}

void SpeechController::startRecordingFromMic()
{
    // 当前项目采用自动唤醒+自动VAD录音，该函数保留给未来可能的手动控制入口
    setState(VoiceState::ListeningCommand, QStringLiteral("录音中（请说指令）")); // 手动触发开始听指令时更新状态
}

void SpeechController::stopRecordingFromMic()
{
    // 当前项目采用自动VAD结束录音，stopRecordingFromMic保留为空实现
    setState(VoiceState::Idle, QStringLiteral("录音结束，等待识别")); // 手动结束录音时回到空闲状态
}

void SpeechController::processTextInput(const QString &text)
{
    Q_UNUSED(text);
    // 当前项目仅支持语音交互，不再处理纯文本输入
}

void SpeechController::setCloudMode(bool enable)
{
    m_cloudMode = enable;
}

void SpeechController::handleAudioCaptured(const QByteArray &pcmData)
{
    // 统一音频线程在完成一轮指令录音后会一次性把整段PCM推到这里
    // 这里负责执行“从录音→发送到云端识别”的状态切换与数据预处理
    QByteArray mono = stereoToMono16(pcmData);                    // 将双声道PCM转换为单声道PCM（只取左声道）
    if (mono.isEmpty()) {                                         // 如果转换失败（数据长度不对等异常）
        emit systemMessage(QStringLiteral("录音数据格式不正确"));  // 提示录音数据异常
        setState(VoiceState::Idle, QStringLiteral("待机：唤醒词监听中")); // 回到待机状态，等待下一次唤醒
        return;
    }

    setState(VoiceState::ProcessingCloud, QStringLiteral("云端语音处理中")); // 状态切到“云端ASR处理中”
    m_cloudClient->processAudio(mono);                                      // 调用云端短语音识别接口
}

void SpeechController::handleCloudAudioResult(const QString &userText, const QString &botReply)
{
    // 这是短语音ASR的最终结果回调：userText来自百度ASR，botReply目前为空（预留给未来大模型）
    QString t = userText.trimmed();                           // 去掉识别文本前后空白

    if (!t.isEmpty()) {                                       // 如果识别出了用户说的话
        emit userUtteranceReady(t);                           // 通知UI显示用户语句
    }

    // 先处理音乐相关的固定指令（不依赖本地规则表），确保“下一首/上一首”等可以直接生效
    if (!t.isEmpty() && m_musicController) {
        QString reply;
        bool handledMusic = false;

        if (t.contains(QStringLiteral("播放音乐")) || t.contains(QStringLiteral("继续播放"))) {
            m_musicController->play();
            reply = QStringLiteral("好的，为你播放音乐");
            handledMusic = true;
        } else if (t.contains(QStringLiteral("停止音乐")) ||
                   t.contains(QStringLiteral("暂停音乐")) ||
                   t.contains(QStringLiteral("暂停播放"))) {
            m_musicController->pause();
            reply = QStringLiteral("好的，已停止音乐");
            handledMusic = true;
        } else if (t.contains(QStringLiteral("下一首"))) {
            m_musicController->next();
            reply = QStringLiteral("好的，下一首");
            handledMusic = true;
        } else if (t.contains(QStringLiteral("上一首"))) {
            m_musicController->previous();
            reply = QStringLiteral("好的，上一首");
            handledMusic = true;
        }

        if (handledMusic) {
            emit botReplyReady(reply);
            setState(VoiceState::SpeakingReply, QStringLiteral("本地音乐指令处理"));
            speakText(reply, VoiceState::Idle);
            return;
        }
    }

    // 车身状态查询与控制（优先处理）
    // 倒车雷达手动开关（不依赖车身控制器）
    if (!t.isEmpty()) {
        const bool mentionRadar = t.contains(QStringLiteral("倒车雷达")) || t.contains(QStringLiteral("后视雷达"));
        const bool wantOpen = t.contains(QStringLiteral("打开")) || t.contains(QStringLiteral("开启"));
        const bool wantClose = t.contains(QStringLiteral("关闭")) || t.contains(QStringLiteral("停用")) || t.contains(QStringLiteral("停止"));
        if (mentionRadar && (wantOpen || wantClose)) {
            emit requestManualRadar(wantOpen && !wantClose);
            QString reply = wantOpen ? QStringLiteral("已为你打开倒车雷达") : QStringLiteral("已为你关闭倒车雷达");
            emit botReplyReady(reply);
            setState(VoiceState::SpeakingReply, QStringLiteral("倒车雷达控制"));
            speakText(reply, VoiceState::Idle);
            return;
        }
    }

    if (!t.isEmpty() && m_vehicleController) {
        const auto s = m_vehicleController->state();
        auto say = [&](const QString &reply) {
            emit botReplyReady(reply);
            setState(VoiceState::SpeakingReply, QStringLiteral("车身语音处理"));
            speakText(reply, VoiceState::Idle);
        };

        // 查询类
        if (t.contains(QStringLiteral("车内温度")) || t.contains(QStringLiteral("舱内温度"))) {
            const double tp = s.cabinTempHalf / 2.0;
            const int rh = s.cabinHumidity;
            say(QStringLiteral("车内温度%1度，湿度%2百分比").arg(tp, 0, 'f', 1).arg(rh));
            return;
        }
        if (t.contains(QStringLiteral("车外温度")) || t.contains(QStringLiteral("环境温度"))) {
            const double tp = s.ambientTempHalf / 2.0;
            const int rh = s.ambientHumidity;
            say(QStringLiteral("车外温度%1度，湿度%2百分比").arg(tp, 0, 'f', 1).arg(rh));
            return;
        }
        if (t.contains(QStringLiteral("车速")) || t.contains(QStringLiteral("速度"))) {
            const double sp = s.speedDeciKmh / 10.0;
            say(QStringLiteral("当前车速%1公里每小时").arg(sp, 0, 'f', 1));
            return;
        }
        if (t.contains(QStringLiteral("车内湿度"))) {
            say(QStringLiteral("车内湿度%1百分比").arg(s.cabinHumidity));
            return;
        }
        if (t.contains(QStringLiteral("车外湿度"))) {
            say(QStringLiteral("车外湿度%1百分比").arg(s.ambientHumidity));
            return;
        }

        // 控制类（显式意图）
        auto containsAll = [&](std::initializer_list<QString> keys) {
            for (const auto &k : keys) if (!t.contains(k)) return false; return true;
        };
        // 前左窗
        if (containsAll({QStringLiteral("前左"), QStringLiteral("窗")}) &&
            (t.contains(QStringLiteral("打开")) || t.contains(QStringLiteral("关闭")))) {
            const bool wantOpen = t.contains(QStringLiteral("打开"));
            const bool curOpen = (s.windows & 0x01) != 0;
            if (wantOpen != curOpen) m_vehicleController->toggleWindowFrontLeft();
            say(wantOpen ? QStringLiteral("前左窗已打开") : QStringLiteral("前左窗已关闭"));
            return;
        }
        // 前右窗
        if (containsAll({QStringLiteral("前右"), QStringLiteral("窗")}) &&
            (t.contains(QStringLiteral("打开")) || t.contains(QStringLiteral("关闭")))) {
            const bool wantOpen = t.contains(QStringLiteral("打开"));
            const bool curOpen = (s.windows & 0x02) != 0;
            if (wantOpen != curOpen) m_vehicleController->toggleWindowFrontRight();
            say(wantOpen ? QStringLiteral("前右窗已打开") : QStringLiteral("前右窗已关闭"));
            return;
        }
        // 后左窗
        if (containsAll({QStringLiteral("后左"), QStringLiteral("窗")}) &&
            (t.contains(QStringLiteral("打开")) || t.contains(QStringLiteral("关闭")))) {
            const bool wantOpen = t.contains(QStringLiteral("打开"));
            const bool curOpen = (s.windows & 0x04) != 0;
            if (wantOpen != curOpen) m_vehicleController->toggleWindowRearLeft();
            say(wantOpen ? QStringLiteral("后左窗已打开") : QStringLiteral("后左窗已关闭"));
            return;
        }
        // 后右窗
        if (containsAll({QStringLiteral("后右"), QStringLiteral("窗")}) &&
            (t.contains(QStringLiteral("打开")) || t.contains(QStringLiteral("关闭")))) {
            const bool wantOpen = t.contains(QStringLiteral("打开"));
            const bool curOpen = (s.windows & 0x08) != 0;
            if (wantOpen != curOpen) m_vehicleController->toggleWindowRearRight();
            say(wantOpen ? QStringLiteral("后右窗已打开") : QStringLiteral("后右窗已关闭"));
            return;
        }
        // 空调
        if (t.contains(QStringLiteral("空调")) &&
            (t.contains(QStringLiteral("打开")) || t.contains(QStringLiteral("开启")) || t.contains(QStringLiteral("关闭")))) {
            const bool wantOn = t.contains(QStringLiteral("打开")) || t.contains(QStringLiteral("开启"));
            const bool curOn = (s.acFlags & 0x01) != 0;
            if (wantOn != curOn) m_vehicleController->toggleAcPower();
            say(wantOn ? QStringLiteral("空调已开启") : QStringLiteral("空调已关闭"));
            return;
        }
        // 近光灯
        if (t.contains(QStringLiteral("近光")) &&
            (t.contains(QStringLiteral("打开")) || t.contains(QStringLiteral("开启")) || t.contains(QStringLiteral("关闭")))) {
            const bool wantOn = t.contains(QStringLiteral("打开")) || t.contains(QStringLiteral("开启"));
            const bool curOn = (s.lights & 0x01) != 0;
            if (wantOn != curOn) m_vehicleController->toggleLowBeam();
            say(wantOn ? QStringLiteral("近光灯已打开") : QStringLiteral("近光灯已关闭"));
            return;
        }
        // 双闪
        if (t.contains(QStringLiteral("双闪")) &&
            (t.contains(QStringLiteral("打开")) || t.contains(QStringLiteral("开启")) || t.contains(QStringLiteral("关闭")))) {
            const bool wantOn = t.contains(QStringLiteral("打开")) || t.contains(QStringLiteral("开启"));
            const bool curOn = (s.lights & 0x10) && (s.lights & 0x20);
            if (wantOn != curOn) m_vehicleController->toggleHazard();
            say(wantOn ? QStringLiteral("双闪灯已打开") : QStringLiteral("双闪灯已关闭"));
            return;
        }
    }

    // 再尝试本地规则处理：例如“小贝打开空调”等固定指令优先走本地逻辑
    if (!t.isEmpty() && m_localProcessor->isLocalCommand(t)) {
        QString reply = m_localProcessor->localReply(t);      // 通过本地规则生成回复文本

        emit botReplyReady(reply);                            // 通知UI显示机器人回复
        setState(VoiceState::SpeakingReply, QStringLiteral("本地语音指令处理")); // 状态机切到“本地指令播报中”
        speakText(reply, VoiceState::Idle);                   // 播报本地回复，播报结束后回到Idle
        return;
    }

    // 当前未接入大模型：对于非本地指令，统一播报固定文案“云端语音处理完成”，然后回到待机
    QString reply = QStringLiteral("云端语音处理完成");       // 固定播报内容
    emit botReplyReady(reply);                                // 通知UI显示该回复文本
    setState(VoiceState::SpeakingReply, QStringLiteral("云端语音处理完成")); // 状态机切到“云端回复播报中”
    speakText(reply, VoiceState::Idle);                       // 播报完成后自动回Idle
}

void SpeechController::handleCloudTextResult(const QString &userText, const QString &botReply)
{
    // 当前项目未使用“云端文本→文本”模式，该回调仅保留以兼容接口，简单透传到UI
    if (!userText.isEmpty()) {                    // 如果有用户文本
        emit userUtteranceReady(userText);        // 通知UI显示
    }
    if (!botReply.isEmpty()) {                    // 如果有机器人回复
        emit botReplyReady(botReply);             // 通知UI显示
    }
    setState(VoiceState::Idle, QStringLiteral("待机：唤醒词监听中")); // 文本模式完成后回到待机
}

void SpeechController::handleCloudError(const QString &message)
{
    // 云端识别或TTS发生错误时，通过系统消息提示用户，并回到待机唤醒状态
    emit systemMessage(QStringLiteral("云端错误: ") + message);
    setState(VoiceState::Idle, QStringLiteral("云端错误")); // 状态栏显示错误信息，同时内部状态回Idle
}

void SpeechController::handleAsrPartialText(const QString &text)
{
    Q_UNUSED(text);
}

void SpeechController::handleAsrFinalText(const QString &text)
{
    const QString t = text.trimmed();
    if (t.isEmpty()) {
        return;
    }

    if (m_state == VoiceState::StandbyWaitWake) {
        if (t.contains(m_wakeWord)) {
            stopAsr();
            emit statusChanged(QStringLiteral("已唤醒"));
            m_state = VoiceState::SpeakingWakeAck;
            speakText(QStringLiteral("我在"), VoiceState::ListeningCommand);
        }
        return;
    }

    if (m_state == VoiceState::ListeningCommand) {
        stopAsr();
        emit userUtteranceReady(t);

        QString reply;
        if (!m_cloudMode && m_localProcessor->isLocalCommand(t)) {
            reply = m_localProcessor->localReply(t);
        } else if (m_localProcessor->isLocalCommand(t)) {
            reply = m_localProcessor->localReply(t);
        } else {
            reply = QStringLiteral("已收到：") + t;
        }

        emit botReplyReady(reply);
        emit statusChanged(QStringLiteral("播报中"));
        m_state = VoiceState::SpeakingReply;
        speakText(reply, VoiceState::StandbyWaitWake);
        return;
    }
}

void SpeechController::handleTtsWavReady(const QByteArray &wavData)
{
    // 当云端TTS生成的WAV数据就绪时，异步播放并在播放结束回调中更新状态机
    playWavAsync(wavData, [this]() {
        // 播报结束后根据预设的下一状态更新状态机与状态栏
        if (m_stateAfterSpeak == VoiceState::Idle) {
            setState(VoiceState::Idle, QStringLiteral("待机：唤醒词监听中")); // 正常对话结束：回到待机唤醒
            // 在一轮对话完全结束且回到Idle时，如果之前音乐在播放，则恢复播放
            if (m_musicController) {
                m_musicController->resumeAfterVoice();
            }
        } else {
            setState(m_stateAfterSpeak);
            if (m_stateAfterSpeak == VoiceState::ListeningCommand && m_wakeDetector) {
                m_wakeDetector->enableCommandCapture(); // 唤醒应答“我在”播报结束后，才真正开始采集指令
            }
        }
    });
}

void SpeechController::handleInitialized()
{
    // 唤醒链路完全就绪后，播报一次“语音助手小贝已启动”
    setState(VoiceState::SpeakingReply); // 简单将状态切到“播报中”
    speakText(QStringLiteral("语音助手小贝已启动"), VoiceState::Idle);
}

void SpeechController::setState(VoiceState newState, const QString &statusMsg)
{
    // setState是状态机的唯一入口：统一修改m_state，并在需要时更新状态栏文本
    m_state = newState;                        // 记录当前语音状态
    if (!statusMsg.isEmpty()) {               // 如果调用方提供了状态栏文本
        emit statusChanged(statusMsg);        // 统一从这里发送statusChanged信号
    }
}

QByteArray SpeechController::stereoToMono16(const QByteArray &stereoPcm) const
{
    // 当前板子上的USB麦克风是双声道，我们把左右声道中的左声道抽出来作为单声道输入给ASR
    if (stereoPcm.isEmpty()) {
        return QByteArray();
    }
    if ((stereoPcm.size() % 4) != 0) {
        return QByteArray();
    }

    const int frames = stereoPcm.size() / 4;
    QByteArray mono;
    mono.resize(frames * 2);

    const char *in = stereoPcm.constData();
    char *out = mono.data();

    for (int i = 0; i < frames; ++i) {
        out[0] = in[0];
        out[1] = in[1];
        in += 4;
        out += 2;
    }
    return mono;
}

void SpeechController::startStandbyAsr()
{
    m_state = VoiceState::StandbyWaitWake;
    emit statusChanged(QStringLiteral("待机：等待唤醒词"));
}

void SpeechController::stopAsr()
{
}

void SpeechController::speakText(const QString &text, VoiceState nextState)
{
    m_stateAfterSpeak = nextState;                // 记录播报结束后的目标状态

    // 在语音助手说话之前，如果音乐正在播放，则为语音对话暂时暂停音乐
    if (m_musicController) {
        m_musicController->pauseForVoice();
    }

    m_cloudClient->requestTtsWav(text, 16000);    // 请求云端TTS生成16k语音
}

void SpeechController::playWavAsync(const QByteArray &wavData, std::function<void()> onDone)
{
    std::thread([this, wavData, onDone]() {
        int channels = 0;
        int sampleRate = 0;
        int bitsPerSample = 0;
        QByteArray pcm;
        if (parseWavPcm(wavData, channels, sampleRate, bitsPerSample, pcm) && bitsPerSample == 16) {
            playPcmBlocking(pcm, sampleRate, channels);
        } else {
            QMetaObject::invokeMethod(this, [this]() {
                emit systemMessage(QStringLiteral("TTS音频解析失败"));
            }, Qt::QueuedConnection);
        }

        QMetaObject::invokeMethod(this, [onDone]() {
            if (onDone) {
                onDone();
            }
        }, Qt::QueuedConnection);
    }).detach();
}

bool SpeechController::parseWavPcm(const QByteArray &wavData, int &channels, int &sampleRate, int &bitsPerSample, QByteArray &pcmOut) const
{
    channels = 0;
    sampleRate = 0;
    bitsPerSample = 0;
    pcmOut.clear();

    if (wavData.size() < 12) {
        return false;
    }

    const uchar *p = reinterpret_cast<const uchar *>(wavData.constData());
    auto u32le = [](const uchar *x) -> quint32 {
        return qFromLittleEndian<quint32>(x);
    };
    auto u16le = [](const uchar *x) -> quint16 {
        return qFromLittleEndian<quint16>(x);
    };

    if (memcmp(p, "RIFF", 4) != 0 || memcmp(p + 8, "WAVE", 4) != 0) {
        return false;
    }

    bool fmtFound = false;
    bool dataFound = false;
    quint16 audioFormat = 0;

    int offset = 12;
    while (offset + 8 <= wavData.size()) {
        const uchar *chunk = p + offset;
        const quint32 chunkSize = u32le(chunk + 4);
        offset += 8;

        if (offset + static_cast<int>(chunkSize) > wavData.size()) {
            break;
        }

        if (memcmp(chunk, "fmt ", 4) == 0 && chunkSize >= 16) {
            const uchar *fmt = p + offset;
            audioFormat = u16le(fmt + 0);
            channels = static_cast<int>(u16le(fmt + 2));
            sampleRate = static_cast<int>(u32le(fmt + 4));
            bitsPerSample = static_cast<int>(u16le(fmt + 14));
            fmtFound = true;
        } else if (memcmp(chunk, "data", 4) == 0) {
            pcmOut = wavData.mid(offset, static_cast<int>(chunkSize));
            dataFound = true;
        }

        offset += static_cast<int>(chunkSize);
        if (chunkSize % 2 == 1) {
            offset += 1;
        }
    }

    if (!fmtFound || !dataFound) {
        return false;
    }
    if (audioFormat != 1) {
        return false;
    }
    if (channels <= 0 || sampleRate <= 0 || bitsPerSample <= 0) {
        return false;
    }
    return true;
}

void SpeechController::playPcmBlocking(const QByteArray &pcm, int sampleRate, int channels)
{
    // 为了兼容当前板子扬声器(hw:0,0 通常按2声道配置)，
    // 如果TTS返回的是单声道音频，这里主动做一次单声道→双声道扩展。
    QByteArray outPcm;
    int outChannels = channels;

    if (channels == 1) {
        const int bytesPerSample = 2;
        const int samples = pcm.size() / bytesPerSample;
        outPcm.resize(samples * bytesPerSample * 2);

        const char *in = pcm.constData();
        char *out = outPcm.data();
        for (int i = 0; i < samples; ++i) {
            // 复制同一个采样到左右声道
            out[0] = in[0];
            out[1] = in[1];
            out[2] = in[0];
            out[3] = in[1];
            in += 2;
            out += 4;
        }
        outChannels = 2;
    } else {
        outPcm = pcm;
        outChannels = channels;
    }

    snd_pcm_t *handle = nullptr;
    snd_pcm_hw_params_t *params = nullptr;

    int err = snd_pcm_open(&handle, "hw:0,0", SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        return;
    }

    if (snd_pcm_hw_params_malloc(&params) < 0 || !params) {
        snd_pcm_close(handle);
        return;
    }

    err = snd_pcm_hw_params_any(handle, params);
    if (err < 0) {
        snd_pcm_hw_params_free(params);
        snd_pcm_close(handle);
        return;
    }

    err = snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    if (err < 0) {
        snd_pcm_hw_params_free(params);
        snd_pcm_close(handle);
        return;
    }

    err = snd_pcm_hw_params_set_format(handle, params, SND_PCM_FORMAT_S16_LE);
    if (err < 0) {
        snd_pcm_hw_params_free(params);
        snd_pcm_close(handle);
        return;
    }

    err = snd_pcm_hw_params_set_channels(handle, params, static_cast<unsigned int>(outChannels));
    if (err < 0) {
        snd_pcm_hw_params_free(params);
        snd_pcm_close(handle);
        return;
    }

    unsigned int rate = static_cast<unsigned int>(sampleRate);
    err = snd_pcm_hw_params_set_rate_near(handle, params, &rate, nullptr);
    if (err < 0) {
        snd_pcm_hw_params_free(params);
        snd_pcm_close(handle);
        return;
    }

    err = snd_pcm_hw_params(handle, params);
    if (err < 0) {
        snd_pcm_hw_params_free(params);
        snd_pcm_close(handle);
        return;
    }

    snd_pcm_hw_params_free(params);
    snd_pcm_prepare(handle);

    const int bytesPerFrame = outChannels * 2;
    const char *data = outPcm.constData();
    int remaining = outPcm.size();

    while (remaining > 0) {
        const int frames = remaining / bytesPerFrame;
        if (frames <= 0) {
            break;
        }

        snd_pcm_sframes_t written = snd_pcm_writei(handle, data, static_cast<snd_pcm_uframes_t>(frames));
        if (written == -EPIPE) {
            snd_pcm_prepare(handle);
            continue;
        }
        if (written < 0) {
            break;
        }

        const int bytesWritten = static_cast<int>(written) * bytesPerFrame;
        data += bytesWritten;
        remaining -= bytesWritten;
    }

    snd_pcm_drain(handle);
    snd_pcm_close(handle);
}
