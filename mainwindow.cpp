#include "mainwindow.h"
#include "systemmonitor.h"
#include "overlay.h"

#include <QCloseEvent>
#include <QMessageBox>
#include <QSettings>
#include <QDesktopWidget>
#include <QStyle>
#include <QApplication>
#include <QComboBox>
#include <QMap>
#include <QEvent>
#include <QSignalBlocker>
#include <QKeySequenceEdit>
#include <QDialog>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QAbstractButton>
#include <QDesktopServices>
#include <QUrl>
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QScrollArea>
#include <QClipboard>
#include <QFrame>
#include <QGridLayout>

#ifdef Q_OS_WIN32
#include <windows.h>
#endif

MainWindow::MainWindow(SystemMonitor *monitor, Overlay *overlay, QWidget *parent)
    : QMainWindow(parent)
    , m_monitor(monitor)
    , m_overlay(overlay)
    , m_trayIcon(nullptr)
    , m_presetCombo(nullptr)
    , m_presetLabel(nullptr)
    , m_restoreDefaultsButton(nullptr)
    , m_moveOverlayAction(nullptr)
    , m_trayShowHideAction(nullptr)
    , m_trayExitAction(nullptr)
    , m_hotkeyLabel(nullptr)
    , m_hotkeyButton(nullptr)
    , m_recordHotkeyLabel(nullptr)
    , m_recordButton(nullptr)
    , m_openCapturesButton(nullptr)
    , m_copyReportButton(nullptr)
    , m_alertsButton(nullptr)
    , m_gamesGroup(nullptr)
    , m_gameSearchEdit(nullptr)
    , m_gameList(nullptr)
    , m_refreshGamesButton(nullptr)
    , m_launchGameButton(nullptr)
    , m_gameLibraryStatus(nullptr)
    , m_configuringHotkey(false)
    , m_currentLang(1)
    , m_toggleHotkey(QKeySequence(Qt::ALT | Qt::Key_F12))
    , m_recordHotkey(QKeySequence(Qt::ALT | Qt::Key_F11))
    , m_recording(false)
    , m_recordingRequestPending(false)
    , m_gameScanner(nullptr)
    , m_loadingGameProfile(false)
    , m_activeProcessId(0)
{
    initUI();
    initTrayIcon();
    if (m_monitor) {
        connect(m_monitor, &SystemMonitor::foregroundProcessChanged, this, &MainWindow::onForegroundProcessChanged);
        connect(m_monitor, &SystemMonitor::recordingStateChanged, this, &MainWindow::onRecordingStateChanged);
        connect(m_monitor, &SystemMonitor::dataUpdated, this, &MainWindow::onMonitorUpdated);
    }
    if (m_overlay) {
        connect(m_overlay, &Overlay::requestOpenSettings, this, [this]() {
            showNormal();
            raise();
            activateWindow();
        });
        connect(m_overlay, &Overlay::requestPreset, this, [this](int preset) {
            const int index = m_presetCombo->findData(preset);
            if (index >= 0) m_presetCombo->setCurrentIndex(index);
        });
        connect(m_overlay, &Overlay::requestMoveMode, this, &MainWindow::onMoveOverlayToggled);
    }
    initHotkey();
    loadSettings();
    onRefreshGames();

    setWindowTitle(translate("Game Monitor Overlay"));
    const QRect available = QApplication::desktop()->availableGeometry(this);
    const double goldenRatio = 1.61803398875;
    const int maxWindowHeight = qMax(320, qMin(840, available.height() - 24));
    const int maxWindowWidth = qMax(320, available.width() - 24);
    const int windowWidth = qMin(maxWindowWidth, qRound(maxWindowHeight * goldenRatio));
    const int windowHeight = qMin(maxWindowHeight, qRound(windowWidth / goldenRatio));
    resize(windowWidth, windowHeight);
    setGeometry(
        QStyle::alignedRect(
            Qt::LeftToRight,
            Qt::AlignCenter,
            size(),
            available
        )
    );
}

MainWindow::~MainWindow()
{
    if (m_gameScanner && m_gameScanner->isRunning()) m_gameScanner->wait();
    saveSettings();
#ifdef Q_OS_WIN32
    UnregisterHotKey((HWND)winId(), 1);
    UnregisterHotKey((HWND)winId(), 2);
#endif
}

