#include "musiccontroller.h"
#include "bluetoothcontroller.h"

#include <QDir>              // 目录遍历工具，用于扫描音乐文件
#include <QFileInfo>         // 文件信息类，用于解析文件名
#include <QMediaPlayer>      // 音频播放核心类
#include <QMediaPlaylist>    // 播放列表管理类
#include <QUrl>              // URL类，用于构造本地文件URL

MusicController::MusicController(QObject *parent)
    : QObject(parent)
    , m_player(new QMediaPlayer(this))
    , m_playlist(new QMediaPlaylist(this))
    , m_wasPlayingBeforeVoice(false)
    , m_bt(nullptr)
    , m_sourceMode(0)
    , m_btWasPlayingBeforeVoice(false)
{
    m_player->setPlaylist(m_playlist);

    scanMusicFolder();                        // 扫描本地音乐目录并填充播放列表

    m_playlist->setPlaybackMode(QMediaPlaylist::Sequential);

    connect(m_player, &QMediaPlayer::stateChanged, this, &MusicController::handleStateChanged);
    connect(m_player, &QMediaPlayer::positionChanged, this, &MusicController::handlePositionChanged);
    connect(m_player, &QMediaPlayer::durationChanged, this, &MusicController::handleDurationChanged);
    connect(m_playlist, &QMediaPlaylist::currentIndexChanged, this, &MusicController::handleCurrentIndexChanged);
}

QStringList MusicController::tracks() const
{
    return m_tracks; // 返回扫描到的所有曲目文件名列表
}

int MusicController::currentIndex() const
{
    return m_playlist->currentIndex(); // 从playlist获取当前索引
}

bool MusicController::playing() const
{
    if (m_sourceMode == 1 && m_bt) return m_bt->playing();
    return m_player->state() == QMediaPlayer::PlayingState;
}

qint64 MusicController::position() const
{
    if (m_sourceMode == 1 && m_bt) return m_bt->position();
    return m_player->position();
}

qint64 MusicController::duration() const
{
    if (m_sourceMode == 1 && m_bt) return m_bt->duration();
    return m_player->duration();
}

int MusicController::volume() const
{
    return m_player->volume();
}

int MusicController::playbackMode() const
{
    // 将QMediaPlaylist的枚举映射为整数给QML使用
    QMediaPlaylist::PlaybackMode mode = m_playlist->playbackMode();
    switch (mode) {
    case QMediaPlaylist::Random:
        return 1; // 随机播放
    case QMediaPlaylist::CurrentItemInLoop:
        return 2; // 单曲循环
    case QMediaPlaylist::Sequential:
    case QMediaPlaylist::Loop:
    default:
        return 0; // 顺序播放
    }
}

QString MusicController::currentTitle() const
{
    if (m_sourceMode == 1 && m_bt) return m_bt->trackTitle();
    int idx = m_playlist->currentIndex();
    if (idx >= 0 && idx < m_tracks.size()) return m_tracks.at(idx);
    return QString();
}

QString MusicController::currentArtist() const
{
    if (m_sourceMode == 1 && m_bt) return m_bt->trackArtist();
    int idx = m_playlist->currentIndex();
    if (idx >= 0 && idx < m_artists.size()) return m_artists.at(idx);
    return QString();
}

