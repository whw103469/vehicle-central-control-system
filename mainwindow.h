#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTextEdit>
#include <QLabel>
class QPushButton;
class QSlider;
class QWidget;

class SpeechController;
class MusicController;
class VehicleController;
class VideoCapture;
class BluetoothController;
class BluetoothDialog;
class CloudIoTClient;
class SentinelH264Streamer;
class CloudOssClient;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void appendUserText(const QString &text);
    void appendBotText(const QString &text);
    void appendSystemText(const QString &text);
    void updateStatusText(const QString &text);
    void handlePlayClicked();
    void handleStopClicked();
    void handlePrevClicked();
    void handleNextClicked();
    void handleVolumeChanged(int value);
    void handleMusicPositionChangedFromPlayer();
    void handleMusicTrackChanged();
    void handleVolumeFromController();
    void handleProgressSliderReleased();
    void handlePlayingChanged();
    void handleVehicleStateChanged();
    void handleVehicleConnectionChanged(bool online);
    void handleAcPowerClicked();
    void handleWindowFlClicked();
    void handleWindowFrClicked();
    void handleWindowRlClicked();
    void handleWindowRrClicked();
    void handleLowBeamClicked();
    void handleHazardClicked();
    void handleRadarClicked();
    void handleCameraFrame(const QImage &image);
    void setManualRadar(bool on);
    void handleSourceLocal();
    void handleSourceBluetooth();
    void handleSentinelRequest(bool start);

private:
    void setupUi();
    void updateVehicleUi();
    void updateToggleButton(QPushButton *button, bool active, const QString &onText, const QString &offText);

    QTextEdit *m_chatView;
    QLabel *m_statusLabel;

    SpeechController *m_speechController;
    MusicController   *m_musicController;
    VehicleController *m_vehicleController;
    VideoCapture *m_cameraCapture;
    QWidget      *m_navWidget;
    QPushButton  *m_btnPrev;
    QPushButton  *m_btnPlay;
    QPushButton  *m_btnStop;
    QPushButton  *m_btnNext;
    QSlider      *m_volumeSlider;
    QSlider      *m_progressSlider;
    QLabel       *m_musicTitleLabel;
    QLabel       *m_musicArtistLabel;
    QLabel       *m_musicTimeLabel;
    QLabel       *m_navDisplayLabel;
    QLabel       *m_speedLabel;
    QLabel       *m_envInLabel;
    QLabel       *m_envOutLabel;
    QLabel       *m_nodeStatusLabel;
    QPushButton  *m_btnAcPower;
    QPushButton  *m_btnWindowFl;
    QPushButton  *m_btnWindowFr;
    QPushButton  *m_btnWindowRl;
    QPushButton  *m_btnWindowRr;
    QPushButton  *m_btnLowBeam;
    QPushButton  *m_btnHazard;
    QPushButton  *m_btnRadar;
    QPushButton  *m_btnSourceLocal;
    QPushButton  *m_btnSourceBluetooth;
    BluetoothController *m_btController;
    CloudIoTClient *m_cloudIoT;
    bool m_manualRadarOn;
    bool m_radarActive;
    bool m_sentinelStreaming;
    SentinelH264Streamer *m_sentinelStreamer;
    CloudOssClient *m_oss;
};

#endif