void MainWindow::initUI()
{
    m_centralWidget = new QWidget(this);
    m_mainLayout = new QVBoxLayout(m_centralWidget);
    m_mainLayout->setContentsMargins(15, 15, 15, 15);
    m_mainLayout->setSpacing(10);

    QLabel *titleLabel = new QLabel("Game Monitor Overlay");
    titleLabel->setObjectName("titleLabel");
    titleLabel->setAlignment(Qt::AlignCenter);
    m_mainLayout->addWidget(titleLabel);

    QHBoxLayout *contentLayout = new QHBoxLayout();
    contentLayout->setSpacing(14);
    QVBoxLayout *leftColumn = new QVBoxLayout();
    QVBoxLayout *rightColumn = new QVBoxLayout();
    leftColumn->setSpacing(10);
    rightColumn->setSpacing(10);
    contentLayout->addLayout(leftColumn, 3);
    contentLayout->addLayout(rightColumn, 2);
    m_mainLayout->addLayout(contentLayout, 1);

    m_displayGroup = new QGroupBox("Display Options");
    QVBoxLayout *displayLayout = new QVBoxLayout();

    QHBoxLayout *presetLayout = new QHBoxLayout();
    m_presetLabel = new QLabel("Layout:");
    m_presetCombo = new QComboBox();
    m_presetCombo->addItem("Gaming HUD", 0);
    m_presetCombo->addItem("System information", 1);
    m_presetCombo->addItem("Custom", 2);
    m_restoreDefaultsButton = new QPushButton("Recommended");
    presetLayout->addWidget(m_presetLabel);
    presetLayout->addWidget(m_presetCombo, 1);
    presetLayout->addWidget(m_restoreDefaultsButton);
    displayLayout->addLayout(presetLayout);

    m_checkCpu = new QCheckBox("CPU Usage");
    m_checkGpu = new QCheckBox("GPU Usage");
    m_checkMemory = new QCheckBox("Memory Usage");
    m_checkVideoMemory = new QCheckBox("Video Memory");
    m_checkCpuTemp = new QCheckBox("CPU Temperature");
    m_checkGpuTemp = new QCheckBox("GPU Temperature");
    m_checkPing = new QCheckBox("Network Latency");
    m_checkNetworkInfo = new QCheckBox("Network Information");
    m_checkDeviceInfo = new QCheckBox("Device Information");
    m_checkStorageInfo = new QCheckBox("Storage Information");
    m_checkNetworkTraffic = new QCheckBox("Network Traffic + Trend");
    m_checkFrameMetrics = new QCheckBox("FPS / Frame Time / 1% Low");
    m_checkMousePassThrough = new QCheckBox("Move overlay (temporarily disables click-through)");

    m_checkCpu->setChecked(true);
    m_checkGpu->setChecked(true);
    m_checkMemory->setChecked(true);
    m_checkVideoMemory->setChecked(false);
    m_checkCpuTemp->setChecked(true);
    m_checkGpuTemp->setChecked(true);
    m_checkPing->setChecked(false);
    m_checkNetworkInfo->setChecked(false);
    m_checkDeviceInfo->setChecked(false);
    m_checkStorageInfo->setChecked(false);
    m_checkNetworkTraffic->setChecked(true);
    m_checkFrameMetrics->setChecked(true);
    m_checkMousePassThrough->setChecked(false);

    QGroupBox *cpuGroup = new QGroupBox("CPU and memory");
    cpuGroup->setObjectName("customCpuGroup");
    QVBoxLayout *cpuLayout = new QVBoxLayout(cpuGroup);
    cpuLayout->addWidget(m_checkCpu);
    cpuLayout->addWidget(m_checkCpuTemp);
    cpuLayout->addWidget(m_checkMemory);
    QGroupBox *gpuGroup = new QGroupBox("Graphics");
    gpuGroup->setObjectName("customGpuGroup");
    QVBoxLayout *gpuLayout = new QVBoxLayout(gpuGroup);
    gpuLayout->addWidget(m_checkGpu);
    gpuLayout->addWidget(m_checkVideoMemory);
    gpuLayout->addWidget(m_checkGpuTemp);
    gpuLayout->addWidget(m_checkFrameMetrics);
    QGroupBox *networkGroup = new QGroupBox("Network");
    networkGroup->setObjectName("customNetworkGroup");
    QVBoxLayout *networkLayout = new QVBoxLayout(networkGroup);
    networkLayout->addWidget(m_checkPing);
    networkLayout->addWidget(m_checkNetworkInfo);
    networkLayout->addWidget(m_checkNetworkTraffic);
    QGroupBox *storageGroup = new QGroupBox("Memory and storage details");
    storageGroup->setObjectName("customStorageGroup");
    QVBoxLayout *storageLayout = new QVBoxLayout(storageGroup);
    storageLayout->addWidget(m_checkDeviceInfo);
    storageLayout->addWidget(m_checkStorageInfo);
    QGridLayout *metricGroups = new QGridLayout();
    metricGroups->setHorizontalSpacing(10);
    metricGroups->setVerticalSpacing(8);
    metricGroups->addWidget(cpuGroup, 0, 0);
    metricGroups->addWidget(gpuGroup, 0, 1);
    metricGroups->addWidget(networkGroup, 1, 0);
    metricGroups->addWidget(storageGroup, 1, 1);
    metricGroups->setColumnStretch(0, 1);
    metricGroups->setColumnStretch(1, 1);
    displayLayout->addLayout(metricGroups);
    displayLayout->addWidget(m_checkMousePassThrough);
    m_displayGroup->setLayout(displayLayout);
    leftColumn->addWidget(m_displayGroup);

    m_gamesGroup = new QGroupBox("Game library (Steam / Epic)");
    QVBoxLayout *gamesLayout = new QVBoxLayout(m_gamesGroup);
    m_gameSearchEdit = new QLineEdit(m_gamesGroup);
    m_gameSearchEdit->setPlaceholderText("Search games...");
    m_gameList = new QListWidget(m_gamesGroup);
    m_gameList->setMinimumHeight(140);
    m_gameList->setMaximumHeight(360);
    m_gameList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_gameLibraryStatus = new QLabel("Scanning installed games...", m_gamesGroup);
    m_gameLibraryStatus->setObjectName("gameLibraryStatus");
    QHBoxLayout *gameButtons = new QHBoxLayout();
    m_refreshGamesButton = new QPushButton("Refresh library", m_gamesGroup);
    m_launchGameButton = new QPushButton("Launch selected game", m_gamesGroup);
    m_launchGameButton->setObjectName("primaryButton");
    m_launchGameButton->setEnabled(false);
    gameButtons->addWidget(m_refreshGamesButton);
    gameButtons->addWidget(m_launchGameButton, 1);
    gamesLayout->addWidget(m_gameSearchEdit);
    gamesLayout->addWidget(m_gameList, 1);
    gamesLayout->addLayout(gameButtons);
    gamesLayout->addWidget(m_gameLibraryStatus);
    leftColumn->addWidget(m_gamesGroup, 1);

    m_opacityGroup = new QGroupBox("Opacity");
    QHBoxLayout *opacityLayout = new QHBoxLayout();

    m_opacitySlider = new QSlider(Qt::Horizontal);
    m_opacitySlider->setMinimum(50);
    m_opacitySlider->setMaximum(255);
    m_opacitySlider->setValue(200);

    m_opacityLabel = new QLabel("80%");
    m_opacityLabel->setMinimumWidth(40);

    opacityLayout->addWidget(m_opacitySlider);
    opacityLayout->addWidget(m_opacityLabel);
    m_opacityGroup->setLayout(opacityLayout);
    rightColumn->addWidget(m_opacityGroup);

    m_hotkeyGroup = new QGroupBox("Hotkey");
    QVBoxLayout *hotkeyLayout = new QVBoxLayout();
    QHBoxLayout *toggleHotkeyLayout = new QHBoxLayout();
    m_hotkeyLabel = new QLabel("Show/Hide: Alt + F12");
    m_hotkeyButton = new QPushButton("Change Hotkey");
    toggleHotkeyLayout->addWidget(m_hotkeyLabel, 1);
    toggleHotkeyLayout->addWidget(m_hotkeyButton);
    QHBoxLayout *recordHotkeyLayout = new QHBoxLayout();
    m_recordHotkeyLabel = new QLabel("Record: Alt + F11");
    recordHotkeyLayout->addWidget(m_recordHotkeyLabel, 1);
    hotkeyLayout->addLayout(toggleHotkeyLayout);
    hotkeyLayout->addLayout(recordHotkeyLayout);
    m_hotkeyGroup->setLayout(hotkeyLayout);
    rightColumn->addWidget(m_hotkeyGroup);

    m_btnShowOverlay = new QPushButton("Show Overlay");
    m_btnShowOverlay->setObjectName("primaryButton");
    rightColumn->addWidget(m_btnShowOverlay);

    QHBoxLayout *recordLayout = new QHBoxLayout();
    m_recordButton = new QPushButton("Start benchmark recording");
    m_openCapturesButton = new QPushButton("Open recordings folder");
    recordLayout->addWidget(m_recordButton);
    recordLayout->addWidget(m_openCapturesButton);
    rightColumn->addLayout(recordLayout);
    m_copyReportButton = new QPushButton("Copy status report");
    rightColumn->addWidget(m_copyReportButton);
    m_alertsButton = new QPushButton("Alert thresholds...");
    rightColumn->addWidget(m_alertsButton);

    m_statusLabel = new QLabel("Status: Running");
    m_statusLabel->setAlignment(Qt::AlignCenter);
    rightColumn->addWidget(m_statusLabel);

    rightColumn->addStretch();

    m_btnExit = new QPushButton("Exit");
    rightColumn->addWidget(m_btnExit);

    connect(m_btnShowOverlay, &QPushButton::clicked, this, &MainWindow::onToggleOverlay);
    connect(m_btnExit, &QPushButton::clicked, this, &MainWindow::onExitTriggered);
    connect(m_hotkeyButton, &QPushButton::clicked, this, &MainWindow::onConfigureHotkey);
    connect(m_recordButton, &QPushButton::clicked, this, &MainWindow::onToggleRecording);
    connect(m_openCapturesButton, &QPushButton::clicked, this, &MainWindow::onOpenCaptures);
    connect(m_copyReportButton, &QPushButton::clicked, this, &MainWindow::onCopyReport);
    connect(m_refreshGamesButton, &QPushButton::clicked, this, &MainWindow::onRefreshGames);
    connect(m_launchGameButton, &QPushButton::clicked, this, &MainWindow::onLaunchGame);
    connect(m_gameList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) { onLaunchGame(); });
    connect(m_gameList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *, QListWidgetItem *) { onGameSelectionChanged(); });
    connect(m_gameSearchEdit, &QLineEdit::textChanged, this, &MainWindow::onGameSearchChanged);
    connect(m_alertsButton, &QPushButton::clicked, this, &MainWindow::onConfigureAlerts);
    connect(m_opacitySlider, &QSlider::valueChanged, this, &MainWindow::onOpacityChanged);
    connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onPresetChanged);
    connect(m_restoreDefaultsButton, &QPushButton::clicked, this, &MainWindow::onRestoreDefaults);
    connect(m_checkMousePassThrough, &QCheckBox::toggled, this, &MainWindow::onMoveOverlayToggled);

    connect(m_checkCpu, &QCheckBox::toggled, this, &MainWindow::onDisplayOptionChanged);
    connect(m_checkGpu, &QCheckBox::toggled, this, &MainWindow::onDisplayOptionChanged);
    connect(m_checkMemory, &QCheckBox::toggled, this, &MainWindow::onDisplayOptionChanged);
    connect(m_checkVideoMemory, &QCheckBox::toggled, this, &MainWindow::onDisplayOptionChanged);
    connect(m_checkCpuTemp, &QCheckBox::toggled, this, &MainWindow::onDisplayOptionChanged);
    connect(m_checkGpuTemp, &QCheckBox::toggled, this, &MainWindow::onDisplayOptionChanged);
    connect(m_checkPing, &QCheckBox::toggled, this, &MainWindow::onDisplayOptionChanged);
    connect(m_checkNetworkInfo, &QCheckBox::toggled, this, &MainWindow::onDisplayOptionChanged);
    connect(m_checkDeviceInfo, &QCheckBox::toggled, this, &MainWindow::onDisplayOptionChanged);
    connect(m_checkStorageInfo, &QCheckBox::toggled, this, &MainWindow::onDisplayOptionChanged);
    connect(m_checkNetworkTraffic, &QCheckBox::toggled, this, &MainWindow::onDisplayOptionChanged);
    connect(m_checkFrameMetrics, &QCheckBox::toggled, this, &MainWindow::onDisplayOptionChanged);

    setStyleSheet(R"(
        QMainWindow, QWidget { background-color: #111925; color: #d9e4f2; font-family: 'Microsoft YaHei UI', 'Microsoft YaHei'; font-size: 10pt; }
        #titleLabel {
            font-size: 18px;
            font-weight: bold;
            color: #62dce5;
            margin-bottom: 10px;
        }
        QGroupBox { font-size: 10.5pt; font-weight: 600; border: 1px solid #2a394d; border-radius: 8px; margin-top: 10px; padding: 10px 8px 7px 8px; }
        QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 5px; color: #91a7bf; }
        QComboBox, QPushButton { background: #1b2737; border: 1px solid #34465c; border-radius: 5px; padding: 8px 10px; color: #e5edf7; font-size: 10pt; }
        QComboBox:hover, QPushButton:hover { border-color: #4dbcc8; }
        QComboBox::drop-down { border: 0; width: 22px; }
        QComboBox QAbstractItemView {
            background-color: #121c29;
            color: #e5edf7;
            border: 1px solid #3c526a;
            selection-background-color: #247d89;
            selection-color: #ffffff;
            outline: 0;
            padding: 3px;
        }
        QComboBox QAbstractItemView::item { min-height: 28px; padding: 6px 10px; }
        QComboBox QAbstractItemView::item:hover { background-color: #1b3445; }
        QSlider::groove:horizontal { height: 5px; background: #2c3a4e; border-radius: 2px; }
        QSlider::handle:horizontal { width: 13px; margin: -5px 0; border-radius: 6px; background: #5ad5dd; }
        QCheckBox { spacing: 8px; }
        QCheckBox::indicator { width: 15px; height: 15px; }
        QCheckBox::indicator:unchecked { border: 1px solid #62748a; border-radius: 3px; background: #172231; }
        QCheckBox::indicator:checked { border: 1px solid #5ad5dd; border-radius: 3px; background: #298f9b; }
        QLineEdit, QListWidget { background: #121c29; border: 1px solid #34465c; border-radius: 5px; padding: 6px; color: #e5edf7; }
        QListWidget::item { padding: 5px; }
        QListWidget::item:selected { background: #1c6772; color: #ffffff; }
        #gameLibraryStatus { color: #9aadc4; font-size: 9pt; }
        #primaryButton {
            background-color: #49cbd4;
            color: #0e1b27;
            border: none;
            padding: 9px;
            font-weight: bold;
            border-radius: 6px;
        }
        #primaryButton:hover {
            background-color: #6edfe5;
        }
    )");

    QScrollArea *settingsScroll = new QScrollArea(this);
    settingsScroll->setWidgetResizable(true);
    settingsScroll->setFrameShape(QFrame::NoFrame);
    settingsScroll->setWidget(m_centralWidget);
    setCentralWidget(settingsScroll);
}

void MainWindow::initTrayIcon()
{
    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setIcon(windowIcon());
    m_trayIcon->setToolTip("Game Monitor Overlay");

    QMenu *trayMenu = new QMenu(this);

    m_trayShowHideAction = new QAction("Show/Hide", this);
    connect(m_trayShowHideAction, &QAction::triggered, this, &MainWindow::onShowHideTriggered);

    QAction *separator = new QAction("", this);
    separator->setSeparator(true);

    m_trayExitAction = new QAction("Exit", this);
    connect(m_trayExitAction, &QAction::triggered, this, &MainWindow::onExitTriggered);

    trayMenu->addAction(m_trayShowHideAction);
    m_moveOverlayAction = new QAction("Move overlay", this);
    m_moveOverlayAction->setCheckable(true);
    connect(m_moveOverlayAction, &QAction::toggled, this, &MainWindow::onMoveOverlayToggled);
    trayMenu->addAction(m_moveOverlayAction);
    trayMenu->addAction(separator);
    trayMenu->addAction(m_trayExitAction);

    m_trayIcon->setContextMenu(trayMenu);

    connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::DoubleClick) {
            onToggleOverlay();
        }
    });

    m_trayIcon->show();
}

void MainWindow::initHotkey()
{
#ifdef Q_OS_WIN32
    QSettings settings("GameMonitor", "Settings");
    const QKeySequence savedToggle = QKeySequence::fromString(settings.value("hotkeys/toggle", "Alt+F12").toString(), QKeySequence::PortableText);
    const QKeySequence savedRecord = QKeySequence::fromString(settings.value("hotkeys/record", "Alt+F11").toString(), QKeySequence::PortableText);
    if (!savedToggle.isEmpty()) m_toggleHotkey = savedToggle;
    if (!savedRecord.isEmpty()) m_recordHotkey = savedRecord;
    if (!registerHotkey(m_toggleHotkey, 1)) {
        m_toggleHotkey = QKeySequence(Qt::ALT | Qt::Key_F12);
        if (!registerHotkey(m_toggleHotkey, 1)) m_statusLabel->setText(translate("Status: Hotkey registration failed"));
    }
    if (!registerHotkey(m_recordHotkey, 2)) {
        m_recordHotkey = QKeySequence(Qt::ALT | Qt::Key_F11);
        if (!registerHotkey(m_recordHotkey, 2)) m_statusLabel->setText(translate("Status: Recording hotkey registration failed"));
    }
    updateHotkeyLabels();
#endif
}

bool MainWindow::registerHotkey(const QKeySequence &sequence, int id)
{
#ifdef Q_OS_WIN32
    if (sequence.count() != 1) return false;
    const int encoded = sequence[0];
    const int modifiers = encoded & Qt::KeyboardModifierMask;
    const int key = encoded & ~Qt::KeyboardModifierMask;
    UINT nativeModifiers = MOD_NOREPEAT;
    if (modifiers & Qt::ALT) nativeModifiers |= MOD_ALT;
    if (modifiers & Qt::CTRL) nativeModifiers |= MOD_CONTROL;
    if (modifiers & Qt::SHIFT) nativeModifiers |= MOD_SHIFT;
    if (modifiers & Qt::META) nativeModifiers |= MOD_WIN;
    if (nativeModifiers == MOD_NOREPEAT) return false;

    UINT virtualKey = 0;
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) virtualKey = VK_F1 + (key - Qt::Key_F1);
    else if (key >= Qt::Key_A && key <= Qt::Key_Z) virtualKey = 'A' + (key - Qt::Key_A);
    else if (key >= Qt::Key_0 && key <= Qt::Key_9) virtualKey = '0' + (key - Qt::Key_0);
    else {
        switch (key) {
        case Qt::Key_Space: virtualKey = VK_SPACE; break;
        case Qt::Key_Tab: virtualKey = VK_TAB; break;
        case Qt::Key_Escape: virtualKey = VK_ESCAPE; break;
        case Qt::Key_Home: virtualKey = VK_HOME; break;
        case Qt::Key_End: virtualKey = VK_END; break;
        case Qt::Key_Insert: virtualKey = VK_INSERT; break;
        case Qt::Key_Delete: virtualKey = VK_DELETE; break;
        case Qt::Key_PageUp: virtualKey = VK_PRIOR; break;
        case Qt::Key_PageDown: virtualKey = VK_NEXT; break;
        case Qt::Key_Left: virtualKey = VK_LEFT; break;
        case Qt::Key_Right: virtualKey = VK_RIGHT; break;
        case Qt::Key_Up: virtualKey = VK_UP; break;
        case Qt::Key_Down: virtualKey = VK_DOWN; break;
        default: return false;
        }
    }
    return RegisterHotKey((HWND)winId(), id, nativeModifiers, virtualKey) != FALSE;
#else
    Q_UNUSED(sequence);
    Q_UNUSED(id);
    return false;
#endif
}

void MainWindow::updateHotkeyLabels()
{
    m_hotkeyLabel->setText(translate("Show/Hide: %1").arg(m_toggleHotkey.toString(QKeySequence::NativeText)));
    m_recordHotkeyLabel->setText(translate("Record: %1").arg(m_recordHotkey.toString(QKeySequence::NativeText)));
}

bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, long *result)
{
#ifdef Q_OS_WIN32
    MSG *msg = static_cast<MSG *>(message);
    if (msg && msg->message == WM_HOTKEY) {
        if (msg->wParam == 1) onHotkeyPressed();
        else if (msg->wParam == 2) onRecordHotkeyPressed();
        else return QMainWindow::nativeEvent(eventType, message, result);
        if (result) *result = 0;
        return true;
    }
#else
    Q_UNUSED(eventType);
    Q_UNUSED(message);
    Q_UNUSED(result);
#endif
    return QMainWindow::nativeEvent(eventType, message, result);
}

void MainWindow::loadSettings()
{
    QSettings settings("GameMonitor", "Settings");

    int opacity = settings.value("opacity", 200).toInt();
    m_opacitySlider->setValue(opacity);
    onOpacityChanged(opacity);

    const QSignalBlocker b0(m_checkCpu), bg(m_checkGpu), b1(m_checkMemory), b2(m_checkVideoMemory),
        b3(m_checkCpuTemp), b4(m_checkGpuTemp), b5(m_checkPing), b6(m_checkNetworkInfo),
        b7(m_checkDeviceInfo), b8(m_checkStorageInfo), b9(m_checkNetworkTraffic), b10(m_checkFrameMetrics);
    const bool hasPreset = settings.contains("layoutPreset");
    if (hasPreset && settings.value("layoutPreset").toInt() == 2) {
        m_checkCpu->setChecked(settings.value("showCpu", true).toBool());
        m_checkGpu->setChecked(settings.value("showGpu", true).toBool());
        m_checkMemory->setChecked(settings.value("showMemory", true).toBool());
        m_checkVideoMemory->setChecked(settings.value("showVideoMemory", false).toBool());
        m_checkCpuTemp->setChecked(settings.value("showCpuTemp", true).toBool());
        m_checkGpuTemp->setChecked(settings.value("showGpuTemp", true).toBool());
        m_checkPing->setChecked(settings.value("showPing", false).toBool());
        m_checkNetworkInfo->setChecked(settings.value("showNetworkInfo", false).toBool());
        m_checkDeviceInfo->setChecked(settings.value("showDeviceInfo", false).toBool());
        m_checkStorageInfo->setChecked(settings.value("showStorageInfo", false).toBool());
        m_checkNetworkTraffic->setChecked(settings.value("showNetworkTraffic", true).toBool());
        m_checkFrameMetrics->setChecked(settings.value("showFrameMetrics", true).toBool());
    }
    m_checkMousePassThrough->setChecked(false);

    const int preset = qBound(0, hasPreset ? settings.value("layoutPreset", 0).toInt() : 0, 2);
    m_presetCombo->setCurrentIndex(preset);
    if (!hasPreset || preset != 2) applyPreset(preset);
    else onDisplayOptionChanged();
    if (m_overlay) m_overlay->setMousePassThrough(true);
    updateUIText();
    onOpacityChanged(opacity);
}

void MainWindow::saveSettings()
{
    if (m_loadingGameProfile) return;
    QSettings settings("GameMonitor", "Settings");
    if (!m_activeGameExecutable.isEmpty()) {
        saveGameProfile(settings);
        return;
    }
    settings.setValue("opacity", m_opacitySlider->value());
    settings.setValue("showCpu", m_checkCpu->isChecked());
    settings.setValue("showGpu", m_checkGpu->isChecked());
    settings.setValue("showMemory", m_checkMemory->isChecked());
    settings.setValue("showVideoMemory", m_checkVideoMemory->isChecked());
    settings.setValue("showCpuTemp", m_checkCpuTemp->isChecked());
    settings.setValue("showGpuTemp", m_checkGpuTemp->isChecked());
    settings.setValue("showPing", m_checkPing->isChecked());
    settings.setValue("showNetworkInfo", m_checkNetworkInfo->isChecked());
    settings.setValue("showDeviceInfo", m_checkDeviceInfo->isChecked());
    settings.setValue("showStorageInfo", m_checkStorageInfo->isChecked());
    settings.setValue("showNetworkTraffic", m_checkNetworkTraffic->isChecked());
    settings.setValue("showFrameMetrics", m_checkFrameMetrics->isChecked());
    settings.setValue("layoutPreset", m_presetCombo->currentData().toInt());
}

void MainWindow::saveGameProfile(QSettings &settings)
{
    if (m_activeGameExecutable.isEmpty() || m_loadingGameProfile) return;
    settings.beginGroup("gameProfiles/" + m_activeGameExecutable.toLower());
    settings.setValue("opacity", m_opacitySlider->value());
    settings.setValue("layoutPreset", m_presetCombo->currentData().toInt());
    settings.setValue("showCpu", m_checkCpu->isChecked());
    settings.setValue("showGpu", m_checkGpu->isChecked());
    settings.setValue("showMemory", m_checkMemory->isChecked());
    settings.setValue("showVideoMemory", m_checkVideoMemory->isChecked());
    settings.setValue("showCpuTemp", m_checkCpuTemp->isChecked());
    settings.setValue("showGpuTemp", m_checkGpuTemp->isChecked());
    settings.setValue("showPing", m_checkPing->isChecked());
    settings.setValue("showNetworkInfo", m_checkNetworkInfo->isChecked());
    settings.setValue("showDeviceInfo", m_checkDeviceInfo->isChecked());
    settings.setValue("showStorageInfo", m_checkStorageInfo->isChecked());
    settings.setValue("showNetworkTraffic", m_checkNetworkTraffic->isChecked());
    settings.setValue("showFrameMetrics", m_checkFrameMetrics->isChecked());
    settings.endGroup();
}

void MainWindow::loadGameProfile(const QString &executable)
{
    const bool hasGame = !executable.isEmpty();
    QSettings settings("GameMonitor", "Settings");
    const QString group = hasGame ? "gameProfiles/" + executable.toLower() : QString();
    bool hasProfile = false;
    if (hasGame) {
        settings.beginGroup(group);
        hasProfile = settings.contains("layoutPreset");
        settings.endGroup();
    }
    if (hasProfile) {
        settings.beginGroup(group);
    } else if (hasGame) {
        m_loadingGameProfile = true;
        applyPreset(0);
        m_opacitySlider->setValue(settings.value("opacity", 200).toInt());
        m_loadingGameProfile = false;
        onOpacityChanged(m_opacitySlider->value());
        saveSettings();
        return;
    }

    m_loadingGameProfile = true;
    const QSignalBlocker b0(m_checkCpu), bg(m_checkGpu), b1(m_checkMemory), b2(m_checkVideoMemory),
        b3(m_checkCpuTemp), b4(m_checkGpuTemp), b5(m_checkPing), b6(m_checkNetworkInfo),
        b7(m_checkDeviceInfo), b8(m_checkStorageInfo), b9(m_checkNetworkTraffic), b10(m_checkFrameMetrics),
        b11(m_presetCombo), b12(m_opacitySlider);
    const int preset = qBound(0, settings.value("layoutPreset", 0).toInt(), 2);
    m_presetCombo->setCurrentIndex(preset);
    applyPreset(preset);
    m_checkCpu->setChecked(settings.value("showCpu", true).toBool());
    m_checkGpu->setChecked(settings.value("showGpu", true).toBool());
    m_checkMemory->setChecked(settings.value("showMemory", true).toBool());
    m_checkVideoMemory->setChecked(settings.value("showVideoMemory", false).toBool());
    m_checkCpuTemp->setChecked(settings.value("showCpuTemp", true).toBool());
    m_checkGpuTemp->setChecked(settings.value("showGpuTemp", true).toBool());
    m_checkPing->setChecked(settings.value("showPing", false).toBool());
    m_checkNetworkInfo->setChecked(settings.value("showNetworkInfo", false).toBool());
    m_checkDeviceInfo->setChecked(settings.value("showDeviceInfo", false).toBool());
    m_checkStorageInfo->setChecked(settings.value("showStorageInfo", false).toBool());
    m_checkNetworkTraffic->setChecked(settings.value("showNetworkTraffic", true).toBool());
    m_checkFrameMetrics->setChecked(settings.value("showFrameMetrics", true).toBool());
    m_opacitySlider->setValue(settings.value("opacity", 200).toInt());
    if (hasGame) settings.endGroup();
    m_loadingGameProfile = false;
    onOpacityChanged(m_opacitySlider->value());
    onDisplayOptionChanged();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    hide();
    event->ignore();
}

void MainWindow::onToggleOverlay()
{
    if (m_overlay) {
        m_overlay->toggleVisibility();

        if (m_overlay->isOverlayVisible()) {
            m_btnShowOverlay->setText(translate("Hide Overlay"));
        } else {
            m_btnShowOverlay->setText(translate("Show Overlay"));
        }
    }
}

void MainWindow::onShowHideTriggered()
{
    onToggleOverlay();
}

void MainWindow::onExitTriggered()
{
    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        translate("Confirm Exit"),
        translate("Are you sure you want to exit?"),
        QMessageBox::Yes | QMessageBox::No
    );

    if (reply == QMessageBox::Yes) {
        saveSettings();
        qApp->quit();
    }
}

void MainWindow::onHotkeyPressed()
{
    onToggleOverlay();
}

void MainWindow::onConfigureHotkey()
{
    if (m_configuringHotkey) return;
    m_configuringHotkey = true;
    QDialog dialog(this);
    dialog.setWindowTitle(translate("Change Hotkey"));
    QFormLayout *form = new QFormLayout(&dialog);
    QKeySequenceEdit *toggleEdit = new QKeySequenceEdit(m_toggleHotkey, &dialog);
    QKeySequenceEdit *recordEdit = new QKeySequenceEdit(m_recordHotkey, &dialog);
    form->addRow(translate("Show/Hide overlay"), toggleEdit);
    form->addRow(translate("Start/stop recording"), recordEdit);
    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Save)->setText(translate("Save"));
    buttons->button(QDialogButtonBox::Cancel)->setText(translate("Cancel"));
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) { m_configuringHotkey = false; return; }

    const QKeySequence newToggle = toggleEdit->keySequence();
    const QKeySequence newRecord = recordEdit->keySequence();
    if (newToggle.isEmpty() || newRecord.isEmpty() || newToggle == newRecord) {
        QMessageBox::warning(this, translate("Hotkey conflict"), translate("Choose two different shortcuts with a modifier key."));
        m_configuringHotkey = false;
        return;
    }
