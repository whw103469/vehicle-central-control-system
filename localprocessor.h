#ifndef LOCALPROCESSOR_H
#define LOCALPROCESSOR_H

#include <QObject>
#include <QByteArray>
#include <QString>

// LocalProcessor实现“本地规则引擎”：根据识别出来的文本判断是不是车载指令，并给出固定回复
class LocalProcessor : public QObject
{
    Q_OBJECT

public:
    explicit LocalProcessor(QObject *parent = nullptr);

    QString recognizeLocal(const QByteArray &pcmData) const;
    bool isLocalCommand(const QString &text) const;
    QString localReply(const QString &text) const;
};

#endif
