#include "sentinelh264streamer.h"
#include <QDateTime>

SentinelH264Streamer::SentinelH264Streamer(QObject *parent)
    : QObject(parent)
    , m_fmt(nullptr)
    , m_codec(nullptr)
    , m_stream(nullptr)
    , m_sws(nullptr)
    , m_frame(nullptr)
    , m_pkt(nullptr)
    , m_io(nullptr)
    , m_width(0)
    , m_height(0)
    , m_fps(0)
    , m_segSeconds(1)
    , m_framesInSeg(0)
    , m_segSeq(0)
    , m_running(false)
{
    av_log_set_level(AV_LOG_ERROR);
}

SentinelH264Streamer::~SentinelH264Streamer()
{
    stopStreaming();
}

void SentinelH264Streamer::startStreaming(int width, int height, int fps, int segmentSeconds)
{
    if (m_running) return;
    m_width = width;
    m_height = height;
    m_fps = fps;
    m_segSeconds = segmentSeconds > 0 ? segmentSeconds : 1;
    m_segSeq = 0;
    if (!beginSegment()) {
        emit errorOccurred(QStringLiteral("start failed"));
        stopStreaming();
        return;
    }
    m_running = true;
}

void SentinelH264Streamer::stopStreaming()
{
    if (!m_running) return;
    endSegment();
    m_running = false;
}