#ifdef Q_OS_WIN32
    UnregisterHotKey((HWND)winId(), 1);
    UnregisterHotKey((HWND)winId(), 2);
    const bool toggleOk = registerHotkey(newToggle, 1);
    const bool recordOk = toggleOk && registerHotkey(newRecord, 2);
    if (!toggleOk || !recordOk) {
        UnregisterHotKey((HWND)winId(), 1);
        UnregisterHotKey((HWND)winId(), 2);
        registerHotkey(m_toggleHotkey, 1);
        registerHotkey(m_recordHotkey, 2);
        QMessageBox::warning(this, translate("Hotkey conflict"), translate("That shortcut is already in use or unsupported; existing shortcuts were kept."));
        m_configuringHotkey = false;
        return;
    }
#endif
    m_toggleHotkey = newToggle;
    m_recordHotkey = newRecord;
    QSettings settings("GameMonitor", "Settings");
    settings.setValue("hotkeys/toggle", m_toggleHotkey.toString(QKeySequence::PortableText));
    settings.setValue("hotkeys/record", m_recordHotkey.toString(QKeySequence::PortableText));
    updateHotkeyLabels();
    m_configuringHotkey = false;
}

void MainWindow::onRecordHotkeyPressed()
{
    onToggleRecording();
}

void MainWindow::onToggleRecording()
{
    if (!m_monitor || m_recordingRequestPending) return;
    const bool enabled = !m_recording;
    m_recordingRequestPending = true;
    m_recordButton->setEnabled(false);
    if (!QMetaObject::invokeMethod(m_monitor, "setRecordingEnabled", Qt::QueuedConnection, Q_ARG(bool, enabled))) {
        m_recordingRequestPending = false;
        m_recordButton->setEnabled(true);
        m_statusLabel->setText(translate("Recording unavailable"));
    }
}

