#include "cloudossclient.h"

CloudOssClient::CloudOssClient(QObject *parent)
    : QObject(parent)
{
}

CloudOssClient::~CloudOssClient()
{
}

QString CloudOssClient::uploadBytes(const QString &objectKey, const QByteArray &bytes)
{
    Q_UNUSED(bytes);
    if (m_endpoint.isEmpty() || m_bucket.isEmpty()) {
        emit errorOccurred(QStringLiteral("OSS参数未配置"));
        return QString();
    }
    // 占位：返回一个拼接的URL；请替换为OSS SDK PutObject并生成签名URL
    return QStringLiteral("https://%1/%2/%3").arg(m_endpoint, m_bucket, objectKey);
}

