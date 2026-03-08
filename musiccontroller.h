#ifndef MUSICCONTROLLER_H
#define MUSICCONTROLLER_H

#include <QObject>
#include <QStringList>

class QMediaPlayer;
class QMediaPlaylist;
class BluetoothController;

// MusicController负责管理本地音乐播放：
// 1. 自动扫描 /mnt/sdcard/Music 目录下的音频文件
// 2. 提供播放/暂停、上一首/下一首、进度拖动、音量调节等基础控制接口
// 3. 暴露属性给QML界面使用（播放列表、当前曲目信息、进度、音量、播放模式）
// 4. 为语音助手提供 pauseForVoice()/resumeAfterVoice()，实现对话时自动暂停/恢复音乐
class MusicController : public QObject
{
    Q_OBJECT

    // 播放列表中文件名列表（仅用于界面展示）
    Q_PROPERTY(QStringList tracks READ tracks NOTIFY tracksChanged)
    // 当前播放曲目在tracks中的索引
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentIndexChanged)
    // 当前是否处于播放状态
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    // 当前播放进度（毫秒）
    Q_PROPERTY(qint64 position READ position NOTIFY positionChanged)
    // 当前歌曲总时长（毫秒）
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    // 音量百分比（0-100）
    Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY volumeChanged)
    // 播放模式：0=顺序播放，1=随机播放，2=单曲循环
    Q_PROPERTY(int playbackMode READ playbackMode WRITE setPlaybackMode NOTIFY playbackModeChanged)
    // 当前歌曲标题（从文件名简单提取）
    Q_PROPERTY(QString currentTitle READ currentTitle NOTIFY currentTrackInfoChanged)
    // 当前歌曲歌手（示例中从文件名简单提取或留空）
    Q_PROPERTY(QString currentArtist READ currentArtist NOTIFY currentTrackInfoChanged)
    // 当前歌曲时长文案（例如"03:25"）
    Q_PROPERTY(QString currentDurationText READ currentDurationText NOTIFY currentTrackInfoChanged)
    Q_PROPERTY(int source READ source WRITE setSource NOTIFY sourceChanged)

public:
    explicit MusicController(QObject *parent = nullptr);

    // 属性访问接口
    QStringList tracks() const;
    int currentIndex() const;
    bool playing() const;
    qint64 position() const;
    qint64 duration() const;
    int volume() const;
    int playbackMode() const;
    QString currentTitle() const;
    QString currentArtist() const;
    QString currentDurationText() const;
    int source() const;
    void setSource(int s);
    void setBluetoothController(BluetoothController *c);

    // QML与语音助手均可调用的控制接口
    Q_INVOKABLE void play();                 // 播放当前曲目
    Q_INVOKABLE void pause();                // 暂停播放
    Q_INVOKABLE void togglePlay();           // 播放/暂停切换
    Q_INVOKABLE void next();                 // 播放下一首
    Q_INVOKABLE void previous();             // 播放上一首
    Q_INVOKABLE void seek(qint64 ms);        // 跳转到指定毫秒位置
    Q_INVOKABLE void setVolume(int percent); // 设置音量（0-100）
    Q_INVOKABLE void selectTrack(int index); // 选择列表中的某一首并播放
    Q_INVOKABLE void setPlaybackMode(int mode); // 设置播放模式

    // 语音助手用接口：在对话期间暂时暂停音乐、结束后恢复之前的播放状态
    void pauseForVoice();
    void resumeAfterVoice();

signals:
    void tracksChanged();            // 播放列表发生变化
    void currentIndexChanged();      // 当前播放索引发生变化
    void playingChanged();           // 播放/暂停状态改变
    void positionChanged();          // 播放进度改变
    void durationChanged();          // 当前曲目总时长改变
    void volumeChanged();            // 音量改变
    void playbackModeChanged();      // 播放模式改变
    void currentTrackInfoChanged();  // 当前曲目信息改变
    void sourceChanged();

private slots:
    void handleStateChanged();             // 处理播放器状态变化
    void handlePositionChanged(qint64 pos);     // 处理进度变化
    void handleDurationChanged(qint64 dur);     // 处理总时长变化
    void handleCurrentIndexChanged(int index);  // 处理当前曲目索引变化
    void handleBtPlayingChanged();
    void handleBtPositionChanged();
    void handleBtDurationChanged();
    void handleBtTrackChanged();

private:
    void scanMusicFolder();          // 扫描/mnt/sdcard/Music目录并构造播放列表
    void updateTrackInfo();          // 更新当前曲目信息（标题、歌手、时长）

    QMediaPlayer   *m_player;        // 多媒体播放器
    QMediaPlaylist *m_playlist;      // 播放列表
    QStringList     m_tracks;        // 播放列表展示用的文件名集合
    QStringList     m_artists;       // 播放列表展示用的歌手名称集合
    bool            m_wasPlayingBeforeVoice; // 是否在语音对话开始前处于播放状态
    BluetoothController *m_bt;
    int             m_sourceMode;
    bool            m_btWasPlayingBeforeVoice;
};

#endif // MUSICCONTROLLER_H