void MainWindow::onRecordingStateChanged(bool recording, const QString &path, const QString &message)
{
    m_recordingRequestPending = false;
    m_recording = recording;
    m_recordButton->setEnabled(true);
    m_recordButton->setText(translate(recording ? "Stop benchmark recording" : "Start benchmark recording"));
    if (!message.isEmpty()) {
        m_statusLabel->setText(message);
        if (m_trayIcon) m_trayIcon->showMessage(translate("Recording unavailable"), message, QSystemTrayIcon::Warning, 5000);
    } else if (!recording && !path.isEmpty()) {
        m_statusLabel->setText(translate("Recording saved"));
        if (m_trayIcon) m_trayIcon->showMessage(translate("Recording saved"), path, QSystemTrayIcon::Information, 5000);
    } else if (recording) {
        m_statusLabel->setText(translate("Recording game frames"));
    }
}

void MainWindow::onForegroundProcessChanged(const QString &executable, uint processId)
{
    m_activeProcessId = processId;
    if (m_activeGameExecutable == executable) return;
    m_activeGameExecutable = executable;
    loadGameProfile(executable);
}

void MainWindow::onOpenCaptures()
{
    QString folder = QString::fromLocal8Bit(qgetenv("LOCALAPPDATA"));
    folder = folder.isEmpty() ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath("captures")
                              : QDir(folder).filePath("SystemWatch/captures");
    QDir().mkpath(folder);
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

void MainWindow::onCopyReport()
{
    if (!m_monitor) return;
    const QString unavailable = translate("Unavailable");
    const auto numberOrUnavailable = [&unavailable](double value, int precision, const QString &suffix) {
        return value >= 0.0 ? QString::number(value, 'f', precision) + suffix : unavailable;
    };
    QStringList lines;
    const double fps = m_monitor->getFps();
    const QString fpsText = fps > 0.0 ? QString::number(fps, 'f', 0) : unavailable;
    lines << translate("System status report")
          << QString("%1: %2").arg(translate("Device Information"), m_monitor->getDeviceInfo())
          << QString("%1: %2 | %3: %4").arg(translate("CPU Usage"))
                .arg(numberOrUnavailable(m_monitor->getCpuUsage(), 1, "%"))
                .arg(translate("CPU Temperature"))
                .arg(numberOrUnavailable(m_monitor->getCpuTemperature(), 0, QString::fromUtf8(" \xC2\xB0" "C")))
          << QString("%1: %2 | %3: %4").arg(translate("GPU Usage"))
                .arg(numberOrUnavailable(m_monitor->getGpuUsage(), 1, "%"))
                .arg(translate("GPU Temperature"))
                .arg(numberOrUnavailable(m_monitor->getGpuTemperature(), 0, QString::fromUtf8(" \xC2\xB0" "C")))
          << QString("%1: %2 / %3 GB").arg(translate("Memory Usage"))
                .arg(m_monitor->getMemoryUsageMB() / 1024.0, 0, 'f', 1)
                .arg(m_monitor->getTotalMemoryMB() / 1024.0, 0, 'f', 1)
          << QString("%1: %2 / %3 Mbps").arg(translate("Network Traffic + Trend"))
                .arg(m_monitor->getNetworkDownloadMbps(), 0, 'f', 2)
                .arg(m_monitor->getNetworkUploadMbps(), 0, 'f', 2)
          << QString("FPS: %1 | %2: %3 ms | 1% Low: %4")
                .arg(fpsText, translate("Frame time"))
                .arg(numberOrUnavailable(m_monitor->getFrameTimeMs(), 2, ""))
                .arg(numberOrUnavailable(m_monitor->getOnePercentLowFps(), 1, ""))
          << QString("%1: %2").arg(translate("Network Information"), m_monitor->getNetworkInfo())
          << QString("%1: %2").arg(translate("Storage Information"), m_monitor->getStorageInfo());
    QApplication::clipboard()->setText(lines.join("\n"));
    m_statusLabel->setText(translate("Report copied"));
}

void MainWindow::onRefreshGames()
{
    if (m_gameScanner && m_gameScanner->isRunning()) return;
    if (m_gameScanner) {
        delete m_gameScanner;
        m_gameScanner = nullptr;
    }
    m_gameList->clear();
    m_gameLibraryStatus->setText(translate("Scanning installed games..."));
    m_refreshGamesButton->setEnabled(false);
    m_gameScanner = new GameLibraryScanner(this);
    connect(m_gameScanner, &QThread::finished, this, &MainWindow::onGamesScanned);
    m_gameScanner->start();
}

void MainWindow::onGamesScanned()
{
    if (!m_gameScanner) return;
    GameLibraryScanner *scanner = m_gameScanner;
    m_gameScanner = nullptr;
    const QList<GameEntry> games = scanner->games();
    scanner->deleteLater();
    for (const GameEntry &game : games) {
        QListWidgetItem *item = new QListWidgetItem(QString("%1  [%2]").arg(game.name, game.platform), m_gameList);
        item->setData(Qt::UserRole, game.launchUrl);
        item->setToolTip(game.name + "\n" + game.platform);
    }
    m_gameLibraryStatus->setText(games.isEmpty()
        ? translate("No supported games found (Steam/Epic).")
        : translate("Found %1 games").arg(games.size()));
    m_refreshGamesButton->setEnabled(true);
    if (!games.isEmpty()) m_gameList->setCurrentRow(0);
    onGameSelectionChanged();
}

void MainWindow::onLaunchGame()
{
    QListWidgetItem *item = m_gameList->currentItem();
    if (!item || item->isHidden()) return;
    const QUrl launchUrl(item->data(Qt::UserRole).toString());
    if (!launchUrl.isValid() || !QDesktopServices::openUrl(launchUrl)) {
        m_gameLibraryStatus->setText(translate("Could not launch game. Check that its launcher is installed."));
        return;
    }
    m_gameLibraryStatus->setText(translate("Game launch requested"));
}

void MainWindow::onGameSelectionChanged()
{
    QListWidgetItem *item = m_gameList->currentItem();
    m_launchGameButton->setEnabled(item && !item->isHidden() && !item->data(Qt::UserRole).toString().isEmpty());
}

void MainWindow::onGameSearchChanged(const QString &text)
{
    for (int row = 0; row < m_gameList->count(); ++row) {
        QListWidgetItem *item = m_gameList->item(row);
        item->setHidden(!item->text().contains(text.trimmed(), Qt::CaseInsensitive));
    }
    onGameSelectionChanged();
}

void MainWindow::onConfigureAlerts()
{
    QSettings settings("GameMonitor", "Settings");
    QDialog dialog(this);
    dialog.setWindowTitle(translate("Alert thresholds"));
    QFormLayout *form = new QFormLayout(&dialog);
    QSpinBox *cpuTemp = new QSpinBox(&dialog); cpuTemp->setRange(0, 110); cpuTemp->setSuffix(" °C"); cpuTemp->setSpecialValueText(translate("Off"));
    QSpinBox *gpuTemp = new QSpinBox(&dialog); gpuTemp->setRange(0, 110); gpuTemp->setSuffix(" °C"); gpuTemp->setSpecialValueText(translate("Off"));
    QSpinBox *memory = new QSpinBox(&dialog); memory->setRange(0, 100); memory->setSuffix("%"); memory->setSpecialValueText(translate("Off"));
    QSpinBox *vram = new QSpinBox(&dialog); vram->setRange(0, 100); vram->setSuffix("%"); vram->setSpecialValueText(translate("Off"));
    cpuTemp->setValue(settings.value("alerts/cpuTemp", 90).toInt());
    gpuTemp->setValue(settings.value("alerts/gpuTemp", 85).toInt());
    memory->setValue(settings.value("alerts/memory", 90).toInt());
    vram->setValue(settings.value("alerts/vram", 90).toInt());
    form->addRow(translate("CPU temperature"), cpuTemp);
    form->addRow(translate("GPU temperature"), gpuTemp);
    form->addRow(translate("Memory usage"), memory);
    form->addRow(translate("Game VRAM usage"), vram);
    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Save)->setText(translate("Save"));
    buttons->button(QDialogButtonBox::Cancel)->setText(translate("Cancel"));
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    settings.setValue("alerts/cpuTemp", cpuTemp->value());
    settings.setValue("alerts/gpuTemp", gpuTemp->value());
    settings.setValue("alerts/memory", memory->value());
    settings.setValue("alerts/vram", vram->value());
}

