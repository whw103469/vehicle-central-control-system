#ifndef SENTINELH264STREAMER_H
#define SENTINELH264STREAMER_H

#include <QObject>
#include <QImage>
#include <QByteArray>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/opt.h>
}

class SentinelH264Streamer : public QObject
{
    Q_OBJECT
public:
    explicit SentinelH264Streamer(QObject *parent = nullptr);
    ~SentinelH264Streamer() override;

public slots:
    void startStreaming(int width, int height, int fps, int segmentSeconds);
    void stopStreaming();
    void pushFrame(const QImage &frame);

signals:
    void segmentReady(const QByteArray &bytes, qint64 timestampMs, int seq);
    void errorOccurred(const QString &message);

private:
    bool beginSegment();
    void endSegment();
    bool convertImageToFrame(const QImage &image, AVFrame *frame);
    bool encodeAndWriteFrame(AVFrame *frame);

    static int writeCallback(void *opaque, uint8_t *buf, int buf_size);
    static int64_t seekCallback(void *opaque, int64_t offset, int whence);

    AVFormatContext *m_fmt;
    AVCodecContext *m_codec;
    AVStream *m_stream;
    SwsContext *m_sws;
    AVFrame *m_frame;
    AVPacket *m_pkt;
    AVIOContext *m_io;
    QByteArray m_buffer;
    int m_width;
    int m_height;
    int m_fps;
    int m_segSeconds;
    int m_framesInSeg;
    int m_segSeq;
    bool m_running;
};

#endif
