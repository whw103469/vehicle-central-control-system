#include "mainwindow.h"
#include "speechcontroller.h"
#include "musiccontroller.h"
#include "vehiclecontroller.h"
#include "bluetoothcontroller.h"
#include "bluetoothdialog.h"
#include "cloudiotclient.h"
#include "sentinelh264streamer.h"
#include "cloudossclient.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QPushButton>
#include <QSlider>
#include <QPainter>
#include <QFontMetrics>

#include "../Camera/videocapture.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_chatView(nullptr)
    , m_statusLabel(nullptr)
    , m_speechController(new SpeechController(this))
    , m_musicController(new MusicController(this))
    , m_vehicleController(new VehicleController(this))
    , m_cameraCapture(new VideoCapture(this))
    , m_btController(new BluetoothController(this))
    , m_cloudIoT(new CloudIoTClient(this))
    , m_manualRadarOn(false)
    , m_radarActive(false)
    , m_sentinelStreaming(false)
    , m_sentinelStreamer(new SentinelH264Streamer(this))
    , m_oss(new CloudOssClient(this))
{
    setupUi();

    m_speechController->setMusicController(m_musicController);
    m_speechController->setVehicleController(m_vehicleController);
    m_musicController->setBluetoothController(m_btController);
    m_cloudIoT->connectCloud();

    connect(m_speechController, &SpeechController::userUtteranceReady, this, &MainWindow::appendUserText);
    connect(m_speechController, &SpeechController::botReplyReady, this, &MainWindow::appendBotText);
    connect(m_speechController, &SpeechController::systemMessage, this, &MainWindow::appendSystemText);
    connect(m_speechController, &SpeechController::statusChanged, this, &MainWindow::updateStatusText);

    connect(m_vehicleController, &VehicleController::stateChanged, this, &MainWindow::handleVehicleStateChanged);
    connect(m_vehicleController, &VehicleController::connectionStatusChanged, this, &MainWindow::handleVehicleConnectionChanged);
    connect(m_cameraCapture, &VideoCapture::frameCaptured, this, &MainWindow::handleCameraFrame);
    connect(m_speechController, &SpeechController::requestManualRadar, this, &MainWindow::setManualRadar);
    connect(m_btController, &BluetoothController::connectionChanged, this, [this](bool ok){
        updateStatusText(ok ? QStringLiteral("蓝牙已连接") : QStringLiteral("蓝牙未连接"));
    });
    connect(m_btController, &BluetoothController::errorOccurred, this, [this](const QString &msg){
        appendSystemText(QStringLiteral("蓝牙错误: ") + msg);
    });
    connect(m_cloudIoT, &CloudIoTClient::requestSentinelMonitor, this, &MainWindow::handleSentinelRequest);
    connect(m_vehicleController, &VehicleController::stateChanged, this, [this](){
        m_cloudIoT->publishVehicleState(m_vehicleController->state());
    });
    connect(m_sentinelStreamer, &SentinelH264Streamer::segmentReady, this, [this](const QByteArray &bytes, qint64 ts, int seq){
        const QString key = QStringLiteral("sentinel/%1_%2.mp4").arg(ts).arg(seq);
        const QString url = m_oss->uploadBytes(key, bytes);
        if (!url.isEmpty()) {
            m_cloudIoT->publishSentinelUrl(url, ts, seq);
        }
    });
    connect(m_sentinelStreamer, &SentinelH264Streamer::errorOccurred, this, [this](const QString &msg){
        appendSystemText(QStringLiteral("哨兵编码错误: ") + msg);
    });
    connect(m_cloudIoT, &CloudIoTClient::remoteControlWindowFrontLeft, this, [this](bool open){
        const auto s = m_vehicleController->state();
        const bool curOpen = (s.windows & 0x01) != 0;
        if (curOpen != open) m_vehicleController->toggleWindowFrontLeft();
    });
    connect(m_cloudIoT, &CloudIoTClient::remoteControlWindowFrontRight, this, [this](bool open){
        const auto s = m_vehicleController->state();
        const bool curOpen = (s.windows & 0x02) != 0;
        if (curOpen != open) m_vehicleController->toggleWindowFrontRight();
    });
    connect(m_cloudIoT, &CloudIoTClient::remoteControlWindowRearLeft, this, [this](bool open){
        const auto s = m_vehicleController->state();
        const bool curOpen = (s.windows & 0x04) != 0;
        if (curOpen != open) m_vehicleController->toggleWindowRearLeft();
    });
    connect(m_cloudIoT, &CloudIoTClient::remoteControlWindowRearRight, this, [this](bool open){
        const auto s = m_vehicleController->state();
        const bool curOpen = (s.windows & 0x08) != 0;
        if (curOpen != open) m_vehicleController->toggleWindowRearRight();
    });
    connect(m_cloudIoT, &CloudIoTClient::remoteControlLowBeam, this, [this](bool on){
        const auto s = m_vehicleController->state();
        const bool curOn = (s.lights & 0x01) != 0;
        if (curOn != on) m_vehicleController->toggleLowBeam();
    });
    connect(m_cloudIoT, &CloudIoTClient::remoteControlHazard, this, [this](bool on){
        const auto s = m_vehicleController->state();
        const bool curOn = (s.lights & 0x10) && (s.lights & 0x20);
        if (curOn != on) m_vehicleController->toggleHazard();
    });
    connect(m_cloudIoT, &CloudIoTClient::remoteControlAcPower, this, [this](bool on){
        const auto s = m_vehicleController->state();
        const bool curOn = (s.acFlags & 0x01) != 0;
        if (curOn != on) m_vehicleController->toggleAcPower();
    });

    m_vehicleController->start(QStringLiteral("can1"));
}