QString MusicController::currentDurationText() const
{
    qint64 ms = m_sourceMode == 1 && m_bt ? m_bt->duration() : m_player->duration();
    if (ms <= 0) {
        return QStringLiteral("00:00");
    }
    qint64 totalSeconds = ms / 1000;
    int minutes = static_cast<int>(totalSeconds / 60);
    int seconds = static_cast<int>(totalSeconds % 60);
    return QStringLiteral("%1:%2")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

int MusicController::source() const
{
    return m_sourceMode;
}

void MusicController::setSource(int s)
{
    if (m_sourceMode == s) return;
    m_sourceMode = s;
    emit sourceChanged();
    emit playingChanged();
    emit positionChanged();
    emit durationChanged();
    emit currentTrackInfoChanged();
}

void MusicController::setBluetoothController(BluetoothController *c)
{
    if (m_bt == c) return;
    if (m_bt) {
        disconnect(m_bt, nullptr, this, nullptr);
    }
    m_bt = c;
    if (m_bt) {
        connect(m_bt, &BluetoothController::playingChanged, this, &MusicController::handleBtPlayingChanged);
        connect(m_bt, &BluetoothController::positionChanged, this, &MusicController::handleBtPositionChanged);
        connect(m_bt, &BluetoothController::durationChanged, this, &MusicController::handleBtDurationChanged);
        connect(m_bt, &BluetoothController::trackChanged, this, &MusicController::handleBtTrackChanged);
    }
}

void MusicController::play()
{
    if (m_sourceMode == 1 && m_bt) {
        m_bt->play();
        emit playingChanged();
        return;
    }
    if (m_playlist->isEmpty()) return;
    if (m_playlist->currentIndex() < 0) {
        m_playlist->setCurrentIndex(0);
        emit currentIndexChanged();
        updateTrackInfo();
    }
    m_player->play();
    emit playingChanged();
}

void MusicController::pause()
{
    if (m_sourceMode == 1 && m_bt) {
        m_bt->pause();
        emit playingChanged();
        return;
    }
    if (m_player->state() == QMediaPlayer::PlayingState) {
        m_player->pause();
        emit playingChanged();
    }
}

void MusicController::togglePlay()
{
    if (m_player->state() == QMediaPlayer::PlayingState) {
        pause();    // 当前在播放则暂停
    } else {
        play();     // 当前未播放则启动播放
    }
}

void MusicController::next()
{
    if (m_sourceMode == 1 && m_bt) {
        m_bt->next();
        return;
    }
    if (!m_playlist->isEmpty()) {
        m_playlist->next();
        emit currentIndexChanged();
        updateTrackInfo();
        m_player->play();
    }
}

void MusicController::previous()
{
    if (m_sourceMode == 1 && m_bt) {
        m_bt->previous();
        return;
    }
    if (!m_playlist->isEmpty()) {
        m_playlist->previous();
        emit currentIndexChanged();
        updateTrackInfo();
        m_player->play();
    }
}

void MusicController::seek(qint64 ms)
{
    if (ms < 0) {
        ms = 0;
    }
    if (m_sourceMode == 1 && m_bt) {
        m_bt->seekMs(ms);
        return;
    }
    if (ms > m_player->duration()) {
        ms = m_player->duration();
    }
    m_player->setPosition(ms);
}

void MusicController::setVolume(int percent)
{
    if (percent < 0) {
        percent = 0;
    }
    if (percent > 100) {
        percent = 100;
    }
    if (m_sourceMode == 1 && m_bt) {
        emit volumeChanged();
        return;
    }
    if (m_player->volume() == percent) return;
    m_player->setVolume(percent);
    emit volumeChanged();
}

void MusicController::selectTrack(int index)
{
    if (index < 0 || index >= m_tracks.size()) {
        return;
    }
    m_playlist->setCurrentIndex(index);
    emit currentIndexChanged();
    updateTrackInfo();
    m_player->play();
    emit playingChanged();
}

void MusicController::setPlaybackMode(int mode)
{
    QMediaPlaylist::PlaybackMode m = QMediaPlaylist::Sequential;
    if (mode == 1) {
        m = QMediaPlaylist::Random;
    } else if (mode == 2) {
        m = QMediaPlaylist::CurrentItemInLoop;
    } else {
        m = QMediaPlaylist::Sequential;
    }
    if (m_playlist->playbackMode() == m) {
        return;
    }
    m_playlist->setPlaybackMode(m);
    emit playbackModeChanged();
}

void MusicController::pauseForVoice()
{
    if (m_sourceMode == 1 && m_bt) {
        m_btWasPlayingBeforeVoice = m_bt->playing();
        if (m_btWasPlayingBeforeVoice) {
            m_bt->pause();
            emit playingChanged();
        }
        return;
    }
    if (m_player->state() == QMediaPlayer::PlayingState) {
        m_wasPlayingBeforeVoice = true;
        m_player->pause();
        emit playingChanged();
    } else {
        m_wasPlayingBeforeVoice = false;
    }
}

void MusicController::resumeAfterVoice()
{
    if (m_sourceMode == 1 && m_bt) {
        if (m_btWasPlayingBeforeVoice) {
            m_bt->play();
            emit playingChanged();
            m_btWasPlayingBeforeVoice = false;
        }
        return;
    }
    if (m_wasPlayingBeforeVoice) {
        m_player->play();
        emit playingChanged();
        m_wasPlayingBeforeVoice = false;
    }
}

void MusicController::handleStateChanged()
{
    emit playingChanged();
}

void MusicController::handlePositionChanged(qint64)
{
    emit positionChanged();
}

void MusicController::handleDurationChanged(qint64)
{
    emit durationChanged();
    emit currentTrackInfoChanged();
}

void MusicController::handleCurrentIndexChanged(int)
{
    emit currentIndexChanged();
    updateTrackInfo();
}

void MusicController::scanMusicFolder()
{
    QDir dir(QStringLiteral("/mnt/sdcard/Music"));
    if (!dir.exists()) {
        return;
    }

    QStringList nameFilters;
    nameFilters << "*.mp3" << "*.ogg" << "*.flac" << "*.wav";

    QFileInfoList files = dir.entryInfoList(nameFilters, QDir::Files | QDir::Readable, QDir::Name);

    m_tracks.clear();
    m_artists.clear();
    m_playlist->clear();

    for (const QFileInfo &fi : files) {
        const QString path = fi.absoluteFilePath();
        const QString base = fi.completeBaseName();
        QString artist;
        QString title = base;
        int sep = base.indexOf('-');
        if (sep > 0) {
            artist = base.left(sep).trimmed();
            title = base.mid(sep + 1).trimmed();
        }
        m_tracks << title;
        m_artists << artist;
        m_playlist->addMedia(QUrl::fromLocalFile(path));
    }

    if (!m_tracks.isEmpty()) {
        m_playlist->setCurrentIndex(0);                                   // 默认选中第一首
        updateTrackInfo();                                                // 初始化当前曲目信息
    }

    emit tracksChanged();                                                 // 通知界面更新播放列表
}

void MusicController::updateTrackInfo()
{
    emit currentTrackInfoChanged();
}

void MusicController::handleBtPlayingChanged()
{
    if (m_sourceMode != 1) return;
    emit playingChanged();
}

void MusicController::handleBtPositionChanged()
{
    if (m_sourceMode != 1) return;
    emit positionChanged();
}

void MusicController::handleBtDurationChanged()
{
    if (m_sourceMode != 1) return;
    emit durationChanged();
}

void MusicController::handleBtTrackChanged()
{
    if (m_sourceMode != 1) return;
    emit currentTrackInfoChanged();
}