void SentinelH264Streamer::pushFrame(const QImage &frame)
{
    if (!m_running || frame.isNull() || !m_frame) return;
    QImage img = frame;
    if (img.width() != m_width || img.height() != m_height) {
        img = img.scaled(m_width, m_height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    if (!convertImageToFrame(img, m_frame)) {
        emit errorOccurred(QStringLiteral("convert failed"));
        return;
    }
    m_frame->pts = m_framesInSeg;
    if (!encodeAndWriteFrame(m_frame)) {
        emit errorOccurred(QStringLiteral("encode failed"));
        return;
    }
    m_framesInSeg++;
    if (m_framesInSeg >= m_fps * m_segSeconds) {
        endSegment();
        m_segSeq++;
        if (!beginSegment()) {
            emit errorOccurred(QStringLiteral("segment failed"));
            stopStreaming();
            return;
        }
    }
}

bool SentinelH264Streamer::beginSegment()
{
    m_buffer.clear();
    m_framesInSeg = 0;
    const AVCodec *codec = avcodec_find_encoder_by_name("h264_rkmpp");
    if (!codec) codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) return false;
    if (avformat_alloc_output_context2(&m_fmt, nullptr, "mp4", nullptr) < 0) return false;
    m_stream = avformat_new_stream(m_fmt, codec);
    if (!m_stream) return false;
    m_codec = avcodec_alloc_context3(codec);
    if (!m_codec) return false;
    m_codec->codec_id = codec->id;
    m_codec->bit_rate = 2000000;
    m_codec->width = m_width;
    m_codec->height = m_height;
    m_codec->time_base = AVRational{1, m_fps};
    m_codec->framerate = AVRational{m_fps, 1};
    m_codec->gop_size = m_fps;
    m_codec->max_b_frames = 0;
    m_codec->pix_fmt = AV_PIX_FMT_YUV420P;
    av_opt_set(m_codec->priv_data, "preset", "fast", 0);
    if (m_fmt->oformat->flags & AVFMT_GLOBALHEADER) {
        m_codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    if (avcodec_open2(m_codec, codec, nullptr) < 0) return false;
    if (avcodec_parameters_from_context(m_stream->codecpar, m_codec) < 0) return false;
    m_stream->time_base = m_codec->time_base;
    unsigned char *buf = static_cast<unsigned char*>(av_malloc(32768));
    m_io = avio_alloc_context(buf, 32768, 1, this, nullptr, &SentinelH264Streamer::writeCallback, &SentinelH264Streamer::seekCallback);
    if (!m_io) return false;
    m_fmt->pb = m_io;
    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "movflags", "faststart", 0);
    if (avformat_write_header(m_fmt, &opts) < 0) {
        av_dict_free(&opts);
        return false;
    }
    av_dict_free(&opts);
    m_sws = sws_getContext(m_width, m_height, AV_PIX_FMT_BGRA, m_width, m_height, m_codec->pix_fmt, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!m_sws) return false;
    m_frame = av_frame_alloc();
    if (!m_frame) return false;
    m_frame->format = m_codec->pix_fmt;
    m_frame->width = m_width;
    m_frame->height = m_height;
    if (av_frame_get_buffer(m_frame, 32) < 0) return false;
    m_pkt = av_packet_alloc();
    if (!m_pkt) return false;
    return true;
}

void SentinelH264Streamer::endSegment()
{
    if (m_codec) {
        avcodec_send_frame(m_codec, nullptr);
        while (avcodec_receive_packet(m_codec, m_pkt) == 0) {
            m_pkt->stream_index = m_stream->index;
            av_packet_rescale_ts(m_pkt, m_codec->time_base, m_stream->time_base);
            av_interleaved_write_frame(m_fmt, m_pkt);
            av_packet_unref(m_pkt);
        }
    }
    if (m_fmt) {
        av_write_trailer(m_fmt);
    }
    QByteArray bytes = m_buffer;
    qint64 ts = QDateTime::currentMSecsSinceEpoch();
    int seq = m_segSeq;
    if (!bytes.isEmpty()) emit segmentReady(bytes, ts, seq);
    if (m_pkt) {
        av_packet_free(&m_pkt);
        m_pkt = nullptr;
    }
    if (m_frame) {
        av_frame_free(&m_frame);
        m_frame = nullptr;
    }
    if (m_sws) {
        sws_freeContext(m_sws);
        m_sws = nullptr;
    }
    if (m_io) {
        av_free(m_io->buffer);
        avio_context_free(&m_io);
        m_io = nullptr;
    }
    if (m_codec) {
        avcodec_free_context(&m_codec);
        m_codec = nullptr;
    }
    if (m_fmt) {
        avformat_free_context(m_fmt);
        m_fmt = nullptr;
    }
    m_buffer.clear();
}

bool SentinelH264Streamer::convertImageToFrame(const QImage &image, AVFrame *frame)
{
    if (!m_sws || !frame) return false;
    QImage img = image.convertToFormat(QImage::Format_RGB32);
    const uint8_t *inData[1] = { img.bits() };
    int inLinesize[1] = { img.bytesPerLine() };
    if (sws_scale(m_sws, inData, inLinesize, 0, m_height, frame->data, frame->linesize) <= 0) return false;
    return true;
}

bool SentinelH264Streamer::encodeAndWriteFrame(AVFrame *frame)
{
    if (!m_codec || !m_fmt || !m_stream) return false;
    int ret = avcodec_send_frame(m_codec, frame);
    if (ret < 0) return false;
    while (ret >= 0) {
        ret = avcodec_receive_packet(m_codec, m_pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        else if (ret < 0) return false;
        m_pkt->stream_index = m_stream->index;
        av_packet_rescale_ts(m_pkt, m_codec->time_base, m_stream->time_base);
        if (av_interleaved_write_frame(m_fmt, m_pkt) < 0) {
            av_packet_unref(m_pkt);
            return false;
        }
        av_packet_unref(m_pkt);
    }
    return true;
}

int SentinelH264Streamer::writeCallback(void *opaque, uint8_t *buf, int buf_size)
{
    auto *self = static_cast<SentinelH264Streamer*>(opaque);
    self->m_buffer.append(reinterpret_cast<const char*>(buf), buf_size);
    return buf_size;
}

int64_t SentinelH264Streamer::seekCallback(void *opaque, int64_t offset, int whence)
{
    Q_UNUSED(opaque);
    Q_UNUSED(offset);
    Q_UNUSED(whence);
    return -1;
}