MainWindow::~MainWindow()
{
}

void MainWindow::setupUi()
{
    QWidget *central = new QWidget(this);
    setCentralWidget(central);

    QVBoxLayout *mainLayout = new QVBoxLayout(central);

    QHBoxLayout *topLayout = new QHBoxLayout;
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->setSpacing(8);

    m_navWidget = new QWidget(this);
    QVBoxLayout *navLayout = new QVBoxLayout(m_navWidget);
    navLayout->setContentsMargins(8, 8, 8, 8);
    navLayout->setSpacing(8);

    m_speedLabel = new QLabel(m_navWidget);
    m_envInLabel = new QLabel(m_navWidget);
    m_envOutLabel = new QLabel(m_navWidget);
    m_nodeStatusLabel = new QLabel(m_navWidget);
    m_speedLabel->setText(QStringLiteral("车速 0.0 km/h"));
    m_envInLabel->setText(QStringLiteral("车内 0.0°C 0%RH"));
    m_envOutLabel->setText(QStringLiteral("车外 0.0°C 0%RH"));
    m_nodeStatusLabel->setText(QStringLiteral("车身节点 离线"));
    m_nodeStatusLabel->setStyleSheet(QStringLiteral("color: #F44336;"));
    // 计算能完整显示“车外 100.0°C 100%RH”的最小宽度，按钮比这再略宽一点
    {
        QFontMetrics fm(m_speedLabel->font());
        int base = fm.horizontalAdvance(QStringLiteral("车外 100.0°C 100%RH")) + 20; // padding
        int btnW = qBound(170, base + 6, 230); // 按钮宽度在 170~230 之间
        int labelW = qMax(base, 170);
        m_speedLabel->setFixedWidth(labelW);
        m_envInLabel->setFixedWidth(labelW);
        m_envOutLabel->setFixedWidth(labelW);
        // 左栏整体宽度 = 按钮宽度 + 左右内边距
        m_navWidget->setFixedWidth(btnW + 16);
        m_navWidget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        // 保存到属性，供下面按钮统一设置
        m_navWidget->setProperty("leftButtonWidth", btnW);
    }
    navLayout->addWidget(m_speedLabel);
    navLayout->addSpacing(12);
    navLayout->addWidget(m_envInLabel);
    navLayout->addWidget(m_envOutLabel);
    navLayout->addSpacing(12);
    navLayout->addWidget(m_nodeStatusLabel);

    QVBoxLayout *bodyLayout = new QVBoxLayout;

    auto styleButton = [this](QPushButton *b){
        b->setMinimumHeight(44);
        int btnW = m_navWidget->property("leftButtonWidth").toInt();
        if (btnW <= 0) btnW = 200;
        b->setFixedWidth(btnW);
        b->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    };

    m_btnAcPower = new QPushButton(QStringLiteral("空调 开"), m_navWidget);
    styleButton(m_btnAcPower);
    bodyLayout->addWidget(m_btnAcPower);

    m_btnRadar = new QPushButton(QStringLiteral("倒车雷达 开"), m_navWidget);
    styleButton(m_btnRadar);
    bodyLayout->addWidget(m_btnRadar);

    m_btnWindowFl = new QPushButton(QStringLiteral("前左窗 开"), m_navWidget);
    m_btnWindowFr = new QPushButton(QStringLiteral("前右窗 开"), m_navWidget);
    m_btnWindowRl = new QPushButton(QStringLiteral("后左窗 开"), m_navWidget);
    m_btnWindowRr = new QPushButton(QStringLiteral("后右窗 开"), m_navWidget);
    styleButton(m_btnWindowFl);
    styleButton(m_btnWindowFr);
    styleButton(m_btnWindowRl);
    styleButton(m_btnWindowRr);
    bodyLayout->addWidget(m_btnWindowFl);
    bodyLayout->addWidget(m_btnWindowFr);
    bodyLayout->addWidget(m_btnWindowRl);
    bodyLayout->addWidget(m_btnWindowRr);

    m_btnLowBeam = new QPushButton(QStringLiteral("近光灯 开"), m_navWidget);
    m_btnHazard = new QPushButton(QStringLiteral("双闪 开"), m_navWidget);
    styleButton(m_btnLowBeam);
    styleButton(m_btnHazard);
    bodyLayout->addWidget(m_btnLowBeam);
    bodyLayout->addWidget(m_btnHazard);

    m_btnSourceLocal = new QPushButton(QStringLiteral("本地音乐"), m_navWidget);
    m_btnSourceBluetooth = new QPushButton(QStringLiteral("蓝牙音乐"), m_navWidget);
    styleButton(m_btnSourceLocal);
    styleButton(m_btnSourceBluetooth);
    bodyLayout->addWidget(m_btnSourceLocal);
    bodyLayout->addWidget(m_btnSourceBluetooth);

    navLayout->addLayout(bodyLayout);

    m_statusLabel = new QLabel(m_navWidget);
    m_statusLabel->setText(QStringLiteral("状态: 语音助手后台运行中"));
    navLayout->addStretch(1);
    navLayout->addWidget(m_statusLabel);

    topLayout->addWidget(m_navWidget);

    QWidget *rightPane = new QWidget(central);
    QVBoxLayout *rightLayout = new QVBoxLayout(rightPane);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(8);

    m_navDisplayLabel = new QLabel(rightPane);
    m_navDisplayLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_navDisplayLabel->setAlignment(Qt::AlignCenter);
    m_navDisplayLabel->setStyleSheet(QStringLiteral("background-color: #222222; color: #DDDDDD;"));
    m_navDisplayLabel->setText(QStringLiteral("导航地图区域"));
    rightLayout->addWidget(m_navDisplayLabel, 1);

    topLayout->addWidget(rightPane, 1);
    topLayout->setStretch(0, 0);
    topLayout->setStretch(1, 1);
    mainLayout->addLayout(topLayout, 1);

    QWidget *musicPanel = new QWidget(rightPane);
    QVBoxLayout *musicLayout = new QVBoxLayout(musicPanel);

    QHBoxLayout *infoLayout = new QHBoxLayout;
    m_musicTitleLabel = new QLabel(musicPanel);
    m_musicArtistLabel = new QLabel(musicPanel);
    infoLayout->addWidget(m_musicTitleLabel);
    infoLayout->addWidget(m_musicArtistLabel);
    musicLayout->addLayout(infoLayout);

    QHBoxLayout *progressLayout = new QHBoxLayout;
    m_progressSlider = new QSlider(Qt::Horizontal, musicPanel);
    m_musicTimeLabel = new QLabel(musicPanel);
    progressLayout->addWidget(m_progressSlider);
    progressLayout->addWidget(m_musicTimeLabel);
    musicLayout->addLayout(progressLayout);

    QHBoxLayout *controlLayout = new QHBoxLayout;
    m_btnPrev = new QPushButton(QStringLiteral("上一首"), musicPanel);
    m_btnPlay = new QPushButton(QStringLiteral("播放"), musicPanel);
    m_btnStop = new QPushButton(QStringLiteral("停止"), musicPanel);
    m_btnNext = new QPushButton(QStringLiteral("下一首"), musicPanel);
    m_volumeSlider = new QSlider(Qt::Horizontal, musicPanel);
    m_volumeSlider->setRange(0, 100);
    controlLayout->addWidget(m_btnPrev);
    controlLayout->addWidget(m_btnPlay);
    controlLayout->addWidget(m_btnStop);
    controlLayout->addWidget(m_btnNext);
    controlLayout->addWidget(new QLabel(QStringLiteral("音量"), musicPanel));
    controlLayout->addWidget(m_volumeSlider);
    musicLayout->addLayout(controlLayout);

    rightLayout->addWidget(musicPanel, 0);

    m_chatView = new QTextEdit(this);
    m_chatView->setReadOnly(true);
    m_chatView->setVisible(false);
    // 聊天视图暂不加入布局，保持隐藏备用

    setWindowTitle(QStringLiteral("DR4-RK3568 语音助手"));
    // 全屏显示在 main.cpp 中处理

    connect(m_btnPlay, &QPushButton::clicked, this, &MainWindow::handlePlayClicked);
    connect(m_btnStop, &QPushButton::clicked, this, &MainWindow::handleStopClicked);
    connect(m_btnPrev, &QPushButton::clicked, this, &MainWindow::handlePrevClicked);
    connect(m_btnNext, &QPushButton::clicked, this, &MainWindow::handleNextClicked);
    connect(m_volumeSlider, &QSlider::valueChanged, this, &MainWindow::handleVolumeChanged);
    connect(m_progressSlider, &QSlider::sliderReleased, this, &MainWindow::handleProgressSliderReleased);

    connect(m_musicController, &MusicController::positionChanged, this, &MainWindow::handleMusicPositionChangedFromPlayer);
    connect(m_musicController, &MusicController::durationChanged, this, &MainWindow::handleMusicTrackChanged);
    connect(m_musicController, &MusicController::currentTrackInfoChanged, this, &MainWindow::handleMusicTrackChanged);
    connect(m_musicController, &MusicController::volumeChanged, this, &MainWindow::handleVolumeFromController);
    connect(m_musicController, &MusicController::playingChanged, this, &MainWindow::handlePlayingChanged);

    connect(m_btnAcPower, &QPushButton::clicked, this, &MainWindow::handleAcPowerClicked);
    connect(m_btnRadar, &QPushButton::clicked, this, &MainWindow::handleRadarClicked);
    connect(m_btnWindowFl, &QPushButton::clicked, this, &MainWindow::handleWindowFlClicked);
    connect(m_btnWindowFr, &QPushButton::clicked, this, &MainWindow::handleWindowFrClicked);
    connect(m_btnWindowRl, &QPushButton::clicked, this, &MainWindow::handleWindowRlClicked);
    connect(m_btnWindowRr, &QPushButton::clicked, this, &MainWindow::handleWindowRrClicked);
    connect(m_btnLowBeam, &QPushButton::clicked, this, &MainWindow::handleLowBeamClicked);
    connect(m_btnHazard, &QPushButton::clicked, this, &MainWindow::handleHazardClicked);
    connect(m_btnSourceLocal, &QPushButton::clicked, this, &MainWindow::handleSourceLocal);
    connect(m_btnSourceBluetooth, &QPushButton::clicked, this, &MainWindow::handleSourceBluetooth);

    handleMusicTrackChanged();
    handleMusicPositionChangedFromPlayer();
    m_volumeSlider->setValue(m_musicController->volume());
    handlePlayingChanged();
}

