#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QGroupBox>
#include <QCheckBox>
#include <QSlider>
#include <QSpinBox>
#include <QComboBox>
#include <QKeySequence>
#include <QMap>
#include <QSet>
#include <QSettings>
#include <QListWidget>
#include <QLineEdit>
#include "gamelibrary.h"

class SystemMonitor;
class Overlay;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(SystemMonitor *monitor, Overlay *overlay, QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, long *result) override;

private:
    void initUI();
    void initTrayIcon();
    void initHotkey();
    void loadSettings();
    void saveSettings();

    SystemMonitor *m_monitor;
    Overlay *m_overlay;
    QSystemTrayIcon *m_trayIcon;

    QWidget *m_centralWidget;
    QVBoxLayout *m_mainLayout;

    QGroupBox *m_displayGroup;
    QCheckBox *m_checkCpu;
    QCheckBox *m_checkGpu;
    QCheckBox *m_checkMemory;
    QCheckBox *m_checkVideoMemory;
    QCheckBox *m_checkCpuTemp;
    QCheckBox *m_checkGpuTemp;
    QCheckBox *m_checkPing;
    QCheckBox *m_checkNetworkInfo;
    QCheckBox *m_checkDeviceInfo;
    QCheckBox *m_checkStorageInfo;
    QCheckBox *m_checkNetworkTraffic;
    QCheckBox *m_checkFrameMetrics;
    QCheckBox *m_checkMousePassThrough;
    QComboBox *m_presetCombo;
    QLabel *m_presetLabel;
    QPushButton *m_restoreDefaultsButton;
    QAction *m_moveOverlayAction;
    QAction *m_trayShowHideAction;
    QAction *m_trayExitAction;

    QGroupBox *m_opacityGroup;
    QSlider *m_opacitySlider;
    QLabel *m_opacityLabel;

    QGroupBox *m_hotkeyGroup;
    QLabel *m_hotkeyLabel;
    QPushButton *m_hotkeyButton;
    QLabel *m_recordHotkeyLabel;
    QPushButton *m_recordButton;
    QPushButton *m_openCapturesButton;
    QPushButton *m_copyReportButton;
    QPushButton *m_alertsButton;
    QGroupBox *m_gamesGroup;
    QLineEdit *m_gameSearchEdit;
    QListWidget *m_gameList;
    QPushButton *m_refreshGamesButton;
    QPushButton *m_launchGameButton;
    QLabel *m_gameLibraryStatus;

    QPushButton *m_btnShowOverlay;
    QPushButton *m_btnExit;
    QLabel *m_statusLabel;

    bool m_configuringHotkey;
    int m_currentLang;
    QKeySequence m_toggleHotkey;
    QKeySequence m_recordHotkey;
    bool m_recording;
    bool m_recordingRequestPending;
    GameLibraryScanner *m_gameScanner;
    bool m_loadingGameProfile;
    QString m_activeGameExecutable;
    uint m_activeProcessId;
    QMap<QString, qint64> m_alertLastShown;
    QSet<QString> m_activeAlerts;

signals:
    void signalShowHide();

private slots:
    void onToggleOverlay();
    void onShowHideTriggered();
    void onExitTriggered();
    void onHotkeyPressed();
    void onConfigureHotkey();
    void onOpacityChanged(int value);
    void onDisplayOptionChanged();
    void onPresetChanged(int index);
    void onRestoreDefaults();
    void onMoveOverlayToggled(bool moving);
    void onRecordHotkeyPressed();
    void onToggleRecording();
    void onRecordingStateChanged(bool recording, const QString &path, const QString &message);
    void onForegroundProcessChanged(const QString &executable, uint processId);
    void onMonitorUpdated();
    void onOpenCaptures();
    void onCopyReport();
    void onRefreshGames();
    void onGamesScanned();
    void onLaunchGame();
    void onGameSelectionChanged();
    void onGameSearchChanged(const QString &text);
    void onConfigureAlerts();

public:
    QString translate(const QString &key);

private:
    void updateUIText();
    void applyPreset(int preset);
    bool registerHotkey(const QKeySequence &sequence, int id);
    void updateHotkeyLabels();
    void saveGameProfile(QSettings &settings);
    void loadGameProfile(const QString &executable);
    void checkThreshold(const QString &key, const QString &title, double value, double threshold, const QString &unit);
};

#endif
