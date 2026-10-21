#include "systemmonitor.h"

#ifdef Q_OS_WIN32
#include <iphlpapi.h>
#include <icmpapi.h>
#include <QMutexLocker>
#include <QCoreApplication>
#include <QNetworkInterface>
#include <QNetworkAddressEntry>
#include <QAbstractSocket>
#include <QDateTime>
#include <QFileInfo>
#include <algorithm>
#include <cmath>
#include <functional>
#include <QSysInfo>
#include <QLibrary>
#include <QMap>
#include <QDir>
#include <QStandardPaths>
#include <QTextStream>
#include <QFile>
#include <QFileInfo>
#ifdef Q_OS_WIN32
#include <dxgi.h>
#endif
#pragma comment(lib, "iphlpapi.lib")

// NVAPI is loaded dynamically so the application does not require the NVAPI SDK
// at build time. These V1 definitions match NVIDIA's documented ABI.
typedef void *NvPhysicalGpuHandle;
typedef int (__cdecl *NvApiQueryInterface)(unsigned int);
typedef int (__cdecl *NvApiInitialize)();
typedef int (__cdecl *NvApiEnumPhysicalGpus)(NvPhysicalGpuHandle *, unsigned int *);
struct NvApiThermalSettingsV1 {
    unsigned int version;
    unsigned int count;
    struct Sensor {
        int controller;
        unsigned int defaultMinTemp;
        unsigned int defaultMaxTemp;
        unsigned int currentTemp;
        int target;
    } sensor[3];
};
typedef int (__cdecl *NvApiGetThermalSettings)(NvPhysicalGpuHandle, unsigned int, NvApiThermalSettingsV1 *);

static HRESULT connectWmiNamespace(IWbemLocator *locator, const wchar_t *nameSpace, IWbemServices **services)
{
    if (!locator || !services) return E_POINTER;
    BSTR namespaceBstr = SysAllocString(nameSpace);
    if (!namespaceBstr) return E_OUTOFMEMORY;
    const HRESULT result = locator->ConnectServer(namespaceBstr, NULL, NULL, 0, 0, NULL, NULL, services);
    SysFreeString(namespaceBstr);
    return result;
}

static HRESULT executeWmiQuery(IWbemServices *services, const wchar_t *query,
                               IEnumWbemClassObject **enumerator, long flags)
{
    if (!services || !query || !enumerator) return E_POINTER;
    *enumerator = nullptr;
    BSTR language = SysAllocString(L"WQL");
    BSTR queryBstr = SysAllocString(query);
    if (!language || !queryBstr) {
        if (language) SysFreeString(language);
        if (queryBstr) SysFreeString(queryBstr);
        return E_OUTOFMEMORY;
    }
    const HRESULT result = services->ExecQuery(language, queryBstr, flags, NULL, enumerator);
    SysFreeString(queryBstr);
    SysFreeString(language);
    return result;
}
#endif

SystemMonitor::SystemMonitor(QObject *parent)
    : QObject(parent)
    , m_cpuQuery(nullptr)
    , m_cpuCounter(nullptr)
    , m_gpuQuery(nullptr)
    , m_wmiLocator(nullptr)
    , m_wmiServices(nullptr)
    , m_wmiLhmServices(nullptr)
    , m_cpuUsage(0.0)
    , m_gpuUsage(-1.0)
    , m_gpuLastRefreshMs(0)
    , m_memoryUsageMB(0.0)
    , m_videoMemoryMB(0.0)
    , m_videoMemoryUsedMB(-1.0)
    , m_cpuTemp(-1.0)
    , m_gpuTemp(-1.0)
    , m_networkLatency(-1)
    , m_wmiInitialized(false)
    , m_workerThread(new QThread())
    , m_comInitialized(false)
    , m_lhmRetryCounter(0)
    , m_lastThermalSampleMs(0)
    , m_foregroundProcessId(0)
    , m_recording(false)
    , m_recordingFile(nullptr)
    , m_recordingStartedAt(0)
    , m_recordingFlushCounter(0)
    , m_networkDownloadMbps(0.0)
    , m_networkUploadMbps(0.0)
    , m_fps(0.0)
    , m_frameTimeMs(0.0)
    , m_onePercentLowFps(0.0)
    , m_frameCaptureStatus(QString::fromUtf8("\xe7\xad\x89\xe5\xbe\x85\xe6\xb8\xb8\xe6\x88\x8f\xe7\xaa\x97\xe5\x8f\xa3"))
    , m_lastNetworkInBytes(0)
    , m_lastNetworkOutBytes(0)
    , m_lastNetworkSampleTime(0)
    , m_hasNetworkBaseline(false)
    , m_presentMonProcess(nullptr)
    , m_targetProcessId(0)
    , m_targetStartedAt(0)
{
    m_updateTimer = new QTimer(this);
    connect(m_updateTimer, &QTimer::timeout, this, &SystemMonitor::onUpdateTimerTimeout);
    moveToThread(m_workerThread);
    connect(m_workerThread, &QThread::started, this, &SystemMonitor::initializeWorker);
    m_workerThread->start();
}

SystemMonitor::~SystemMonitor()
{
    if (m_workerThread->isRunning()) {
        QMetaObject::invokeMethod(this, "shutdownWorker", Qt::BlockingQueuedConnection);
        m_workerThread->quit();
        m_workerThread->wait();
        moveToThread(QCoreApplication::instance()->thread());
    }
#ifdef Q_OS_WIN32
    if (m_cpuQuery) {
        PdhCloseQuery(m_cpuQuery);
        m_cpuQuery = nullptr;
    }
    if (m_gpuQuery) {
        PdhCloseQuery(m_gpuQuery);
        m_gpuQuery = nullptr;
        m_gpuCounters.clear();
    }

    if (m_wmiServices) {
        m_wmiServices->Release();
        m_wmiServices = nullptr;
    }
    if (m_wmiLhmServices) {
        m_wmiLhmServices->Release();
        m_wmiLhmServices = nullptr;
    }
    if (m_wmiLocator) {
        m_wmiLocator->Release();
        m_wmiLocator = nullptr;
    }
#endif
    delete m_workerThread;
}