void MainWindow::appendUserText(const QString &text)
{
    m_chatView->append(QStringLiteral("用户: ") + text);
}

void MainWindow::appendBotText(const QString &text)
{
    m_chatView->append(QStringLiteral("AI: ") + text);
}

void MainWindow::appendSystemText(const QString &text)
{
    m_chatView->append(QStringLiteral("系统: ") + text);
}

void MainWindow::updateStatusText(const QString &text)
{
    m_statusLabel->setText(QStringLiteral("状态: ") + text);
}

void MainWindow::handlePlayClicked()
{
    m_musicController->play();
}

void MainWindow::handlePrevClicked()
{
    m_musicController->previous();
}

void MainWindow::handleNextClicked()
{
    m_musicController->next();
}

void MainWindow::handleStopClicked()
{
    m_musicController->pause();
}

void MainWindow::handleVolumeChanged(int value)
{
    if (m_volumeSlider->value() != value) {
        m_volumeSlider->setValue(value);
    }
    m_musicController->setVolume(value);
}

void MainWindow::handleMusicPositionChangedFromPlayer()
{
    qint64 pos = m_musicController->position();
    qint64 dur = m_musicController->duration();
    m_progressSlider->blockSignals(true);
    m_progressSlider->setRange(0, static_cast<int>(dur));
    m_progressSlider->setValue(static_cast<int>(pos));
    m_progressSlider->blockSignals(false);

    qint64 secPos = pos / 1000;
    qint64 secDur = dur / 1000;
    int mp = static_cast<int>(secPos / 60);
    int sp = static_cast<int>(secPos % 60);
    int md = static_cast<int>(secDur / 60);
    int sd = static_cast<int>(secDur % 60);
    QString text = QStringLiteral("%1:%2 / %3:%4")
        .arg(mp, 2, 10, QLatin1Char('0'))
        .arg(sp, 2, 10, QLatin1Char('0'))
        .arg(md, 2, 10, QLatin1Char('0'))
        .arg(sd, 2, 10, QLatin1Char('0'));
    m_musicTimeLabel->setText(text);
}

