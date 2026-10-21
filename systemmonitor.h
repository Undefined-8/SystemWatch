#ifndef SYSTEMMONITOR_H
#define SYSTEMMONITOR_H

#include <QObject>
#include <QTimer>
#include <QDebug>
#include <QThread>
#include <QMutex>
#include <QProcess>
#include <QQueue>
#include <QPair>
#include <QVector>
#include <QFile>

#ifdef Q_OS_WIN32
#include <windows.h>
#include <pdh.h>
#include <comdef.h>
#include <wbemidl.h>
#pragma comment(lib, "pdh.lib")
#pragma warning(disable:4091)
#endif

class SystemMonitor : public QObject
{
    Q_OBJECT

public:
    explicit SystemMonitor(QObject *parent = nullptr);
    ~SystemMonitor();

    double getCpuUsage();
    double getGpuUsage() const;
    double getMemoryUsageMB();
    double getMemoryUsagePercent();
    qint64 getTotalMemoryMB();
    double getCpuTemperature();
    double getGpuTemperature();
    int getNetworkLatency() const;
    double getVideoMemoryTotalMB();
    QString getNetworkInfo() const;
    QString getDeviceInfo() const;
    QString getStorageInfo() const;
    double getNetworkDownloadMbps() const;
    double getNetworkUploadMbps() const;
    double getFps() const;
    double getFrameTimeMs() const;
    double getOnePercentLowFps() const;
    QString getFrameCaptureStatus() const;
    double getVideoMemoryUsedMB() const;
    bool isRecording() const;
    QString getRecordingPath() const;

signals:
    void dataUpdated();
    void foregroundProcessChanged(const QString &executable, uint processId);
    void recordingStateChanged(bool recording, const QString &path, const QString &message);

private:
    bool initPdh();
    void refreshGpuCounters();
    double readGpuUsage();
    double readGpuMemoryUsedMB(uint processId);
    bool initWmi();
    void updateData();
    double getCpuTemperatureFromWmi();
    double getGpuTemperatureFromWmi();
    int probeNetworkLatency(const QString &host);
    double getVideoMemoryTotalFromDxgi();
    void updateNetworkRates();
    void updatePresentMonTarget();
    void parsePresentMonOutput();
    void updateFrameMetrics();

#ifdef Q_OS_WIN32
    HQUERY m_cpuQuery;
    HCOUNTER m_cpuCounter;
    HQUERY m_gpuQuery;
    QVector<QPair<QString, HCOUNTER> > m_gpuCounters;
    QVector<QPair<QString, HCOUNTER> > m_vramCounters;
#endif

#ifdef Q_OS_WIN32
    IWbemLocator *m_wmiLocator;
    IWbemServices *m_wmiServices;
    IWbemServices *m_wmiLhmServices;
#endif

    double m_cpuUsage;
    double m_gpuUsage;
    qint64 m_gpuLastRefreshMs;
    double m_memoryUsageMB;
    double m_videoMemoryMB;
    double m_videoMemoryUsedMB;
    double m_cpuTemp;
    double m_gpuTemp;
    int m_networkLatency;
    QString m_networkInfo;
    QString m_deviceInfo;
    QString m_storageInfo;
    double m_networkDownloadMbps;
    double m_networkUploadMbps;
    double m_fps;
    double m_frameTimeMs;
    double m_onePercentLowFps;
    QString m_frameCaptureStatus;
    quint64 m_lastNetworkInBytes;
    quint64 m_lastNetworkOutBytes;
    qint64 m_lastNetworkSampleTime;
    QString m_networkBaselineInterfaces;
    bool m_hasNetworkBaseline;
    QProcess *m_presentMonProcess;
    QByteArray m_presentMonBuffer;
    QStringList m_presentMonHeaders;
    QQueue<QPair<qint64, double> > m_frameSamples;
    DWORD m_targetProcessId;
    qint64 m_targetStartedAt;

    QTimer *m_updateTimer;
    bool m_wmiInitialized;
    QThread *m_workerThread;
    mutable QMutex m_dataMutex;
    bool m_comInitialized;
    int m_lhmRetryCounter;
    qint64 m_lastThermalSampleMs;
    uint m_foregroundProcessId;
    QString m_foregroundExecutable;
    bool m_recording;
    QFile *m_recordingFile;
    QString m_recordingPath;
    QString m_recordingSummaryPath;
    QByteArray m_recordingBuffer;
    QVector<double> m_recordedIntervals;
    qint64 m_recordingStartedAt;
    int m_recordingFlushCounter;

private slots:
    void initializeWorker();
    void shutdownWorker();
    void onUpdateTimerTimeout();
    void setRecordingEnabled(bool enabled);

private:
    void finalizeRecording();
    QString capturesDirectory() const;
};

#endif