void SystemMonitor::initializeWorker()
{
#ifdef Q_OS_WIN32
    HRESULT hr = CoInitializeEx(0, COINIT_APARTMENTTHREADED);
    m_comInitialized = SUCCEEDED(hr);
#endif
    m_presentMonProcess = new QProcess(this);
    m_presentMonProcess->setProcessChannelMode(QProcess::SeparateChannels);
    m_presentMonProcess->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments) {
        arguments->flags |= CREATE_NO_WINDOW;
    });
    connect(m_presentMonProcess, &QProcess::readyReadStandardOutput, this, &SystemMonitor::parsePresentMonOutput);
    connect(m_presentMonProcess, static_cast<void (QProcess::*)(QProcess::ProcessError)>(&QProcess::error),
            this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            QMutexLocker lock(&m_dataMutex);
            m_frameCaptureStatus = QString::fromUtf8("PresentMon \xe6\x97\xa0\xe6\xb3\x95\xe5\x90\xaf\xe5\x8a\xa8");
        }
    });
    connect(m_presentMonProcess, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this, [this](int, QProcess::ExitStatus) {
        const QByteArray errorText = m_presentMonProcess->readAllStandardError();
        if (m_recording) finalizeRecording();
        if (m_targetProcessId && m_frameSamples.isEmpty() && errorText.toLower().contains("access denied")) {
            QMutexLocker lock(&m_dataMutex);
            m_frameCaptureStatus = QString::fromUtf8("ETW \xe6\x97\xa0\xe6\x9d\x83\xe9\x99\x90\xef\xbc\x9a\xe6\xa3\x80\xe6\x9f\xa5\xe7\xae\xa1\xe7\x90\x86\xe5\x91\x98\xe6\x88\x96\xe7\x94\xa8\xe6\x88\xb7\xe7\xbb\x84\xe6\x9d\x83\xe9\x99\x90");
        }
    });
    initPdh();
    initWmi();
    m_updateTimer->start(1000);
}

void SystemMonitor::shutdownWorker()
{
    m_updateTimer->stop();
    finalizeRecording();
    if (m_presentMonProcess) {
        m_presentMonProcess->kill();
        m_presentMonProcess->waitForFinished(1000);
        delete m_presentMonProcess;
        m_presentMonProcess = nullptr;
    }
#ifdef Q_OS_WIN32
    if (m_cpuQuery) { PdhCloseQuery(m_cpuQuery); m_cpuQuery = nullptr; }
    if (m_gpuQuery) { PdhCloseQuery(m_gpuQuery); m_gpuQuery = nullptr; }
    m_gpuCounters.clear();
    m_vramCounters.clear();
    if (m_wmiServices) { m_wmiServices->Release(); m_wmiServices = nullptr; }
    if (m_wmiLhmServices) { m_wmiLhmServices->Release(); m_wmiLhmServices = nullptr; }
    if (m_wmiLocator) { m_wmiLocator->Release(); m_wmiLocator = nullptr; }
    if (m_comInitialized) { CoUninitialize(); m_comInitialized = false; }
#endif
    moveToThread(QCoreApplication::instance()->thread());
}

bool SystemMonitor::initPdh()
{
#ifdef Q_OS_WIN32
    PDH_STATUS status = PdhOpenQuery(NULL, 0, &m_cpuQuery);
    if (status != ERROR_SUCCESS) {
        return false;
    }

    HCOUNTER cpuCounterTemp = NULL;
    status = PdhAddCounter(m_cpuQuery, L"\\Processor(_Total)\\% Processor Time", 0, &cpuCounterTemp);
    if (status != ERROR_SUCCESS) {
        status = PdhAddCounter(m_cpuQuery, TEXT("\\Processor(0,_Total)\\% Processor Time"), 0, &cpuCounterTemp);
    }
    if (status != ERROR_SUCCESS) {
        return false;
    }
    m_cpuCounter = cpuCounterTemp;

    PdhCollectQueryData(m_cpuQuery);
    refreshGpuCounters();

    return true;
#else
    return false;
#endif
}

void SystemMonitor::refreshGpuCounters()
{
#ifdef Q_OS_WIN32
    if (!m_gpuQuery && PdhOpenQuery(NULL, 0, &m_gpuQuery) != ERROR_SUCCESS) return;
    for (int i = 0; i < m_gpuCounters.size(); ++i)
        if (m_gpuCounters.at(i).second) PdhRemoveCounter(m_gpuCounters.at(i).second);
    for (int i = 0; i < m_vramCounters.size(); ++i)
        if (m_vramCounters.at(i).second) PdhRemoveCounter(m_vramCounters.at(i).second);
    m_gpuCounters.clear();
    m_vramCounters.clear();

    const auto addWildcard = [this](const wchar_t *wildcard, QVector<QPair<QString, HCOUNTER> > *destination) {
        DWORD size = 0;
        PdhExpandWildCardPathW(NULL, wildcard, NULL, &size, 0);
        if (size == 0) return;
        QVector<wchar_t> paths(static_cast<int>(size));
        if (PdhExpandWildCardPathW(NULL, wildcard, paths.data(), &size, 0) != ERROR_SUCCESS) return;
        const wchar_t *path = paths.constData();
        while (*path) {
            const QString counterPath = QString::fromWCharArray(path);
            HCOUNTER counter = NULL;
            if (PdhAddCounterW(m_gpuQuery, path, 0, &counter) == ERROR_SUCCESS)
                destination->append(qMakePair(counterPath, counter));
            path += wcslen(path) + 1;
        }
    };
    addWildcard(L"\\GPU Engine(*)\\Utilization Percentage", &m_gpuCounters);
    addWildcard(L"\\GPU Process Memory(*)\\Dedicated Usage", &m_vramCounters);
    PdhCollectQueryData(m_gpuQuery);
    m_gpuLastRefreshMs = QDateTime::currentMSecsSinceEpoch();
#endif
}
double SystemMonitor::readGpuUsage()
{
#ifdef Q_OS_WIN32
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_gpuCounters.isEmpty() || now - m_gpuLastRefreshMs >= 15000)
        refreshGpuCounters();
    if (!m_gpuQuery || m_gpuCounters.isEmpty()) return -1.0;
    if (PdhCollectQueryData(m_gpuQuery) != ERROR_SUCCESS) return -1.0;

    QMap<QString, double> engineTotals;
    for (int i = 0; i < m_gpuCounters.size(); ++i) {
        const QString path = m_gpuCounters.at(i).first;
        const int engineTypeIndex = path.indexOf("_engtype_", 0, Qt::CaseInsensitive);
        const int engineIndex = path.indexOf("_eng_", 0, Qt::CaseInsensitive);
        const int luidIndex = path.indexOf("_luid_", 0, Qt::CaseInsensitive);
        if (engineTypeIndex < 0 || engineIndex < 0 || luidIndex < 0) continue;

        PDH_FMT_COUNTERVALUE value;
        DWORD type = 0;
        if (PdhGetFormattedCounterValue(m_gpuCounters.at(i).second, PDH_FMT_DOUBLE, &type, &value) != ERROR_SUCCESS ||
            value.CStatus != ERROR_SUCCESS || value.doubleValue < 0.0) continue;
        const QString adapter = path.mid(luidIndex, engineIndex - luidIndex);
        const QString engineType = path.mid(engineTypeIndex + 9).section('\\', 0, 0);
        engineTotals[adapter + QLatin1Char('|') + engineType] += value.doubleValue;
    }

    QMap<QString, double> adapterTotals;
    for (QMap<QString, double>::const_iterator it = engineTotals.constBegin(); it != engineTotals.constEnd(); ++it) {
        const int separator = it.key().lastIndexOf(QLatin1Char('|'));
        if (separator > 0) adapterTotals[it.key().left(separator)] += it.value();
    }
    double maximum = -1.0;
    for (QMap<QString, double>::const_iterator it = adapterTotals.constBegin(); it != adapterTotals.constEnd(); ++it)
        maximum = qMax(maximum, qMin(100.0, it.value()));
    return maximum;