void MainWindow::checkThreshold(const QString &key, const QString &title, double value, double threshold, const QString &unit)
{
    if (threshold <= 0.0 || value < threshold) {
        m_activeAlerts.remove(key);
        m_alertLastShown.remove(key);
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_activeAlerts.contains(key) && now - m_alertLastShown.value(key) < 5 * 60 * 1000) return;
    m_activeAlerts.insert(key);
    m_alertLastShown.insert(key, now);
    if (m_trayIcon && m_trayIcon->isVisible())
        m_trayIcon->showMessage(title, QString("%1%2").arg(value, 0, 'f', 0).arg(unit), QSystemTrayIcon::Warning, 5000);
}

void MainWindow::onMonitorUpdated()
{
    if (!m_monitor) return;
    QSettings settings("GameMonitor", "Settings");
    checkThreshold("cpuTemp", translate("CPU temperature"), m_monitor->getCpuTemperature(), settings.value("alerts/cpuTemp", 90).toDouble(), " °C");
    checkThreshold("gpuTemp", translate("GPU temperature"), m_monitor->getGpuTemperature(), settings.value("alerts/gpuTemp", 85).toDouble(), " °C");
    checkThreshold("memory", translate("Memory usage"), m_monitor->getMemoryUsagePercent(), settings.value("alerts/memory", 90).toDouble(), "%");
    const double totalVram = m_monitor->getVideoMemoryTotalMB();
    const double usedVram = m_monitor->getVideoMemoryUsedMB();
    const double vramPercent = totalVram > 0.0 && usedVram >= 0.0 ? usedVram * 100.0 / totalVram : -1.0;
    checkThreshold("vram", translate("Game VRAM usage"), vramPercent, settings.value("alerts/vram", 90).toDouble(), "%");
}