void MainWindow::handleMusicTrackChanged()
{
    m_musicTitleLabel->setText(QStringLiteral("音乐: ") + m_musicController->currentTitle());
    m_musicArtistLabel->setText(QStringLiteral("作者: ") + m_musicController->currentArtist());
}

void MainWindow::handleVolumeFromController()
{
    int v = m_musicController->volume();
    m_volumeSlider->blockSignals(true);
    m_volumeSlider->setValue(v);
    m_volumeSlider->blockSignals(false);
}

void MainWindow::handleProgressSliderReleased()
{
    int v = m_progressSlider->value();
    m_musicController->seek(v);
}

void MainWindow::handlePlayingChanged()
{
    const bool playing = m_musicController->playing();
    m_btnPlay->setEnabled(!playing);
    m_btnStop->setEnabled(playing);
}

void MainWindow::handleVehicleStateChanged()
{
    updateVehicleUi();
}

void MainWindow::handleVehicleConnectionChanged(bool online)
{
    Q_UNUSED(online);
    updateVehicleUi();
}

void MainWindow::handleAcPowerClicked()
{
    m_vehicleController->toggleAcPower();
}

void MainWindow::handleWindowFlClicked()
{
    m_vehicleController->toggleWindowFrontLeft();
}

void MainWindow::handleWindowFrClicked()
{
    m_vehicleController->toggleWindowFrontRight();
}