#else
    return -1.0;
#endif
}

double SystemMonitor::readGpuMemoryUsedMB(uint processId)
{
#ifdef Q_OS_WIN32
    if (!processId || m_vramCounters.isEmpty()) return -1.0;
    double bytes = 0.0;
    bool found = false;
    for (int i = 0; i < m_vramCounters.size(); ++i) {
        const QString path = m_vramCounters.at(i).first;
        const int pidIndex = path.indexOf("pid_", 0, Qt::CaseInsensitive);
        if (pidIndex < 0) continue;
        bool ok = false;
        const uint counterPid = path.mid(pidIndex + 4).section('_', 0, 0).toUInt(&ok);
        if (!ok || counterPid != processId) continue;
        PDH_FMT_COUNTERVALUE value;
        DWORD type = 0;
        if (PdhGetFormattedCounterValue(m_vramCounters.at(i).second, PDH_FMT_LARGE, &type, &value) == ERROR_SUCCESS &&
            value.CStatus == ERROR_SUCCESS && value.largeValue >= 0) {
            bytes += static_cast<double>(value.largeValue);
            found = true;
        }
    }
    return found ? bytes / (1024.0 * 1024.0) : -1.0;
#else
    Q_UNUSED(processId);
    return -1.0;
#endif
}

bool SystemMonitor::initWmi()
{
#ifdef Q_OS_WIN32
    HRESULT hr = CoCreateInstance(
        CLSID_WbemLocator,
        0,
        CLSCTX_INPROC_SERVER,
        IID_IWbemLocator,
        (LPVOID *)&m_wmiLocator
    );

    if (FAILED(hr) || !m_wmiLocator) {
        return false;
    }

    hr = connectWmiNamespace(m_wmiLocator, L"ROOT\\CIMV2", &m_wmiServices);

    if (FAILED(hr) || !m_wmiServices) {
        return false;
    }

    hr = CoSetProxyBlanket(
        (IUnknown *)m_wmiServices,
        RPC_C_AUTHN_WINNT,
        RPC_C_AUTHZ_NONE,
        NULL,
        RPC_C_AUTHN_LEVEL_CALL,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        NULL,
        EOAC_NONE
    );

    if (FAILED(hr)) {
        m_wmiServices->Release();
        m_wmiServices = nullptr;
        m_wmiLocator->Release();
        m_wmiLocator = nullptr;
        return false;
    }

    // LibreHardwareMonitor publishes CPU DTS readings through this namespace
    // when its sensor service is running. It remains optional; no driver is
    // installed or started by this application.
    hr = connectWmiNamespace(m_wmiLocator, L"ROOT\\LibreHardwareMonitor", &m_wmiLhmServices);
    if (SUCCEEDED(hr) && m_wmiLhmServices) {
        hr = CoSetProxyBlanket((IUnknown *)m_wmiLhmServices, RPC_C_AUTHN_WINNT,
                              RPC_C_AUTHZ_NONE, NULL, RPC_C_AUTHN_LEVEL_CALL,
                              RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
        if (FAILED(hr)) {
            m_wmiLhmServices->Release();
            m_wmiLhmServices = nullptr;
        }
    }

    m_wmiInitialized = true;
    return true;
#else
    return false;
#endif
}

double SystemMonitor::getCpuUsage()
{
    QMutexLocker lock(&m_dataMutex);
    return m_cpuUsage;
}

double SystemMonitor::getGpuUsage() const
{
    QMutexLocker lock(&m_dataMutex);
    return m_gpuUsage;
}

double SystemMonitor::getMemoryUsageMB()
{
    QMutexLocker lock(&m_dataMutex);
    return m_memoryUsageMB;
}

double SystemMonitor::getMemoryUsagePercent()
{
    QMutexLocker lock(&m_dataMutex);
    qint64 total = getTotalMemoryMB();
    if (total <= 0) return 0.0;
    return (m_memoryUsageMB / total) * 100.0;
}

qint64 SystemMonitor::getTotalMemoryMB()
{
#ifdef Q_OS_WIN32
    MEMORYSTATUSEX memStatus;
    memStatus.dwLength = sizeof(memStatus);
    if (GlobalMemoryStatusEx(&memStatus)) {
        return memStatus.ullTotalPhys / (1024 * 1024);
    }
#endif
    return 0;
}

void SystemMonitor::onUpdateTimerTimeout()
{
    updateData();
    emit dataUpdated();
}

double SystemMonitor::getVideoMemoryTotalMB()
{
    QMutexLocker lock(&m_dataMutex);
    return m_videoMemoryMB;
}

QString SystemMonitor::getNetworkInfo() const
{
    QMutexLocker lock(&m_dataMutex);
    return m_networkInfo;
}

QString SystemMonitor::getDeviceInfo() const
{
    QMutexLocker lock(&m_dataMutex);
    return m_deviceInfo;
}

QString SystemMonitor::getStorageInfo() const
{
    QMutexLocker lock(&m_dataMutex);
    return m_storageInfo;
}

double SystemMonitor::getNetworkDownloadMbps() const { QMutexLocker lock(&m_dataMutex); return m_networkDownloadMbps; }
double SystemMonitor::getNetworkUploadMbps() const { QMutexLocker lock(&m_dataMutex); return m_networkUploadMbps; }
double SystemMonitor::getFps() const { QMutexLocker lock(&m_dataMutex); return m_fps; }
double SystemMonitor::getFrameTimeMs() const { QMutexLocker lock(&m_dataMutex); return m_frameTimeMs; }
double SystemMonitor::getOnePercentLowFps() const { QMutexLocker lock(&m_dataMutex); return m_onePercentLowFps; }
QString SystemMonitor::getFrameCaptureStatus() const { QMutexLocker lock(&m_dataMutex); return m_frameCaptureStatus; }
double SystemMonitor::getVideoMemoryUsedMB() const { QMutexLocker lock(&m_dataMutex); return m_videoMemoryUsedMB; }
bool SystemMonitor::isRecording() const { QMutexLocker lock(&m_dataMutex); return m_recording; }
QString SystemMonitor::getRecordingPath() const { QMutexLocker lock(&m_dataMutex); return m_recordingPath; }

QString SystemMonitor::capturesDirectory() const
{
    const QString localAppData = QString::fromLocal8Bit(qgetenv("LOCALAPPDATA"));
    if (!localAppData.isEmpty()) return QDir(localAppData).filePath("SystemWatch/captures");
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath("captures");
}

void SystemMonitor::setRecordingEnabled(bool enabled)
{
    if (!enabled) {
        finalizeRecording();
        return;
    }
    if (m_recording) return;
    if (!m_targetProcessId || m_foregroundExecutable.isEmpty()) {
        emit recordingStateChanged(false, QString(), QString::fromUtf8("\xe8\xaf\xb7\xe5\x85\x88\xe6\xbf\x80\xe6\xb4\xbb\xe6\xb8\xb8\xe6\x88\x8f\xe7\xaa\x97\xe5\x8f\xa3"));
        return;
    }

    const QString folder = capturesDirectory();
    if (!QDir().mkpath(folder)) {
        emit recordingStateChanged(false, QString(), QString::fromUtf8("\xe6\x97\xa0\xe6\xb3\x95\xe5\x88\x9b\xe5\xbb\xba\xe8\xae\xb0\xe5\xbd\x95\xe7\x9b\xae\xe5\xbd\x95"));
        return;
    }
    const QString baseName = QFileInfo(m_foregroundExecutable).completeBaseName() + "_" +
        QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz");
    const QString recordingPath = QDir(folder).filePath(baseName + ".csv");
    const QString summaryPath = QDir(folder).filePath(baseName + "_summary.csv");
    m_recordingFile = new QFile(recordingPath);
    if (!m_recordingFile->open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        delete m_recordingFile;
        m_recordingFile = nullptr;
        emit recordingStateChanged(false, QString(), QString::fromUtf8("\xe6\x97\xa0\xe6\xb3\x95\xe5\x86\x99\xe5\x85\xa5 CSV \xe8\xae\xb0\xe5\xbd\x95"));
        return;
    }
    m_recordedIntervals.clear();
    m_recordingBuffer.clear();
    m_recordingStartedAt = QDateTime::currentMSecsSinceEpoch();
    m_recordingFlushCounter = 0;
    m_recordingFile->write("timestamp,pid,process,frame_time_ms,fps\r\n");
    {
        QMutexLocker lock(&m_dataMutex);
        m_recording = true;
        m_recordingPath = recordingPath;
    }
    m_recordingSummaryPath = summaryPath;
    emit recordingStateChanged(true, recordingPath, QString());
}

void SystemMonitor::finalizeRecording()
{
    if (!m_recording) return;
    if (m_recordingFile) {
        if (!m_recordingBuffer.isEmpty()) m_recordingFile->write(m_recordingBuffer);
        m_recordingFile->flush();
        m_recordingFile->close();
        delete m_recordingFile;
        m_recordingFile = nullptr;
    }

    QVector<double> sorted = m_recordedIntervals;
    std::sort(sorted.begin(), sorted.end(), std::greater<double>());
    double sum = 0.0;
    double slowOnePercentSum = 0.0;
    double slowPointOnePercentSum = 0.0;
    for (int i = 0; i < sorted.size(); ++i) sum += sorted.at(i);
    const int onePercentCount = sorted.isEmpty() ? 0 : qMax(1, static_cast<int>(std::ceil(sorted.size() * 0.01)));
    const int pointOnePercentCount = sorted.size() < 1000 ? 0 : qMax(1, static_cast<int>(std::ceil(sorted.size() * 0.001)));
    for (int i = 0; i < onePercentCount; ++i) slowOnePercentSum += sorted.at(i);
    for (int i = 0; i < pointOnePercentCount; ++i) slowPointOnePercentSum += sorted.at(i);
    const qint64 durationMs = qMax<qint64>(1, QDateTime::currentMSecsSinceEpoch() - m_recordingStartedAt);
    const double averageFps = sorted.isEmpty() ? 0.0 : sorted.size() * 1000.0 / sum;
    const double minimumFps = sorted.isEmpty() ? 0.0 : 1000.0 / sorted.first();
    const double maximumFps = sorted.isEmpty() ? 0.0 : 1000.0 / sorted.last();
    const double onePercentLow = onePercentCount ? 1000.0 / (slowOnePercentSum / onePercentCount) : 0.0;
    const double pointOnePercentLow = pointOnePercentCount ? 1000.0 / (slowPointOnePercentSum / pointOnePercentCount) : 0.0;
    QFile summary(m_recordingSummaryPath);
    if (summary.open(QIODevice::WriteOnly | QIODevice::Text)) {
        const QString processName = QFileInfo(m_foregroundExecutable).fileName().replace('"', "\"\"");
        const QString text = QString::fromLatin1("metric,value,unit\r\n") +
            QString::fromLatin1("process,\"%1\",\r\n").arg(processName) +
            QString::fromLatin1("duration,%1,seconds\r\n").arg(durationMs / 1000.0, 0, 'f', 1) +
            QString::fromLatin1("frames,%1,count\r\n").arg(sorted.size()) +
            QString::fromLatin1("average_fps,%1,fps\r\n").arg(averageFps, 0, 'f', 2) +
            QString::fromLatin1("minimum_fps,%1,fps\r\n").arg(minimumFps, 0, 'f', 2) +
            QString::fromLatin1("maximum_fps,%1,fps\r\n").arg(maximumFps, 0, 'f', 2) +
            QString::fromLatin1("one_percent_low,%1,fps\r\n").arg(onePercentLow, 0, 'f', 2) +
            QString::fromLatin1("point_one_percent_low,%1,fps\r\n").arg(pointOnePercentLow, 0, 'f', 2) +
            QString::fromLatin1("average_frame_time,%1,ms\r\n").arg(sorted.isEmpty() ? 0.0 : sum / sorted.size(), 0, 'f', 3);
        summary.write(text.toUtf8());
        summary.close();
    }
    m_recordedIntervals.clear();
    {
        QMutexLocker lock(&m_dataMutex);
        m_recording = false;
    }
    emit recordingStateChanged(false, getRecordingPath(), QString());
}

void SystemMonitor::updateNetworkRates()
{
#ifdef Q_OS_WIN32
    ULONG tableSize = 0;
    GetIfTable(nullptr, &tableSize, TRUE);
    if (!tableSize) return;
    QByteArray tableBuffer;
    tableBuffer.resize(static_cast<int>(tableSize));
    MIB_IFTABLE *table = reinterpret_cast<MIB_IFTABLE *>(tableBuffer.data());
    if (GetIfTable(table, &tableSize, TRUE) != NO_ERROR) return;
    quint64 inBytes = 0;
    quint64 outBytes = 0;
    QStringList activeInterfaces;
    for (ULONG i = 0; i < table->dwNumEntries; ++i) {
        const MIB_IFROW &row = table->table[i];
        if ((row.dwType != MIB_IF_TYPE_ETHERNET && row.dwType != 71) || row.dwPhysAddrLen == 0 ||
            row.dwAdminStatus != 1 || row.dwOperStatus != 1) continue;
        inBytes += row.dwInOctets;
        outBytes += row.dwOutOctets;
        activeInterfaces.append(QString::number(row.dwIndex));
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 elapsed = now - m_lastNetworkSampleTime;
    const QString interfaceKey = activeInterfaces.join(",");
    QMutexLocker lock(&m_dataMutex);
    if (m_hasNetworkBaseline && interfaceKey == m_networkBaselineInterfaces && elapsed > 0 &&
        inBytes >= m_lastNetworkInBytes && outBytes >= m_lastNetworkOutBytes) {
        const double seconds = elapsed / 1000.0;
        m_networkDownloadMbps = (inBytes - m_lastNetworkInBytes) * 8.0 / seconds / 1000000.0;
        m_networkUploadMbps = (outBytes - m_lastNetworkOutBytes) * 8.0 / seconds / 1000000.0;
    } else {
        m_networkDownloadMbps = 0.0;
        m_networkUploadMbps = 0.0;
    }
    m_lastNetworkInBytes = inBytes;
    m_lastNetworkOutBytes = outBytes;
    m_networkBaselineInterfaces = interfaceKey;
    m_hasNetworkBaseline = true;
    m_lastNetworkSampleTime = now;
#endif
}

void SystemMonitor::updatePresentMonTarget()
{
#ifdef Q_OS_WIN32
    HWND foreground = GetForegroundWindow();
    DWORD processId = 0;
    if (foreground) GetWindowThreadProcessId(foreground, &processId);
    if (processId == GetCurrentProcessId()) processId = 0;
    if (processId == m_targetProcessId) return;
    if (m_recording) finalizeRecording();
    m_targetProcessId = processId;
    m_targetStartedAt = QDateTime::currentMSecsSinceEpoch();
    m_foregroundProcessId = processId;
    m_foregroundExecutable.clear();
    if (processId) {
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
        if (process) {
            wchar_t imagePath[32768];
            DWORD length = sizeof(imagePath) / sizeof(imagePath[0]);
            if (QueryFullProcessImageNameW(process, 0, imagePath, &length))
                m_foregroundExecutable = QFileInfo(QString::fromWCharArray(imagePath)).fileName().toLower();
            CloseHandle(process);
        }
    }
    emit foregroundProcessChanged(m_foregroundExecutable, processId);
    m_frameSamples.clear();
    {
        QMutexLocker lock(&m_dataMutex);
        m_fps = 0.0;
        m_frameTimeMs = 0.0;
        m_onePercentLowFps = 0.0;
        m_frameCaptureStatus = processId ? QString::fromUtf8("\xe6\xad\xa3\xe5\x9c\xa8\xe8\xbf\x9e\xe6\x8e\xa5\xe5\xb8\xa7\xe9\x87\x87\xe9\x9b\x86\xe5\x99\xa8") : QString::fromUtf8("\xe7\xad\x89\xe5\xbe\x85\xe6\xb8\xb8\xe6\x88\x8f\xe7\xaa\x97\xe5\x8f\xa3");
    }
    if (!m_presentMonProcess) return;
    if (m_presentMonProcess->state() != QProcess::NotRunning) {
        m_targetProcessId = 0;
        m_presentMonProcess->kill();
        m_presentMonProcess->waitForFinished(500);
        m_targetProcessId = processId;
    }
    m_presentMonBuffer.clear();
    m_presentMonHeaders.clear();
    if (!processId) return;
#if defined(_WIN64)
    // The application target architecture may be x86; a 64-bit Windows host can still run the helper.
#else
    SYSTEM_INFO systemInfo;
    GetNativeSystemInfo(&systemInfo);
    if (systemInfo.wProcessorArchitecture != PROCESSOR_ARCHITECTURE_AMD64) {
        QMutexLocker lock(&m_dataMutex);
        m_frameCaptureStatus = QString::fromUtf8("PresentMon \xe4\xbb\x85\xe6\x94\xaf\xe6\x8c\x81 64 \xe4\xbd\x8d Windows");
        return;
    }
#endif
    const QString executable = QCoreApplication::applicationDirPath() + "/tools/PresentMon-2.6.0-x64.exe";
    if (!QFileInfo::exists(executable)) {
        QMutexLocker lock(&m_dataMutex);
        m_frameCaptureStatus = QString::fromUtf8("PresentMon \xe6\x96\x87\xe4\xbb\xb6\xe7\xbc\xba\xe5\xa4\xb1");
        return;
    }
    m_presentMonProcess->start(executable, QStringList() << "--process_id" << QString::number(processId)
                               << "--output_stdout" << "--no_csv" << "--qpc_time" << "--no_console_stats");
#endif
}

void SystemMonitor::parsePresentMonOutput()
{
    if (!m_presentMonProcess) return;
    m_presentMonBuffer.append(m_presentMonProcess->readAllStandardOutput());
    int newline = -1;
    while ((newline = m_presentMonBuffer.indexOf('\n')) >= 0) {
        QByteArray line = m_presentMonBuffer.left(newline).trimmed();
        m_presentMonBuffer.remove(0, newline + 1);
        if (line.isEmpty()) continue;
        const QStringList fields = QString::fromLocal8Bit(line).split(',');
        if (m_presentMonHeaders.isEmpty()) {
            m_presentMonHeaders = fields;
            for (QString &header : m_presentMonHeaders) header = header.trimmed();
            continue;
        }
        const int intervalIndex = m_presentMonHeaders.indexOf("MsBetweenPresents");
        const int pidIndex = m_presentMonHeaders.indexOf("ProcessID");
        if (intervalIndex < 0 || pidIndex < 0 || fields.size() <= qMax(intervalIndex, pidIndex)) continue;
        bool okPid = false;
        bool okInterval = false;
        const uint pid = fields.at(pidIndex).trimmed().toUInt(&okPid);
        const double interval = fields.at(intervalIndex).trimmed().toDouble(&okInterval);
        if (!okPid || !okInterval || pid != m_targetProcessId || interval <= 0.0 || interval > 1000.0) continue;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        m_frameSamples.enqueue(qMakePair(now, interval));
        if (m_recording && m_recordingFile && m_recordingFile->isOpen()) {
            m_recordedIntervals.append(interval);
            const QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
            m_recordingBuffer.append(QString("%1,%2,\"%3\",%4,%5\r\n")
                .arg(timestamp).arg(pid).arg(QFileInfo(m_foregroundExecutable).fileName().replace('"', "\"\""))
                .arg(interval, 0, 'f', 4).arg(1000.0 / interval, 0, 'f', 2).toUtf8());
            if (++m_recordingFlushCounter >= 256) {
                m_recordingFile->write(m_recordingBuffer);
                m_recordingBuffer.clear();
                m_recordingFlushCounter = 0;
            }
        }
        while (!m_frameSamples.isEmpty() && now - m_frameSamples.head().first > 60000) m_frameSamples.dequeue();
        updateFrameMetrics();
    }
    const QByteArray errorText = m_presentMonProcess->readAllStandardError();
    if (!errorText.isEmpty() && m_frameSamples.isEmpty()) {
        QMutexLocker lock(&m_dataMutex);
        m_frameCaptureStatus = QString::fromUtf8("ETW \xe4\xb8\x8d\xe5\x8f\xaf\xe7\x94\xa8\xef\xbc\x9a\xe6\xa3\x80\xe6\x9f\xa5\xe6\x9d\x83\xe9\x99\x90");
    }
}

void SystemMonitor::updateFrameMetrics()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    int recentFrames = 0;
    double recentFrameTime = 0.0;
    QList<double> intervals;
    for (int i = 0; i < m_frameSamples.size(); ++i) {
        const QPair<qint64, double> sample = m_frameSamples.at(i);
        intervals.append(sample.second);
        if (now - sample.first <= 1000) { ++recentFrames; recentFrameTime += sample.second; }
    }
    double onePercentLow = 0.0;
    if (intervals.size() >= 100) {
        std::sort(intervals.begin(), intervals.end(), std::greater<double>());
        const int slowCount = qMax(1, static_cast<int>(std::ceil(intervals.size() * 0.01)));
        double slowAverage = 0.0;
        for (int i = 0; i < slowCount; ++i) slowAverage += intervals.at(i);
        onePercentLow = 1000.0 / (slowAverage / slowCount);
    }
    QMutexLocker lock(&m_dataMutex);
    m_fps = recentFrames;
    m_frameTimeMs = recentFrames ? recentFrameTime / recentFrames : 0.0;
    m_onePercentLowFps = onePercentLow;
    m_frameCaptureStatus = QString::fromUtf8("\xe9\x87\x87\xe9\x9b\x86\xe4\xb8\xad");
}


double SystemMonitor::getCpuTemperature()
{
    QMutexLocker lock(&m_dataMutex);
    return m_cpuTemp;
}

double SystemMonitor::getCpuTemperatureFromWmi()
{
#ifdef Q_OS_WIN32
    if (!m_wmiServices) return -1.0;

    // The user may start the external sensor provider after this application.
    // Retry namespace discovery every five samples instead of requiring a restart.
    if (!m_wmiLhmServices && m_wmiLocator && (++m_lhmRetryCounter % 5 == 0)) {
        HRESULT connectHr = connectWmiNamespace(m_wmiLocator, L"ROOT\\LibreHardwareMonitor", &m_wmiLhmServices);
        if (SUCCEEDED(connectHr) && m_wmiLhmServices) {
            connectHr = CoSetProxyBlanket((IUnknown *)m_wmiLhmServices, RPC_C_AUTHN_WINNT,
                RPC_C_AUTHZ_NONE, NULL, RPC_C_AUTHN_LEVEL_CALL,
                RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
            if (FAILED(connectHr)) {
                m_wmiLhmServices->Release();
                m_wmiLhmServices = nullptr;
            }
        }
    }

    if (m_wmiLhmServices) {
        IEnumWbemClassObject *sensorEnum = nullptr;
        HRESULT sensorHr = executeWmiQuery(m_wmiLhmServices,
            L"SELECT Name,Value,Parent FROM Sensor WHERE SensorType='Temperature'",
            &sensorEnum, WBEM_FLAG_RETURN_IMMEDIATELY | WBEM_FLAG_FORWARD_ONLY);
        if (SUCCEEDED(sensorHr) && sensorEnum) {
            double packageTemp = -1.0;
            double coreMaxTemp = -1.0;
            for (int item = 0; item < 16; ++item) {
                IWbemClassObject *sensor = nullptr;
                sensorHr = sensorEnum->Next(100, 1, &sensor, NULL);
                if (sensorHr != S_OK || !sensor) break;
                VARIANT nameValue, parentValue, tempValue, numericValue;
                VariantInit(&nameValue); VariantInit(&parentValue);
                VariantInit(&tempValue); VariantInit(&numericValue);
                sensor->Get(L"Name", 0, &nameValue, NULL, NULL);
                sensor->Get(L"Parent", 0, &parentValue, NULL, NULL);
                sensor->Get(L"Value", 0, &tempValue, NULL, NULL);
                const QString name = nameValue.vt == VT_BSTR ? QString::fromWCharArray(nameValue.bstrVal) : QString();
                const QString parent = parentValue.vt == VT_BSTR ? QString::fromWCharArray(parentValue.bstrVal) : QString();
                if (parent.contains("intelcpu", Qt::CaseInsensitive) &&
                    SUCCEEDED(VariantChangeType(&numericValue, &tempValue, 0, VT_R8))) {
                    const double temperature = numericValue.dblVal;
                    if (temperature > 0.0 && temperature < 150.0) {
                        if (name.contains("package", Qt::CaseInsensitive))
                            packageTemp = temperature;
                        else if (name.contains("core", Qt::CaseInsensitive) || name.contains("max", Qt::CaseInsensitive))
                            coreMaxTemp = qMax(coreMaxTemp, temperature);
                    }
                }
                VariantClear(&nameValue); VariantClear(&parentValue);
                VariantClear(&tempValue); VariantClear(&numericValue);
                sensor->Release();
            }
            sensorEnum->Release();
            if (packageTemp > 0.0) return packageTemp;
            if (coreMaxTemp > 0.0) return coreMaxTemp;
        }
    }

    // Generic ACPI and motherboard temperature classes do not identify a CPU
    // sensor reliably. Keep this metric unavailable unless Intel DTS sensors
    // are explicitly identified by LibreHardwareMonitor above.
#endif
    return -1.0;
}

double SystemMonitor::getGpuTemperature()
{
    QMutexLocker lock(&m_dataMutex);
    return m_gpuTemp;
}

double SystemMonitor::getGpuTemperatureFromWmi()
{
#ifdef Q_OS_WIN32
    if (!m_wmiServices) return -1;

    const wchar_t *queries[] = {
        L"SELECT * FROM Win32_PerfFormattedData_Counters_GPUTemperature",
        L"SELECT * FROM Win32_VideoController"
    };

    const wchar_t *propNames[] = {L"Temperature", L"CurrentTemperature", L"GPUCurrentTemperature"};

    for (int i = 0; i < 2; i++) {
        IEnumWbemClassObject *pEnum = nullptr;
        HRESULT hr = executeWmiQuery(m_wmiServices, queries[i], &pEnum, WBEM_FLAG_RETURN_IMMEDIATELY);

        if (SUCCEEDED(hr) && pEnum) {
            IWbemClassObject *pObj = nullptr;
            hr = pEnum->Next(100, 1, &pObj, NULL);

            if (SUCCEEDED(hr) && pObj) {
                for (int p = 0; p < 3; p++) {
                    VARIANT vtTemp;
                    VariantInit(&vtTemp);
                    hr = pObj->Get(propNames[p], 0, &vtTemp, NULL, NULL);
                    if (SUCCEEDED(hr) && vtTemp.vt != VT_EMPTY) {
                        double temp = -1;
                        if (vtTemp.vt == VT_I4 || vtTemp.vt == VT_UI4 || vtTemp.vt == VT_I2 || vtTemp.vt == VT_UI2) {
                            temp = vtTemp.vt == VT_UI4 ? vtTemp.ulVal :
                                   vtTemp.vt == VT_I2 ? vtTemp.iVal :
                                   vtTemp.vt == VT_UI2 ? vtTemp.uiVal : vtTemp.intVal;
                        } else if (vtTemp.vt == VT_R8) {
                            temp = vtTemp.dblVal;
                        } else if (vtTemp.vt == VT_R4) {
                            temp = vtTemp.fltVal;
                        }
                        VariantClear(&vtTemp);
                        if (temp > 0 && temp < 150) {
                            pObj->Release();
                            pEnum->Release();
                            return temp;
                        }
                    }
                }
                pObj->Release();
            }
            pEnum->Release();
        }
    }
#endif
    return -1;
}

static double getGpuTemperatureFromNvApi()
{
#ifdef Q_OS_WIN32
    static HMODULE nvapi = nullptr;
    static bool initialized = false;
    static NvApiEnumPhysicalGpus enumPhysicalGpus = nullptr;
    static NvApiGetThermalSettings getThermalSettings = nullptr;
    if (!initialized) {
        initialized = true;
        nvapi = LoadLibraryW(sizeof(void *) == 8 ? L"nvapi64.dll" : L"nvapi.dll");
        if (nvapi) {
            NvApiQueryInterface query = reinterpret_cast<NvApiQueryInterface>(GetProcAddress(nvapi, "nvapi_QueryInterface"));
            if (query) {
                NvApiInitialize initialize = reinterpret_cast<NvApiInitialize>(query(0x0150E828));
                enumPhysicalGpus = reinterpret_cast<NvApiEnumPhysicalGpus>(query(0xE5AC921F));
                getThermalSettings = reinterpret_cast<NvApiGetThermalSettings>(query(0xE3640A56));
                if (!initialize || initialize() != 0) {
                    enumPhysicalGpus = nullptr;
                    getThermalSettings = nullptr;
                }
            }
        }
    }
    if (!enumPhysicalGpus || !getThermalSettings) return -1.0;

    NvPhysicalGpuHandle gpus[64] = {};
    unsigned int gpuCount = 0;
    if (enumPhysicalGpus(gpus, &gpuCount) != 0) return -1.0;
    for (unsigned int i = 0; i < gpuCount && i < 64; ++i) {
        NvApiThermalSettingsV1 settings = {};
        settings.version = static_cast<unsigned int>(sizeof(settings)) | (1u << 16);
        // NVAPI_THERMAL_TARGET_ALL = 15
        if (getThermalSettings(gpus[i], 15, &settings) == 0) {
            for (unsigned int sensor = 0; sensor < settings.count && sensor < 3; ++sensor) {
                const unsigned int temp = settings.sensor[sensor].currentTemp;
                if (temp > 0 && temp < 150) return static_cast<double>(temp);
            }
        }
    }
#endif
    return -1.0;
}

int SystemMonitor::getNetworkLatency() const
{
    QMutexLocker lock(&m_dataMutex);
    return m_networkLatency;
}

int SystemMonitor::probeNetworkLatency(const QString &host)
{
#ifdef Q_OS_WIN32
    HANDLE hIcmp = IcmpCreateFile();
    if (!hIcmp) return -1;

    char SendData[32] = "PingTest";
    char ReplyBuffer[sizeof(ICMP_ECHO_REPLY) + 32];

    DWORD dwRetVal = IcmpSendEcho(
        hIcmp,
        inet_addr(host.toStdString().c_str()),
        SendData,
        sizeof(SendData),
        NULL,
        ReplyBuffer,
        sizeof(ReplyBuffer),
        1000
    );

    IcmpCloseHandle(hIcmp);

    if (dwRetVal > 0) {
        PICMP_ECHO_REPLY pEchoReply = (PICMP_ECHO_REPLY)ReplyBuffer;
        return pEchoReply->RoundTripTime;
    }
#endif

    return -1;
}

double SystemMonitor::getVideoMemoryTotalFromDxgi()
{
#ifdef Q_OS_WIN32
    IDXGIFactory1 *factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(&factory))))
        return 0.0;

    double totalBytes = 0.0;
    for (UINT index = 0;; ++index) {
        IDXGIAdapter1 *adapter = nullptr;
        if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND)
            break;
        if (!adapter)
            continue;

        DXGI_ADAPTER_DESC1 desc;
        if (SUCCEEDED(adapter->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
            totalBytes += static_cast<double>(desc.DedicatedVideoMemory);
        adapter->Release();
    }
    factory->Release();
    return totalBytes / (1024.0 * 1024.0);
#else
    return 0.0;
#endif
}