void MainWindow::onOpacityChanged(int value)
{
    int percent = (value * 100) / 255;
    m_opacityLabel->setText(QString::number(percent) + "%");
    if (m_overlay) m_overlay->setWindowOpacity(value / 255.0);
    saveSettings();
}

void MainWindow::onDisplayOptionChanged()
{
    if (m_presetCombo && m_presetCombo->currentData().toInt() != 2)
        m_presetCombo->setCurrentIndex(2);
    if (m_overlay) {
        m_overlay->setMetricVisibility(m_checkCpu->isChecked(), m_checkGpu->isChecked(), m_checkMemory->isChecked(),
                                       m_checkVideoMemory->isChecked(), m_checkCpuTemp->isChecked(),
                                       m_checkGpuTemp->isChecked(), m_checkPing->isChecked(),
                                       m_checkNetworkInfo->isChecked(), m_checkDeviceInfo->isChecked(),
                                       m_checkStorageInfo->isChecked(), m_checkNetworkTraffic->isChecked(),
                                       m_checkFrameMetrics->isChecked());
        m_overlay->setMousePassThrough(!m_checkMousePassThrough->isChecked());
    }
    saveSettings();
}

void MainWindow::applyPreset(int preset)
{
    if (preset == 2) {
        const bool custom = true;
        const char *names[] = {"customCpuGroup", "customGpuGroup", "customNetworkGroup", "customStorageGroup"};
        for (int i = 0; i < 4; ++i) {
            QGroupBox *group = findChild<QGroupBox *>(QLatin1String(names[i]));
            if (group) group->setVisible(custom);
        }
        return;
    }
    const bool system = preset == 1;
    const QSignalBlocker b0(m_checkCpu), bg(m_checkGpu), b1(m_checkMemory), b2(m_checkVideoMemory),
        b3(m_checkCpuTemp), b4(m_checkGpuTemp), b5(m_checkPing), b6(m_checkNetworkInfo),
        b7(m_checkDeviceInfo), b8(m_checkStorageInfo), b9(m_checkNetworkTraffic), b10(m_checkFrameMetrics);
    m_checkCpu->setChecked(true);
    m_checkGpu->setChecked(true);
    m_checkMemory->setChecked(true);
    m_checkVideoMemory->setChecked(system);
    m_checkCpuTemp->setChecked(true);
    m_checkGpuTemp->setChecked(true);
    m_checkPing->setChecked(system);
    m_checkNetworkInfo->setChecked(system);
    m_checkDeviceInfo->setChecked(system);
    m_checkStorageInfo->setChecked(system);
    m_checkNetworkTraffic->setChecked(true);
    m_checkFrameMetrics->setChecked(true);
    const char *names[] = {"customCpuGroup", "customGpuGroup", "customNetworkGroup", "customStorageGroup"};
    for (int i = 0; i < 4; ++i) {
        QGroupBox *group = findChild<QGroupBox *>(QLatin1String(names[i]));
        if (group) group->setVisible(false);
    }
    if (m_overlay) {
        m_overlay->setMetricVisibility(true, true, true, system, true, true, system, system,
                                       system, system, true, true);
    }
    saveSettings();
}

void MainWindow::onPresetChanged(int index)
{
    const int preset = m_presetCombo->itemData(index).toInt();
    applyPreset(preset);
    if (preset == 2) onDisplayOptionChanged();
}

void MainWindow::onRestoreDefaults()
{
    m_presetCombo->setCurrentIndex(0);
    applyPreset(0);
}

void MainWindow::onMoveOverlayToggled(bool moving)
{
    if (m_checkMousePassThrough->isChecked() != moving) {
        const QSignalBlocker blocker(m_checkMousePassThrough);
        m_checkMousePassThrough->setChecked(moving);
    }
    if (m_moveOverlayAction && m_moveOverlayAction->isChecked() != moving) {
        const QSignalBlocker blocker(m_moveOverlayAction);
        m_moveOverlayAction->setChecked(moving);
    }
    if (m_overlay) m_overlay->setMousePassThrough(!moving);
    if (m_moveOverlayAction)
        m_moveOverlayAction->setText(translate(moving ? "Finish moving overlay" : "Move overlay"));
    if (m_checkMousePassThrough)
        m_checkMousePassThrough->setText(translate(moving ? "Finish moving overlay" : "Move overlay"));
}

void MainWindow::updateUIText()
{
    setWindowTitle(translate("Game Monitor Overlay"));
    QLabel *titleLabel = findChild<QLabel *>("titleLabel");
    if (titleLabel) titleLabel->setText(translate("Game Monitor Overlay"));
    
    m_displayGroup->setTitle(translate("Display Options"));
    m_gamesGroup->setTitle(translate("Game library (Steam / Epic)"));
    m_gameSearchEdit->setPlaceholderText(translate("Search games..."));
    m_refreshGamesButton->setText(translate("Refresh library"));
    m_launchGameButton->setText(translate("Launch selected game"));
    m_checkCpu->setText(translate("CPU Usage"));
    m_checkGpu->setText(translate("GPU Usage"));
    m_checkMemory->setText(translate("Memory Usage"));
    m_checkVideoMemory->setText(translate("Game VRAM usage"));
    m_checkCpuTemp->setText(translate("CPU Temperature"));
    m_checkGpuTemp->setText(translate("GPU Temperature"));
    m_checkPing->setText(translate("Network Latency"));
    m_checkNetworkInfo->setText(translate("Network Information"));
    m_checkDeviceInfo->setText(translate("Device Information"));
    m_checkStorageInfo->setText(translate("Storage Information"));
    m_checkNetworkTraffic->setText(translate("Network Traffic + Trend"));
    m_checkFrameMetrics->setText(translate("FPS / Frame Time / 1% Low"));
    m_presetCombo->setItemText(0, translate("Gaming HUD"));
    m_presetCombo->setItemText(1, translate("System information"));
    m_presetCombo->setItemText(2, translate("Custom"));
    m_presetLabel->setText(translate("Layout"));
    m_restoreDefaultsButton->setText(translate("Recommended"));
    m_checkMousePassThrough->setText(translate(m_checkMousePassThrough->isChecked() ? "Finish moving overlay" : "Move overlay"));
    if (m_moveOverlayAction) m_moveOverlayAction->setText(translate(m_checkMousePassThrough->isChecked() ? "Finish moving overlay" : "Move overlay"));
    const char *groupNames[] = {"customCpuGroup", "customGpuGroup", "customNetworkGroup", "customStorageGroup"};
    const char *groupTitles[] = {"CPU and memory", "Graphics", "Network", "Memory and storage details"};
    for (int i = 0; i < 4; ++i) {
        QGroupBox *group = findChild<QGroupBox *>(QLatin1String(groupNames[i]));
        if (group) group->setTitle(translate(groupTitles[i]));
    }
    if (m_overlay) m_overlay->setLanguage(1);
    
    m_opacityGroup->setTitle(translate("Opacity"));
    m_hotkeyGroup->setTitle(translate("Hotkey"));
    updateHotkeyLabels();
    m_hotkeyButton->setText(translate("Change Hotkey"));
    m_recordButton->setText(translate(m_recording ? "Stop benchmark recording" : "Start benchmark recording"));
    m_openCapturesButton->setText(translate("Open recordings folder"));
    m_copyReportButton->setText(translate("Copy status report"));
    m_alertsButton->setText(translate("Alert thresholds..."));
    
    if (m_overlay && m_overlay->isOverlayVisible()) {
        m_btnShowOverlay->setText(translate("Hide Overlay"));
    } else {
        m_btnShowOverlay->setText(translate("Show Overlay"));
    }
    
    m_statusLabel->setText(translate(m_recording ? "Recording game frames" : "Status: Running"));
    m_btnExit->setText(translate("Exit"));
    
    m_trayIcon->setToolTip(translate("Game Monitor Overlay"));
    if (m_trayShowHideAction) m_trayShowHideAction->setText(translate("Show/Hide"));
    if (m_trayExitAction) m_trayExitAction->setText(translate("Exit"));
    
}