void MainWindow::handleWindowRlClicked()
{
    m_vehicleController->toggleWindowRearLeft();
}

void MainWindow::handleWindowRrClicked()
{
    m_vehicleController->toggleWindowRearRight();
}

void MainWindow::handleLowBeamClicked()
{
    m_vehicleController->toggleLowBeam();
}

void MainWindow::handleHazardClicked()
{
    m_vehicleController->toggleHazard();
}

void MainWindow::handleRadarClicked()
{
    m_manualRadarOn = !m_manualRadarOn;
    updateVehicleUi();
}

void MainWindow::setManualRadar(bool on)
{
    m_manualRadarOn = on;
    updateVehicleUi();
}

void MainWindow::handleCameraFrame(const QImage &image)
{
    if (!m_radarActive) {
        return;
    }
    if (image.isNull()) {
        return;
    }
    QImage frame = image;
    QPainter p(&frame);
    p.setRenderHint(QPainter::Antialiasing, true);
    const auto s = m_vehicleController->state();
    int bars = 4;
    int barW = 18;
    int barH = 80;
    int gap = 6;
    int pad = 10;
    int totalW = bars * barW + (bars - 1) * gap;
    QRect panel(frame.width() - pad - totalW - 10, pad, totalW + 10, barH + 40);
    p.fillRect(panel, QColor(0, 0, 0, 120));
    for (int i = 0; i < bars; ++i) {
        quint8 d = s.rearDistances[i];
        double meters = d * 0.1;
        double ratio = 0.0;
        if (d > 0) {
            double clipped = qMax(0.0, qMin(2.0, meters)); // 0~2m
            ratio = 1.0 - clipped / 2.0;
        }
        int h = static_cast<int>(ratio * barH);
        int x = panel.x() + 5 + i * (barW + gap);
        int y = panel.y() + 30 + (barH - h);
        p.fillRect(QRect(x, y, barW, h), ratio > 0.66 ? QColor(220, 50, 47) : (ratio > 0.33 ? QColor(255, 193, 7) : QColor(76, 175, 80)));
        p.setPen(QColor(220, 220, 220));
        p.drawRect(QRect(x, panel.y() + 30, barW, barH));
    }
    // 最近距离文本
    double nearest = 0.0;
    for (int i = 0; i < 4; ++i) {
        if (s.rearDistances[i] > 0) {
            double m = s.rearDistances[i] * 0.1;
            if (nearest == 0.0 || m < nearest) nearest = m;
        }
    }
    p.setPen(Qt::white);
    QFont f = p.font(); f.setPointSize(10); p.setFont(f);
    p.drawText(panel.adjusted(6, 6, -6, -6), Qt::AlignLeft | Qt::AlignTop,
               nearest > 0.0 ? QStringLiteral("最近: %1m").arg(nearest, 0, 'f', 1)
                             : QStringLiteral("雷达待命"));
    p.end();
    QPixmap pixmap = QPixmap::fromImage(frame).scaled(
                m_navDisplayLabel->size(),
                Qt::KeepAspectRatio,
                Qt::SmoothTransformation);
    m_navDisplayLabel->setPixmap(pixmap);
    if (m_sentinelStreaming) {
        m_sentinelStreamer->pushFrame(frame);
    }
}