void SystemMonitor::updateData()
{
#ifdef Q_OS_WIN32
    updateNetworkRates();
    updatePresentMonTarget();
    if (m_presentMonProcess && m_presentMonProcess->state() == QProcess::Running) {
        parsePresentMonOutput();
        const QByteArray errors = m_presentMonProcess->readAllStandardError();
        if (!errors.isEmpty() && m_frameSamples.isEmpty()) {
            QMutexLocker lock(&m_dataMutex);
            m_frameCaptureStatus = QString::fromUtf8("ETW \xe4\xb8\x8d\xe5\x8f\xaf\xe7\x94\xa8\xef\xbc\x9a\xe6\xa3\x80\xe6\x9f\xa5\xe6\x9d\x83\xe9\x99\x90");
        }
        if (m_frameSamples.isEmpty() && QDateTime::currentMSecsSinceEpoch() - m_targetStartedAt > 3000) {
            QMutexLocker lock(&m_dataMutex);
            m_frameCaptureStatus = QString::fromUtf8("\xe6\x9c\xaa\xe6\xa3\x80\xe6\xb5\x8b\xe5\x88\xb0\xe5\xb8\xa7\xef\xbc\x9a\xe6\xa3\x80\xe6\x9f\xa5 ETW \xe6\x9d\x83\xe9\x99\x90");
        }
        updateFrameMetrics();
    }
    double cpuUsage = 0.0;
    const double gpuUsage = readGpuUsage();
    const double videoMemoryUsedMB = readGpuMemoryUsedMB(m_targetProcessId);
    double memoryUsageMB = 0.0;
    double videoMemoryMB = getVideoMemoryTotalFromDxgi();
    double cpuTemp = -1.0;
    double gpuTemp = -1.0;
    int networkLatency = -1;
    QString networkInfo;
    QString deviceInfo;
    QString storageInfo;
    {
        QMutexLocker lock(&m_dataMutex);
        memoryUsageMB = m_memoryUsageMB;
        networkLatency = m_networkLatency;
        networkInfo = m_networkInfo;
        deviceInfo = m_deviceInfo;
        storageInfo = m_storageInfo;
    }

    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : interfaces) {
        if (!(iface.flags() & QNetworkInterface::IsUp) ||
            !(iface.flags() & QNetworkInterface::IsRunning) ||
            (iface.flags() & QNetworkInterface::IsLoopBack))
            continue;

        const QList<QNetworkAddressEntry> entries = iface.addressEntries();
        for (const QNetworkAddressEntry &entry : entries) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                networkInfo = iface.humanReadableName() + "  " + entry.ip().toString();
                break;
            }
        }
        if (!networkInfo.isEmpty()) break;
    }
    if (networkInfo.isEmpty()) networkInfo = "--";

    const QString hostName = QSysInfo::machineHostName();
    const QString osName = QSysInfo::prettyProductName();
    deviceInfo = hostName.isEmpty() ? osName : hostName + "  |  " + osName;

    QStringList volumes;
    const DWORD driveMask = GetLogicalDrives();
    for (int driveIndex = 0; driveIndex < 26; ++driveIndex) {
        if (!(driveMask & (1u << driveIndex))) continue;
        wchar_t rootPath[] = L"A:\\";
        rootPath[0] = static_cast<wchar_t>(L'A' + driveIndex);
        if (GetDriveTypeW(rootPath) != DRIVE_FIXED) continue;
        ULARGE_INTEGER available, total, freeBytes;
        if (!GetDiskFreeSpaceExW(rootPath, &available, &total, &freeBytes) || total.QuadPart == 0) continue;
        const double totalGb = total.QuadPart / (1024.0 * 1024.0 * 1024.0);
        const double freeGb = available.QuadPart / (1024.0 * 1024.0 * 1024.0);
        volumes.append(QString("%1: %2 / %3 GB free")
                       .arg(QChar('A' + driveIndex))
                       .arg(freeGb, 0, 'f', 1)
                       .arg(totalGb, 0, 'f', 1));
    }
    storageInfo = volumes.isEmpty() ? QString("--") : volumes.join("  |  ");
    if (m_cpuQuery) {
        PdhCollectQueryData(m_cpuQuery);
    }

    PDH_FMT_COUNTERVALUE cpuValue;
    DWORD dwType = 0;
    if (m_cpuCounter && PdhGetFormattedCounterValue(m_cpuCounter, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &dwType, &cpuValue) == ERROR_SUCCESS) {
        cpuUsage = cpuValue.doubleValue;
    } else {
        cpuUsage = 0;
    }

    MEMORYSTATUSEX memStatus;
    memStatus.dwLength = sizeof(memStatus);
    if (GlobalMemoryStatusEx(&memStatus)) {
        memoryUsageMB = (memStatus.ullTotalPhys - memStatus.ullAvailPhys) / (1024.0 * 1024.0);
    }

    if (m_wmiInitialized && m_wmiServices && videoMemoryMB <= 0.0) {
        IEnumWbemClassObject *pEnum = nullptr;
        HRESULT hr = executeWmiQuery(m_wmiServices,
            L"SELECT AdapterRAM FROM Win32_VideoController WHERE AdapterRAM IS NOT NULL",
            &pEnum, WBEM_FLAG_RETURN_IMMEDIATELY | WBEM_FLAG_FORWARD_ONLY);

        double totalVideoMemory = 0;
        if (SUCCEEDED(hr) && pEnum) {
            IWbemClassObject *pObj = nullptr;
            for (int adapterIndex = 0; adapterIndex < 8 && pEnum->Next(100, 1, &pObj, NULL) == S_OK && pObj; ++adapterIndex) {
                VARIANT vtRam;
                VariantInit(&vtRam);
                hr = pObj->Get(L"AdapterRAM", 0, &vtRam, NULL, NULL);
                if (SUCCEEDED(hr) && (vtRam.vt == VT_UI4 || vtRam.vt == VT_I4 || vtRam.vt == VT_UI8 || vtRam.vt == VT_I8))
                    totalVideoMemory += vtRam.vt == VT_UI8 ? static_cast<double>(vtRam.ullVal) :
                                        vtRam.vt == VT_I8 ? static_cast<double>(vtRam.llVal) :
                                        vtRam.vt == VT_UI4 ? static_cast<double>(vtRam.ulVal) :
                                        static_cast<double>(vtRam.lVal);
                if (vtRam.vt != VT_EMPTY) VariantClear(&vtRam);
                pObj->Release();
                pObj = nullptr;
            }
            pEnum->Release();
        }
        videoMemoryMB = totalVideoMemory / (1024.0 * 1024.0);
    }

    {
        QMutexLocker lock(&m_dataMutex);
        cpuTemp = m_cpuTemp;
        gpuTemp = m_gpuTemp;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastThermalSampleMs >= 5000) {
        m_lastThermalSampleMs = now;
        cpuTemp = getCpuTemperatureFromWmi();
        gpuTemp = getGpuTemperatureFromNvApi();
        if (gpuTemp < 0.0) gpuTemp = getGpuTemperatureFromWmi();
    }
    static int pingCounter = 0;
    pingCounter++;
    if (pingCounter >= 5) {
        pingCounter = 0;
        networkLatency = probeNetworkLatency("8.8.8.8");
    }

    {
        QMutexLocker lock(&m_dataMutex);
        m_cpuUsage = cpuUsage;
        m_gpuUsage = gpuUsage;
        m_memoryUsageMB = memoryUsageMB;
        m_videoMemoryMB = videoMemoryMB;
        m_videoMemoryUsedMB = videoMemoryUsedMB;
        m_cpuTemp = cpuTemp;
        m_gpuTemp = gpuTemp;
        m_networkLatency = networkLatency;
        m_networkInfo = networkInfo;
        m_deviceInfo = deviceInfo;
        m_storageInfo = storageInfo;
    }

    qDebug() << QString("CPU: %1%  Memory: %2MB  VRAM total (estimated): %3MB  Latency: %4ms")
                .arg(cpuUsage, 0, 'f', 1)
                .arg(memoryUsageMB, 0, 'f', 0)
                .arg(videoMemoryMB, 0, 'f', 0)
                .arg(networkLatency);
#endif
}