QString MainWindow::translate(const QString &key)
{
    if (m_currentLang == 1) {
        static QMap<QString, QString> cnMap;
        if (cnMap.isEmpty()) {
            cnMap["Game Monitor Overlay"] = QString::fromUtf8("\xe6\xb8\xb8\xe6\x88\x8f\xe7\x9b\x91\xe6\x8e\xa7\xe6\x82\xac\xe6\xb5\xae\xe7\xaa\x97");
            cnMap["Display Options"] = QString::fromUtf8("\xe6\x98\xbe\xe7\xa4\xba\xe9\x80\x89\xe9\xa1\xb9");
            cnMap["GPU Usage"] = QString::fromUtf8("GPU \xe4\xbd\xbf\xe7\x94\xa8\xe7\x8e\x87");
            cnMap["CPU Usage"] = QString::fromUtf8("CPU \xe4\xbd\xbf\xe7\x94\xa8\xe7\x8e\x87");
            cnMap["Memory Usage"] = QString::fromUtf8("\xe5\x86\x85\xe5\xad\x98\xe4\xbd\xbf\xe7\x94\xa8");
            cnMap["Video Memory"] = QString::fromUtf8("\xe6\x98\xbe\xe5\xad\x98\xe6\x80\xbb\xe9\x87\x8f\xef\xbc\x88\xe4\xbc\xb0\xe7\xae\x97\xef\xbc\x89");
            cnMap["CPU Temperature"] = QString::fromUtf8("CPU \xe6\xb8\xa9\xe5\xba\xa6");
            cnMap["GPU Temperature"] = QString::fromUtf8("GPU \xe6\xb8\xa9\xe5\xba\xa6");
            cnMap["Network Latency"] = QString::fromUtf8("\xe7\xbd\x91\xe7\xbb\x9c\xe5\xbb\xb6\xe8\xbf\x9f");
            cnMap["Network Information"] = QString::fromUtf8("\xe7\xbd\x91\xe7\xbb\x9c\xe4\xbf\xa1\xe6\x81\xaf");
            cnMap["Device Information"] = QString::fromUtf8("\xe8\xae\xbe\xe5\xa4\x87\xe4\xbf\xa1\xe6\x81\xaf");
            cnMap["Storage Information"] = QString::fromUtf8("\xe5\xad\x98\xe5\x82\xa8\xe4\xbf\xa1\xe6\x81\xaf");
            cnMap["Network Traffic + Trend"] = QString::fromUtf8("\xe7\xbd\x91\xe7\xbb\x9c\xe6\xb5\x81\xe9\x87\x8f + \xe8\xb6\x8b\xe5\x8a\xbf\xe5\x9b\xbe");
            cnMap["FPS / Frame Time / 1% Low"] = QString::fromUtf8("FPS / \xe5\xb8\xa7\xe6\x97\xb6\xe9\x97\xb4 / 1% Low");
            cnMap["Mouse pass-through (disable to move overlay)"] = QString::fromUtf8("\xe9\xbc\xa0\xe6\xa0\x87\xe7\xa9\xbf\xe9\x80\x8f\xef\xbc\x88\xe5\x85\xb3\xe9\x97\xad\xe5\x90\x8e\xe5\x8f\xaf\xe7\xa7\xbb\xe5\x8a\xa8\xe6\x82\xac\xe6\xb5\xae\xe7\xaa\x97\xef\xbc\x89");
            cnMap["Gaming HUD"] = QString::fromUtf8("\xe6\xb8\xb8\xe6\x88\x8f\xe6\xa0\xb8\xe5\xbf\x83");
            cnMap["Layout"] = QString::fromUtf8("\xe5\xb8\x83\xe5\xb1\x80");
            cnMap["System information"] = QString::fromUtf8("\xe7\xb3\xbb\xe7\xbb\x9f\xe4\xbf\xa1\xe6\x81\xaf");
            cnMap["Custom"] = QString::fromUtf8("\xe8\x87\xaa\xe5\xae\x9a\xe4\xb9\x89");
            cnMap["Recommended"] = QString::fromUtf8("\xe6\x81\xa2\xe5\xa4\x8d\xe6\xb8\xb8\xe6\x88\x8f\xe9\xbb\x98\xe8\xae\xa4");
            cnMap["Move overlay"] = QString::fromUtf8("\xe7\xa7\xbb\xe5\x8a\xa8\xe6\x82\xac\xe6\xb5\xae\xe7\xaa\x97");
            cnMap["Finish moving overlay"] = QString::fromUtf8("\xe5\xae\x8c\xe6\x88\x90\xe7\xa7\xbb\xe5\x8a\xa8");
            cnMap["CPU and memory"] = QString::fromUtf8("CPU \xe4\xb8\x8e\xe5\x86\x85\xe5\xad\x98");
            cnMap["Graphics"] = QString::fromUtf8("\xe6\x98\xbe\xe5\x8d\xa1");
            cnMap["Network"] = QString::fromUtf8("\xe7\xbd\x91\xe7\xbb\x9c");
            cnMap["Memory and storage details"] = QString::fromUtf8("\xe5\xad\x98\xe5\x82\xa8\xe8\xaf\xa6\xe6\x83\x85");
            cnMap["Opacity"] = QString::fromUtf8("\xe9\x80\x8f\xe6\x98\x8e\xe5\xba\xa6");
            cnMap["Hotkey"] = QString::fromUtf8("\xe5\xbf\xab\xe6\x8d\xb7\xe9\x94\xae");
            cnMap["Save"] = QString::fromUtf8("\xe4\xbf\x9d\xe5\xad\x98");
            cnMap["Cancel"] = QString::fromUtf8("\xe5\x8f\x96\xe6\xb6\x88");
            cnMap["Copy status report"] = QString::fromUtf8("\xe5\xa4\x8d\xe5\x88\xb6\xe7\xb3\xbb\xe7\xbb\x9f\xe6\x8a\xa5\xe5\x91\x8a");
            cnMap["System status report"] = QString::fromUtf8("\xe7\xb3\xbb\xe7\xbb\x9f\xe7\x8a\xb6\xe6\x80\x81\xe6\x8a\xa5\xe5\x91\x8a");
            cnMap["Language"] = QString::fromUtf8("\xe8\xaf\xad\xe8\xa8\x80");
            cnMap["Frame time"] = QString::fromUtf8("\xe5\xb8\xa7\xe6\x97\xb6\xe9\x97\xb4");
            cnMap["Game library (Steam / Epic)"] = QString::fromUtf8("\xe6\xb8\xb8\xe6\x88\x8f\xe5\xba\x93\xef\xbc\x88Steam / Epic\xef\xbc\x89");
            cnMap["Search games..."] = QString::fromUtf8("\xe6\x90\x9c\xe7\xb4\xa2\xe6\xb8\xb8\xe6\x88\x8f...");
            cnMap["Scanning installed games..."] = QString::fromUtf8("\xe6\xad\xa3\xe5\x9c\xa8\xe6\x89\xab\xe6\x8f\x8f\xe5\xb7\xb2\xe5\xae\x89\xe8\xa3\x85\xe6\xb8\xb8\xe6\x88\x8f...");
            cnMap["Refresh library"] = QString::fromUtf8("\xe5\x88\xb7\xe6\x96\xb0\xe6\xb8\xb8\xe6\x88\x8f\xe5\xba\x93");
            cnMap["Launch selected game"] = QString::fromUtf8("\xe5\x90\xaf\xe5\x8a\xa8\xe9\x80\x89\xe4\xb8\xad\xe6\xb8\xb8\xe6\x88\x8f");
            cnMap["No supported games found (Steam/Epic)."] = QString::fromUtf8("\xe6\x9c\xaa\xe6\x89\xbe\xe5\x88\xb0\xe6\x94\xaf\xe6\x8c\x81\xe7\x9a\x84\xe6\xb8\xb8\xe6\x88\x8f\xef\xbc\x88Steam / Epic\xef\xbc\x89");
            cnMap["Found %1 games"] = QString::fromUtf8("\xe5\xb7\xb2\xe6\x89\xbe\xe5\x88\xb0 %1 \xe6\xac\xbe\xe6\xb8\xb8\xe6\x88\x8f");
            cnMap["Could not launch game. Check that its launcher is installed."] = QString::fromUtf8("\xe6\x97\xa0\xe6\xb3\x95\xe5\x90\xaf\xe5\x8a\xa8\xe6\xb8\xb8\xe6\x88\x8f\xef\xbc\x8c\xe8\xaf\xb7\xe7\xa1\xae\xe8\xae\xa4\xe5\xaf\xb9\xe5\xba\x94\xe5\x90\xaf\xe5\x8a\xa8\xe5\x99\xa8\xe5\xb7\xb2\xe5\xae\x89\xe8\xa3\x85");
            cnMap["Game launch requested"] = QString::fromUtf8("\xe5\xb7\xb2\xe5\x8f\x91\xe5\x87\xba\xe6\xb8\xb8\xe6\x88\x8f\xe5\x90\xaf\xe5\x8a\xa8\xe8\xaf\xb7\xe6\xb1\x82");
            cnMap["Report copied"] = QString::fromUtf8("\xE7\xB3\xBB\xE7\xBB\x9F\xE6\x8A\xA5\xE5\x91\x8A\xE5\xB7\xB2\xE5\xA4\x8D\xE5\x88\xB6\xE5\x88\xB0\xE5\x89\xAA\xE8\xB4\xB4\xE6\x9D\xBF");
            cnMap["Unavailable"] = QString::fromUtf8("\xe4\xb8\x8d\xe5\x8f\xaf\xe7\x94\xa8");
            cnMap["Show/Hide: Alt + F12"] = QString::fromUtf8("\xe6\x98\xbe\xe7\xa4\xba/\xe9\x9a\x90\xe8\x97\x8f: Alt + F12");
            cnMap["Change Hotkey"] = QString::fromUtf8("\xe6\x9b\xb4\xe6\x94\xb9\xe5\xbf\xab\xe6\x8d\xb7\xe9\x94\xae");
            cnMap["Show/Hide overlay"] = QString::fromUtf8("\xe6\x98\xbe\xe7\xa4\xba/\xe9\x9a\x90\xe8\x97\x8f\xe6\x82\xac\xe6\xb5\xae\xe7\xaa\x97");
            cnMap["Start/stop recording"] = QString::fromUtf8("\xe5\xbc\x80\xe5\xa7\x8b/\xe5\x81\x9c\xe6\xad\xa2\xe8\xae\xb0\xe5\xbd\x95");
            cnMap["Show/Hide: %1"] = QString::fromUtf8("\xe6\x98\xbe\xe7\xa4\xba/\xe9\x9a\x90\xe8\x97\x8f: %1");
            cnMap["Record: %1"] = QString::fromUtf8("\xe8\xae\xb0\xe5\xbd\x95: %1");
            cnMap["Start benchmark recording"] = QString::fromUtf8("\xe5\xbc\x80\xe5\xa7\x8b\xe6\xb8\xb8\xe6\x88\x8f\xe8\xae\xb0\xe5\xbd\x95");
            cnMap["Stop benchmark recording"] = QString::fromUtf8("\xe5\x81\x9c\xe6\xad\xa2\xe6\xb8\xb8\xe6\x88\x8f\xe8\xae\xb0\xe5\xbd\x95");
            cnMap["Open recordings folder"] = QString::fromUtf8("\xe6\x89\x93\xe5\xbc\x80\xe8\xae\xb0\xe5\xbd\x95\xe6\x96\x87\xe4\xbb\xb6\xe5\xa4\xb9");
            cnMap["Alert thresholds..."] = QString::fromUtf8("\xe6\x8c\x87\xe6\xa0\x87\xe9\x98\x88\xe5\x80\xbc...");
            cnMap["Alert thresholds"] = QString::fromUtf8("\xe6\x8c\x87\xe6\xa0\x87\xe9\x98\x88\xe5\x80\xbc");
            cnMap["CPU temperature"] = QString::fromUtf8("CPU \xe6\xb8\xa9\xe5\xba\xa6");
            cnMap["GPU temperature"] = QString::fromUtf8("GPU \xe6\xb8\xa9\xe5\xba\xa6");
            cnMap["Memory usage"] = QString::fromUtf8("\xe5\x86\x85\xe5\xad\x98\xe4\xbd\xbf\xe7\x94\xa8\xe7\x8e\x87");
            cnMap["Game VRAM usage"] = QString::fromUtf8("\xe6\xb8\xb8\xe6\x88\x8f\xe6\x98\xbe\xe5\xad\x98\xe4\xbd\xbf\xe7\x94\xa8\xe7\x8e\x87");
            cnMap["Off"] = QString::fromUtf8("\xe5\x85\xb3\xe9\x97\xad");
            cnMap["Hotkey conflict"] = QString::fromUtf8("\xe5\xbf\xab\xe6\x8d\xb7\xe9\x94\xae\xe5\x86\xb2\xe7\xaa\x81");
            cnMap["Choose two different shortcuts with a modifier key."] = QString::fromUtf8("\xe8\xaf\xb7\xe9\x80\x89\xe6\x8b\xa9\xe4\xb8\xa4\xe4\xb8\xaa\xe4\xb8\x8d\xe5\x90\x8c\xe4\xb8\x94\xe5\x90\xab\xe6\x9c\x89\xe4\xbf\xae\xe9\xa5\xb0\xe9\x94\xae\xe7\x9a\x84\xe5\xbf\xab\xe6\x8d\xb7\xe9\x94\xae\xe3\x80");
            cnMap["That shortcut is already in use or unsupported; existing shortcuts were kept."] = QString::fromUtf8("\xe8\xaf\xa5\xe5\xbf\xab\xe6\x8d\xb7\xe9\x94\xae\xe5\xb7\xb2\xe8\xa2\xab\xe5\x8d\xa0\xe7\x94\xa8\xe6\x88\x96\xe4\xb8\x8d\xe5\x8f\x97\xe6\x94\xaf\xe6\x8c\x81\xef\xbc\x8c\xe5\xb7\xb2\xe4\xbf\x9d\xe7\x95\x99\xe5\x8e\x9f\xe5\xbf\xab\xe6\x8d\xb7\xe9\x94\xae\xe3\x80");
            cnMap["Status: Hotkey registration failed"] = QString::fromUtf8("\xe7\x8a\xb6\xe6\x80\x81: \xe5\xbf\xab\xe6\x8d\xb7\xe9\x94\xae\xe6\xb3\xa8\xe5\x86\x8c\xe5\xa4\xb1\xe8\xb4\xa5");
            cnMap["Status: Recording hotkey registration failed"] = QString::fromUtf8("\xe7\x8a\xb6\xe6\x80\x81: \xe8\xae\xb0\xe5\xbd\x95\xe5\xbf\xab\xe6\x8d\xb7\xe9\x94\xae\xe6\xb3\xa8\xe5\x86\x8c\xe5\xa4\xb1\xe8\xb4\xa5");
            cnMap["Recording unavailable"] = QString::fromUtf8("\xe6\x97\xa0\xe6\xb3\x95\xe5\xbd\x95\xe5\x88\xb6");
            cnMap["Recording saved"] = QString::fromUtf8("\xe8\xae\xb0\xe5\xbd\x95\xe5\xb7\xb2\xe4\xbf\x9d\xe5\xad\x98");
            cnMap["Recording game frames"] = QString::fromUtf8("\xe6\xad\xa3\xe5\x9c\xa8\xe8\xae\xb0\xe5\xbd\x95\xe6\xb8\xb8\xe6\x88\x8f\xe5\xb8\xa7\xe6\x95\xb0\xe6\x8d\xae");
            cnMap["Show Overlay"] = QString::fromUtf8("\xe6\x98\xbe\xe7\xa4\xba\xe6\x82\xac\xe6\xb5\xae\xe7\xaa\x97");
            cnMap["Hide Overlay"] = QString::fromUtf8("\xe9\x9a\x90\xe8\x97\x8f\xe6\x82\xac\xe6\xb5\xae\xe7\xaa\x97");
            cnMap["Status: Running"] = QString::fromUtf8("\xe7\x8a\xb6\xe6\x80\x81: \xe8\xbf\x90\xe8\xa1\x8c\xe4\xb8\xad");
            cnMap["Exit"] = QString::fromUtf8("\xe9\x80\x80\xe5\x87\xba");
            cnMap["Show/Hide"] = QString::fromUtf8("\xe6\x98\xbe\xe7\xa4\xba/\xe9\x9a\x90\xe8\x97\x8f");
            cnMap["Confirm Exit"] = QString::fromUtf8("\xe7\xa1\xae\xe8\xae\xa4\xe9\x80\x80\xe5\x87\xba");
            cnMap["Are you sure you want to exit?"] = QString::fromUtf8("\xe7\xa1\xae\xe5\xae\x9a\xe8\xa6\x81\xe9\x80\x80\xe5\x87\xba\xe5\x90\x97\xef\xbc\x9f");
            cnMap["HotkeyMsg"] = QString::fromUtf8("\xe6\x9b\xb4\xe6\x94\xb9\xe5\xbf\xab\xe6\x8d\xb7\xe9\x94\xae\xe9\x9c\x80\xe8\xa6\x81\xe9\x87\x8d\xe5\x90\xaf\xe7\xa8\x8b\xe5\xba\x8f\xe3\x80\x82\n\n\xe5\xbd\x93\xe5\x89\x8d: Alt + F12\n\n\xe6\xb3\xa8\xe6\x84\x8f\xef\xbc\x9a\xe5\xa6\x82\xe6\x9e\x9c\xe5\xbf\xab\xe6\x8d\xb7\xe9\x94\xae\xe8\xa2\xab\xe5\x85\xb6\xe4\xbb\x96\xe7\xa8\x8b\xe5\xba\x8f\xe5\x8d\xa0\xe7\x94\xa8\xef\xbc\x8c\xe5\x8f\xaf\xe8\x83\xbd\xe4\xbc\x9a\xe5\xa4\xb1\xe8\xb4\xa5\xe3\x80\x82");
        }
        if (cnMap.contains(key)) {
            return cnMap[key];
        }
    }
    return key;
}