void MainWindow::updateVehicleUi()
{
    if (!m_vehicleController) {
        return;
    }
    const VehicleController::VehicleState s = m_vehicleController->state();

    double speed = static_cast<double>(s.speedDeciKmh) / 10.0;
    m_speedLabel->setText(QStringLiteral("车速 %1 km/h").arg(speed, 0, 'f', 1));

    double cabinTemp = static_cast<double>(s.cabinTempHalf) / 2.0;
    double ambientTemp = static_cast<double>(s.ambientTempHalf) / 2.0;
    m_envInLabel->setText(QStringLiteral("车内 %1°C %2%RH")
                          .arg(cabinTemp, 0, 'f', 1)
                          .arg(s.cabinHumidity));
    m_envOutLabel->setText(QStringLiteral("车外 %1°C %2%RH")
                           .arg(ambientTemp, 0, 'f', 1)
                           .arg(s.ambientHumidity));

    if (s.nodeOnline) {
        m_nodeStatusLabel->setText(QStringLiteral("车身节点 在线"));
        m_nodeStatusLabel->setStyleSheet(QStringLiteral("color: #4CAF50;"));
    } else {
        m_nodeStatusLabel->setText(QStringLiteral("车身节点 离线"));
        m_nodeStatusLabel->setStyleSheet(QStringLiteral("color: #F44336;"));
    }

    bool acOn = (s.acFlags & 0x01) != 0;
    updateToggleButton(m_btnAcPower, acOn, QStringLiteral("空调 关"), QStringLiteral("空调 开"));

    bool flOpen = (s.windows & 0x01) != 0;
    bool frOpen = (s.windows & 0x02) != 0;
    bool rlOpen = (s.windows & 0x04) != 0;
    bool rrOpen = (s.windows & 0x08) != 0;
    updateToggleButton(m_btnWindowFl, flOpen, QStringLiteral("前左窗 关"), QStringLiteral("前左窗 开"));
    updateToggleButton(m_btnWindowFr, frOpen, QStringLiteral("前右窗 关"), QStringLiteral("前右窗 开"));
    updateToggleButton(m_btnWindowRl, rlOpen, QStringLiteral("后左窗 关"), QStringLiteral("后左窗 开"));
    updateToggleButton(m_btnWindowRr, rrOpen, QStringLiteral("后右窗 关"), QStringLiteral("后右窗 开"));

    bool lowBeamOn = (s.lights & 0x01) != 0;
    updateToggleButton(m_btnLowBeam, lowBeamOn, QStringLiteral("近光灯 关"), QStringLiteral("近光灯 开"));

    bool hazardOn = (s.lights & 0x10) != 0 && (s.lights & 0x20) != 0;
    updateToggleButton(m_btnHazard, hazardOn, QStringLiteral("双闪 关"), QStringLiteral("双闪 开"));

    bool reverseGear = (s.gearAndPark & 0x07) == 1;
    bool radarActive = m_manualRadarOn || reverseGear;
    if (radarActive != m_radarActive) {
        m_radarActive = radarActive;
        if (m_radarActive) {
            if (!m_cameraCapture->isRunning()) {
                if (m_cameraCapture->startCapture(QStringLiteral("/dev/video10"), 640, 480, 30)) {
                    m_cameraCapture->start();
                }
            }
            m_navDisplayLabel->setText(QString());
        } else {
            m_cameraCapture->stopCapture();
            m_cameraCapture->wait();
            m_navDisplayLabel->setPixmap(QPixmap());
            m_navDisplayLabel->setText(QStringLiteral("导航地图区域"));
        }
    }

    updateToggleButton(m_btnRadar, m_manualRadarOn, QStringLiteral("倒车雷达 关"), QStringLiteral("倒车雷达 开"));
}

void MainWindow::updateToggleButton(QPushButton *button, bool active, const QString &onText, const QString &offText)
{
    if (!button) {
        return;
    }
    if (active) {
        button->setText(onText);
        button->setStyleSheet(QStringLiteral("background-color: #4CAF50; color: white;"));
    } else {
        button->setText(offText);
        button->setStyleSheet(QString());
    }
}

void MainWindow::handleSourceLocal()
{
    m_musicController->setSource(0);
    handleMusicTrackChanged();
    handleMusicPositionChangedFromPlayer();
    handlePlayingChanged();
}

void MainWindow::handleSourceBluetooth()
{
    BluetoothDialog dlg(m_btController, this);
    if (dlg.exec() == QDialog::Accepted) {
        m_musicController->setSource(1);
        handleMusicTrackChanged();
        handleMusicPositionChangedFromPlayer();
        handlePlayingChanged();
    }
}

void MainWindow::handleSentinelRequest(bool start)
{
    m_sentinelStreaming = start;
    if (start) {
        m_sentinelStreamer->startStreaming(m_navDisplayLabel->width(), m_navDisplayLabel->height(), 15, 1);
    } else {
        m_sentinelStreamer->stopStreaming();
    }
}
