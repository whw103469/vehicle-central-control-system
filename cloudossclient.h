#ifndef CLOUDOSSCLIENT_H
#define CLOUDOSSCLIENT_H

#include <QObject>
#include <QString>
#include <QByteArray>

class CloudOssClient : public QObject
{
    Q_OBJECT
public:
    explicit CloudOssClient(QObject *parent = nullptr);
    ~CloudOssClient() override;

    void setEndpoint(const QString &endpoint) { m_endpoint = endpoint; }
    void setBucket(const QString &bucket) { m_bucket = bucket; }
    void setAccessKeyId(const QString &ak) { m_ak = ak; }
    void setAccessKeySecret(const QString &sk) { m_sk = sk; }
    void setSecurityToken(const QString &token) { m_sts = token; }

    // 上传分段字节并返回可访问URL（占位实现：返回拼接URL，实际请用OSS SDK PutObject生成签名URL）
    QString uploadBytes(const QString &objectKey, const QByteArray &bytes);

signals:
    void errorOccurred(const QString &message);

private:
    QString m_endpoint;
    QString m_bucket;
    QString m_ak;
    QString m_sk;
    QString m_sts;
};

#endif
