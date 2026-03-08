#include "localprocessor.h"

#include <QStringList>

LocalProcessor::LocalProcessor(QObject *parent)
    : QObject(parent)
{
}

QString LocalProcessor::recognizeLocal(const QByteArray &pcmData) const
{
    // 当前示例不做本地ASR，仅演示基于“已识别文本”的本地规则，
    // 如果你以后接入离线ASR模型，可以在这里把PCM直接转成文本。
    Q_UNUSED(pcmData);
    return QString();
}

bool LocalProcessor::isLocalCommand(const QString &text) const
{
    // 简单的关键字匹配：只要识别结果中包含任何一个预置短语，就认为是“本地可处理指令”
    QString t = text.trimmed().toLower();
    if (t.isEmpty()) {
        return false;
    }

    QStringList list;
    list << QStringLiteral("你好")
         << QStringLiteral("早上好")
         << QStringLiteral("晚上好")
         << QStringLiteral("嗨")
         << QStringLiteral("hello")
         << QStringLiteral("小度小度")
         << QStringLiteral("你叫什么名字")
         << QStringLiteral("你是谁")
         << QStringLiteral("你会什么")
         << QStringLiteral("打开空调")
         << QStringLiteral("关闭空调")
         << QStringLiteral("打开车窗")
         << QStringLiteral("关闭车窗")
         << QStringLiteral("打开天窗")
         << QStringLiteral("关闭天窗")
         << QStringLiteral("打开车灯")
         << QStringLiteral("关闭车灯")
         << QStringLiteral("播放音乐")
         << QStringLiteral("停止音乐")
         << QStringLiteral("导航到")
         << QStringLiteral("打开设置")
         << QStringLiteral("清空聊天")
         << QStringLiteral("退出程序")
         << QStringLiteral("谢谢")
         << QStringLiteral("不客气")
         << QStringLiteral("再见");

    for (const QString &p : list) {
        if (t == p || text.contains(p, Qt::CaseInsensitive)) {
            return true;
        }
    }

    return false;
}

QString LocalProcessor::localReply(const QString &text) const
{
    // 根据不同的指令短语，返回预设的自然语言回复，这些都是“固定答案”，不是云端大模型生成的。
    QString t = text.trimmed().toLower();

    if (t.contains(QStringLiteral("你好")) || t.contains(QStringLiteral("嗨")) || t.contains(QStringLiteral("hello"))) {
        return QStringLiteral("你好，我是运行在DR4-RK3568开发板上的语音助手。");
    }

    if (t.contains(QStringLiteral("小度小度"))) {
        return QStringLiteral("我在。");
    }

    if (t.contains(QStringLiteral("早上好"))) {
        return QStringLiteral("早上好，祝你今天心情愉快。");
    }

    if (t.contains(QStringLiteral("晚上好"))) {
        return QStringLiteral("晚上好，注意休息。");
    }

    if (t.contains(QStringLiteral("你叫什么名字")) || t.contains(QStringLiteral("你是谁"))) {
        return QStringLiteral("我是一套基于混合架构的AI语音对话系统。");
    }

    if (t.contains(QStringLiteral("你会什么"))) {
        return QStringLiteral("我可以进行简单问候和基础对话，也可以把复杂问题交给云端AI处理。");
    }

    if (t.contains(QStringLiteral("打开空调"))) {
        return QStringLiteral("空调已打开。");
    }

    if (t.contains(QStringLiteral("关闭空调"))) {
        return QStringLiteral("空调已关闭。");
    }

    if (t.contains(QStringLiteral("打开车窗"))) {
        return QStringLiteral("车窗已打开。");
    }

    if (t.contains(QStringLiteral("关闭车窗"))) {
        return QStringLiteral("车窗已关闭。");
    }

    if (t.contains(QStringLiteral("打开天窗"))) {
        return QStringLiteral("天窗已打开。");
    }

    if (t.contains(QStringLiteral("关闭天窗"))) {
        return QStringLiteral("天窗已关闭。");
    }

    if (t.contains(QStringLiteral("打开车灯"))) {
        return QStringLiteral("车灯已打开。");
    }

    if (t.contains(QStringLiteral("关闭车灯"))) {
        return QStringLiteral("车灯已关闭。");
    }

    if (t.contains(QStringLiteral("播放音乐"))) {
        return QStringLiteral("正在为你播放音乐。");
    }

    if (t.contains(QStringLiteral("停止音乐"))) {
        return QStringLiteral("音乐已停止。");
    }

    if (t.contains(QStringLiteral("导航到"))) {
        const int idx = t.indexOf(QStringLiteral("导航到"));
        QString dest = t.mid(idx + 3).trimmed();
        if (dest.isEmpty()) {
            return QStringLiteral("请告诉我目的地。");
        }
        return QStringLiteral("开始导航到") + dest + QStringLiteral("。");
    }

    if (t.contains(QStringLiteral("打开设置"))) {
        return QStringLiteral("当前示例程序没有实现设置界面，可以在后续版本中扩展。");
    }

    if (t.contains(QStringLiteral("清空聊天"))) {
        return QStringLiteral("已收到清空聊天指令，请在界面点击“清空聊天”按钮。");
    }

    if (t.contains(QStringLiteral("退出程序"))) {
        return QStringLiteral("已收到退出程序指令，请在界面点击“退出程序”按钮。");
    }

    if (t.contains(QStringLiteral("谢谢"))) {
        return QStringLiteral("不客气，很高兴帮到你。");
    }

    if (t.contains(QStringLiteral("不客气"))) {
        return QStringLiteral("我们一起学习和探索。");
    }

    if (t.contains(QStringLiteral("再见"))) {
        return QStringLiteral("再见，下次再聊。");
    }

    return QStringLiteral("这是本地指令匹配到的简单回复。");
}
