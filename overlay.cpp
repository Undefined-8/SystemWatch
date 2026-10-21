#include "overlay.h"
#include "systemmonitor.h"

#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QDesktopWidget>
#include <QApplication>
#include <QDebug>
#include <QHeaderView>
#include <QMenu>
#include <QActionGroup>
#include <QAction>
#include <QSettings>
#include <QPainter>
#include <QVector>
#include <algorithm>

#ifdef Q_OS_WIN32
#include <windows.h>
#endif

class NetworkGraphWidget : public QWidget
{
public:
    explicit NetworkGraphWidget(QWidget *parent = nullptr) : QWidget(parent) { setFixedHeight(64); }
    void setLanguage(int language) { m_language = language; update(); }
    void append(double download, double upload) {
        m_values.append(qMakePair(download, upload));
        while (m_values.size() > 60) m_values.removeFirst();
        update();
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), QColor(10, 14, 26, 175));
        p.setPen(QColor(120, 150, 180, 70));
        p.drawLine(12, height() - 15, width() - 10, height() - 15);
        p.drawLine(12, height() / 2, width() - 10, height() / 2);
        double maximum = 1.0;
        for (const QPair<double, double> &v : m_values) maximum = qMax(maximum, qMax(v.first, v.second));
        p.setPen(QColor(170, 185, 205));
        p.drawText(width() - 76, height() / 2 - 2, QString::number(maximum, 'f', 1) + " Mbps");
        p.drawText(12, height() - 2, "60s");
        p.drawText(width() - 39, height() - 2, "now");
        p.setPen(QColor(0, 212, 255));
        p.drawText(12, 14, m_language == 1 ? QString::fromUtf8("\xe2\x86\x93 \xe4\xb8\x8b\xe8\xa1\x8c") : QString::fromLatin1("Download"));
        p.setPen(QColor(255, 180, 70));
        p.drawText(86, 14, m_language == 1 ? QString::fromUtf8("\xe2\x86\x91 \xe4\xb8\x8a\xe8\xa1\x8c") : QString::fromLatin1("Upload"));
        if (m_values.size() < 2) return;
        const QRectF plot(12, 20, width() - 22, height() - 37);
        QPainterPath down, up;
        for (int i = 0; i < m_values.size(); ++i) {
            const qreal x = plot.left() + plot.width() * i / qMax(1, m_values.size() - 1);
            const qreal yDown = plot.bottom() - plot.height() * m_values.at(i).first / maximum;
            const qreal yUp = plot.bottom() - plot.height() * m_values.at(i).second / maximum;
            if (!i) { down.moveTo(x, yDown); up.moveTo(x, yUp); }
            else { down.lineTo(x, yDown); up.lineTo(x, yUp); }
        }
        p.setPen(QPen(QColor(0, 212, 255), 2)); p.drawPath(down);
        p.setPen(QPen(QColor(255, 180, 70), 2)); p.drawPath(up);
    }
private:
    QVector<QPair<double, double> > m_values;
    int m_language = 1;
};

Overlay::Overlay(SystemMonitor *monitor, QWidget *parent)
    : QWidget(parent)
    , m_monitor(monitor)
    , m_isDragging(false)
    , m_isVisible(false)
    , m_lang(0)
    , m_statusTable(nullptr)
    , m_networkGraph(nullptr)
    , m_frameCard(nullptr)
    , m_fpsValue(nullptr)
    , m_fpsStatus(nullptr)
    , m_frameTimeValue(nullptr)
    , m_onePercentValue(nullptr)
    , m_mousePassThrough(true)
{
    setWindowAttribute();
    initStyle();
    initUI();

    if (m_monitor) {
        connect(m_monitor, &SystemMonitor::dataUpdated, this, &Overlay::onDataUpdated);
    }

    QSettings settings("GameMonitor", "Settings");
    QDesktopWidget *desktop = QApplication::desktop();
    const bool hasPosition = settings.contains("overlayX") && settings.contains("overlayY");
    QPoint savedPosition = hasPosition
        ? QPoint(settings.value("overlayX").toInt(), settings.value("overlayY").toInt())
        : QPoint(-1, -1);
    const int screen = hasPosition ? desktop->screenNumber(savedPosition) : 0;
    const QRect available = desktop->availableGeometry(qMax(0, screen));
    const int fitWidth = qMax(320, qMin(width(), available.width() - 20));
    m_mainFrame->setFixedWidth(fitWidth);
    setFixedWidth(fitWidth);
    if (hasPosition) {
        const int maxX = qMax(available.left(), available.right() - width() + 1);
        const int maxY = qMax(available.top(), available.bottom() - height() + 1);
        move(qBound(available.left(), savedPosition.x(), maxX),
             qBound(available.top(), savedPosition.y(), maxY));
    } else {
        move(available.right() - width() - 30, available.top() + 45);
    }
    resizeTableToContents();
}

Overlay::~Overlay()
{
}

void Overlay::setWindowAttribute()
{
    Qt::WindowFlags flags = Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint;
    setWindowFlags(flags);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_MouseTracking);
    setAcceptDrops(true);
}

void Overlay::initUI()
{
    m_mainFrame = new QFrame(this);
    m_mainFrame->setObjectName("overlayFrame");
    const int overlayWidth = qMax(320, qMin(640, QApplication::desktop()->availableGeometry().width() - 20));
    m_mainFrame->setFixedWidth(overlayWidth);
    setFixedWidth(overlayWidth);

    m_mainLayout = new QVBoxLayout(m_mainFrame);
    m_mainLayout->setContentsMargins(10, 8, 10, 8);
    m_mainLayout->setSpacing(7);

    QLabel *title = new QLabel(QString::fromUtf8("\xe7\xb3\xbb\xe7\xbb\x9f\xe7\x8a\xb6\xe6\x80\x81"));
    title->setObjectName("overlayTitle");
    title->setAlignment(Qt::AlignCenter);
    m_mainLayout->addWidget(title);

    m_frameCard = new QFrame(m_mainFrame);
    m_frameCard->setObjectName("frameCard");
    QHBoxLayout *frameLayout = new QHBoxLayout(m_frameCard);
    frameLayout->setContentsMargins(14, 9, 14, 9);
    frameLayout->setSpacing(16);
    m_fpsValue = new QLabel("--", m_frameCard);
    m_fpsValue->setObjectName("fpsValue");
    QVBoxLayout *fpsColumn = new QVBoxLayout();
    QLabel *fpsCaption = new QLabel("FPS", m_frameCard);
    fpsCaption->setObjectName("fpsCaption");
    fpsColumn->addWidget(m_fpsValue);
    fpsColumn->addWidget(fpsCaption);
    fpsColumn->setSpacing(0);
    frameLayout->addLayout(fpsColumn);
    QFrame *divider = new QFrame(m_frameCard);
    divider->setFrameShape(QFrame::VLine);
    divider->setObjectName("frameDivider");
    frameLayout->addWidget(divider);
    QVBoxLayout *frameStats = new QVBoxLayout();
    m_frameTimeValue = new QLabel("-- ms", m_frameCard);
    m_frameTimeValue->setObjectName("frameStatValue");
    m_onePercentValue = new QLabel("--", m_frameCard);
    m_onePercentValue->setObjectName("frameStatValue");
    QLabel *frameTimeCaption = new QLabel("Frame time", m_frameCard);
    QLabel *onePercentCaption = new QLabel("1% Low", m_frameCard);
    frameTimeCaption->setObjectName("frameTimeCaption");
    onePercentCaption->setObjectName("onePercentCaption");
    QHBoxLayout *frameTimeLine = new QHBoxLayout();
    frameTimeLine->addWidget(frameTimeCaption);
    frameTimeLine->addStretch();
    frameTimeLine->addWidget(m_frameTimeValue);
    QHBoxLayout *lowLine = new QHBoxLayout();
    lowLine->addWidget(onePercentCaption);
    lowLine->addStretch();
    lowLine->addWidget(m_onePercentValue);
    frameStats->addLayout(frameTimeLine);
    frameStats->addLayout(lowLine);
    frameStats->setSpacing(3);
    frameLayout->addLayout(frameStats, 1);
    m_fpsStatus = new QLabel(QString::fromUtf8("\xe7\xad\x89\xe5\xbe\x85\xe6\xb8\xb8\xe6\x88\x8f\xe5\xb8\xa7\xe6\x95\xb0\xe6\x8d\xae"), m_mainFrame);
    m_fpsStatus->setObjectName("fpsStatus");
    m_fpsStatus->setWordWrap(false);
    m_fpsStatus->setFixedHeight(19);
    m_mainLayout->addWidget(m_frameCard);
    m_mainLayout->addWidget(m_fpsStatus);

    m_statusTable = new QTableWidget(15, 3, m_mainFrame);
    m_statusTable->setObjectName("statusTable");
    m_statusTable->setHorizontalHeaderLabels(QStringList() << "Category" << "Metric" << "Current value");
    m_statusTable->verticalHeader()->setVisible(false);
    m_statusTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_statusTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_statusTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_statusTable->setColumnWidth(0, 96);
    m_statusTable->setColumnWidth(1, 150);
    m_statusTable->setWordWrap(false);
    m_statusTable->setTextElideMode(Qt::ElideRight);
    m_statusTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_statusTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_statusTable->setFocusPolicy(Qt::NoFocus);
    m_statusTable->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_statusTable->setShowGrid(true);
    m_statusTable->setAlternatingRowColors(true);
    m_statusTable->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_statusTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_statusTable->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    for (int row = 0; row < 15; ++row) {
        m_statusTable->setRowHeight(row, qMax(36, m_statusTable->fontMetrics().height() + 16));
        for (int col = 0; col < 3; ++col) {
            QTableWidgetItem *item = new QTableWidgetItem;
            item->setFlags(Qt::ItemIsEnabled);
            item->setTextAlignment(col == 2 ? (Qt::AlignLeft | Qt::AlignVCenter) : (Qt::AlignLeft | Qt::AlignVCenter));
            m_statusTable->setItem(row, col, item);
        }
    }
    for (int row = 0; row < 3; ++row) m_statusTable->setRowHidden(row, true);
    m_mainLayout->addWidget(m_statusTable);
    m_networkGraph = new NetworkGraphWidget(m_mainFrame);
    m_networkGraph->setObjectName("networkGraph");
    m_networkGraph->setFixedHeight(64);
    m_networkGraph->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_mainLayout->addWidget(m_networkGraph);
    setLanguage(1);
    onDataUpdated();

    QVBoxLayout *main = new QVBoxLayout(this);
    main->setContentsMargins(0, 0, 0, 0);
    main->addWidget(m_mainFrame);

}

void Overlay::initStyle()
{
    setStyleSheet(R"(
        #overlayFrame {
            background-color: rgba(16, 23, 36, 238);
            border: 1px solid rgba(108, 137, 170, 115);
            border-radius: 12px;
        }
        #frameCard { background-color: rgba(10, 16, 27, 235); border: 1px solid rgba(90, 120, 153, 100); border-radius: 9px; }
        #fpsValue { color: #5ce1e6; font-family: 'Consolas'; font-size: 42px; font-weight: bold; }
        #fpsCaption, #frameTimeCaption, #onePercentCaption { color: #c0cfe0; font-size: 10pt; }
        #frameStatValue { color: #edf5ff; font-family: 'Consolas'; font-size: 13pt; font-weight: bold; }
        #fpsStatus { color: #b7c7db; font-size: 10pt; padding-left: 4px; }
        #frameDivider { color: rgba(120, 145, 175, 90); }
        #overlayFrame #overlayTitle { color: #dbe8f8; font-size: 14pt; }
        #overlayFrame #statusTable { border: 1px solid rgba(90, 120, 153, 85); gridline-color: rgba(104, 126, 153, 40); font-size: 11pt; font-family: 'Microsoft YaHei UI', 'Microsoft YaHei', sans-serif; }
        #overlayFrame #statusTable QHeaderView::section { background-color: rgba(40, 55, 76, 220); border-right: 1px solid rgba(104, 126, 153, 60); }
        #overlayFrame #statusTable::item { padding: 3px 4px; }
        #labelName {
            color: rgba(0, 212, 255, 180);
            font-family: 'Consolas', 'Microsoft YaHei';
            font-size: 11pt;
        }
        #labelValue {
            color: #00ff88;
            font-family: 'Consolas', 'Microsoft YaHei';
            font-size: 12px;
            font-weight: bold;
        }
        #overlayTitle {
            color: #ffffff;
            font-family: 'Microsoft YaHei UI', 'Microsoft YaHei';
            font-size: 16px;
            font-weight: bold;
            padding: 2px 0 5px 0;
        }
        #sectionTitle {
            color: #00d4ff;
            font-family: 'Microsoft YaHei UI', 'Microsoft YaHei';
            font-size: 10px;
            font-weight: bold;
            padding-top: 3px;
            border-bottom: 1px solid rgba(0, 212, 255, 80);
        }
        #statusTable {
            background-color: rgba(10, 14, 26, 175);
            alternate-background-color: rgba(35, 43, 62, 185);
            color: #f3f7ff;
            gridline-color: rgba(120, 150, 180, 80);
            border: 1px solid rgba(0, 212, 255, 90);
            selection-background-color: rgba(0, 212, 255, 55);
            font-family: 'Microsoft YaHei UI', 'Microsoft YaHei', 'Consolas';
            font-size: 11pt;
        }
        #statusTable QHeaderView::section {
            background-color: rgba(0, 140, 180, 150);
            color: #ffffff;
            border: 0;
            border-right: 1px solid rgba(120, 190, 220, 80);
            padding: 5px 4px;
            font-weight: bold;
        }
    )");
}

void Overlay::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_isDragging = true;
        m_dragPosition = event->globalPos() - frameGeometry().topLeft();
        event->accept();
    }
}

void Overlay::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isDragging && event->buttons() & Qt::LeftButton) {
        move(event->globalPos() - m_dragPosition);
        event->accept();
    }
}

void Overlay::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_isDragging = false;
        savePosition();
        event->accept();
    }
}

void Overlay::showOverlay()
{
    show();
    m_isVisible = true;
    setMousePassThrough(m_mousePassThrough);
}

void Overlay::hideOverlay()
{
    hide();
    m_isVisible = false;
}

void Overlay::toggleVisibility()
{
    if (m_isVisible) {
        hideOverlay();
    } else {
        showOverlay();
    }
}

bool Overlay::isOverlayVisible() const
{
    return m_isVisible;
}

void Overlay::setMetricVisibility(bool cpu, bool gpu, bool memory, bool vram, bool cpuTemp, bool gpuTemp,
                                  bool ping, bool networkInfo, bool deviceInfo, bool storageInfo,
                                  bool networkTraffic, bool frameMetrics)
{
    m_statusTable->setRowHidden(0, true);
    m_statusTable->setRowHidden(1, true);
    m_statusTable->setRowHidden(2, true);
    m_frameCard->setVisible(frameMetrics);
    m_fpsStatus->setVisible(frameMetrics);
    m_statusTable->setRowHidden(3, !cpu);
    m_statusTable->setRowHidden(4, !gpu);
    m_statusTable->setRowHidden(5, !cpuTemp);
    m_statusTable->setRowHidden(6, !gpuTemp);
    m_statusTable->setRowHidden(7, !memory);
    m_statusTable->setRowHidden(8, !vram);
    m_statusTable->setRowHidden(9, !networkTraffic);
    m_statusTable->setRowHidden(10, !networkTraffic);
    m_statusTable->setRowHidden(11, !ping);
    m_statusTable->setRowHidden(12, !networkInfo);
    m_statusTable->setRowHidden(13, !deviceInfo);
    m_statusTable->setRowHidden(14, !storageInfo);
    m_networkGraph->setVisible(networkTraffic);
    resizeTableToContents();
}
void Overlay::setMousePassThrough(bool enabled)
{
    m_mousePassThrough = enabled;
#ifdef Q_OS_WIN32
    if (winId()) {
        const LONG_PTR exStyle = GetWindowLongPtr((HWND)winId(), GWL_EXSTYLE);
        SetWindowLongPtr((HWND)winId(), GWL_EXSTYLE,
                         enabled ? (exStyle | WS_EX_TRANSPARENT) : (exStyle & ~WS_EX_TRANSPARENT));
        SetWindowPos((HWND)winId(), HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
#endif
}

void Overlay::setTableValue(int row, const QString &value)
{
    QTableWidgetItem *item = m_statusTable->item(row, 2);
    if (item) {
        item->setText(value);
        item->setToolTip(value);
    }
}

void Overlay::resizeTableToContents()
{
    int contentHeight = m_statusTable->horizontalHeader()->height() + 2 * m_statusTable->frameWidth();
    for (int row = 0; row < m_statusTable->rowCount(); ++row) {
        if (!m_statusTable->isRowHidden(row))
            contentHeight += m_statusTable->rowHeight(row);
    }
    m_statusTable->setFixedHeight(contentHeight + 2);
    m_mainFrame->adjustSize();
    adjustSize();
    const QRect available = QApplication::desktop()->availableGeometry(this);
    const int maxX = qMax(available.left(), available.right() - width() + 1);
    const int maxY = qMax(available.top(), available.bottom() - height() + 1);
    const QPoint bounded(qBound(available.left(), x(), maxX), qBound(available.top(), y(), maxY));
    if (bounded != pos()) move(bounded);
}

void Overlay::onDataUpdated()
{
    if (!m_monitor) return;
    const double cpu = m_monitor->getCpuUsage();
    setTableValue(3, cpu >= 0 ? QString::number(cpu, 'f', 1) + "%" : "--");
    const double gpu = m_monitor->getGpuUsage();
    setTableValue(4, gpu >= 0 ? QString::number(gpu, 'f', 0) + "%" : (m_lang == 1 ? QString::fromUtf8("\xe6\x9a\x82\xe4\xb8\x8d\xe5\x8f\xaf\xe7\x94\xa8") : "Unavailable"));

    const double memMB = m_monitor->getMemoryUsageMB();
    const double memPercent = m_monitor->getMemoryUsagePercent();
    const qint64 totalMemMB = m_monitor->getTotalMemoryMB();
    setTableValue(7, QString::number(memMB / 1024.0, 'f', 1) + " / " +
                  QString::number(totalMemMB / 1024.0, 'f', 1) + " GB (" + QString::number(memPercent, 'f', 0) + "%)");
    const double vram = m_monitor->getVideoMemoryTotalMB();
    const double usedVram = m_monitor->getVideoMemoryUsedMB();
    if (vram > 0 && usedVram >= 0) {
        setTableValue(8, QString::number(usedVram / 1024.0, 'f', 1) + " / " +
                      QString::number(vram / 1024.0, 'f', 1) + " GB (" +
                      QString::number(usedVram * 100.0 / vram, 'f', 0) + "%)");
    } else if (vram > 0) {
        setTableValue(8, QString("-- / %1 GB").arg(vram / 1024.0, 0, 'f', 1));
    } else {
        setTableValue(8, m_lang == 1 ? QString::fromUtf8("\xe4\xb8\x8d\xe5\x8f\xaf\xe7\x94\xa8") : "Unavailable");
    }

    const double cpuTemp = m_monitor->getCpuTemperature();
    const QString degreeC = QString::fromUtf8(" \xC2\xB0" "C");
    setTableValue(5, cpuTemp > 0 ? QString::number(cpuTemp, 'f', 0) + degreeC : (m_lang == 1 ? QString::fromUtf8("\xe6\x97\xa0\xe6\xb8\xa9\xe5\xba\xa6\xe4\xbc\xa0\xe6\x84\x9f\xe5\x99\xa8") : "Sensor unavailable"));
    const double gpuTemp = m_monitor->getGpuTemperature();
    setTableValue(6, gpuTemp > 0 ? QString::number(gpuTemp, 'f', 0) + degreeC : (m_lang == 1 ? QString::fromUtf8("\xe6\x9a\x82\xe4\xb8\x8d\xe5\x8f\xaf\xe7\x94\xa8") : "Unavailable"));

    const double download = m_monitor->getNetworkDownloadMbps();
    const double upload = m_monitor->getNetworkUploadMbps();
    setTableValue(9, QString::fromUtf8("\xe2\x86\x93 ") + QString::number(download, 'f', 2) + " Mbps");
    setTableValue(10, QString::fromUtf8("\xe2\x86\x91 ") + QString::number(upload, 'f', 2) + " Mbps");
    const int ping = m_monitor->getNetworkLatency();
    setTableValue(11, ping > 0 ? QString::number(ping) + " ms" : "-- ms");
    setTableValue(12, m_monitor->getNetworkInfo());
    setTableValue(13, m_monitor->getDeviceInfo());
    setTableValue(14, m_monitor->getStorageInfo());

    const double fps = m_monitor->getFps();
    const double frameTime = m_monitor->getFrameTimeMs();
    const double low = m_monitor->getOnePercentLowFps();
    const QString status = m_monitor->getFrameCaptureStatus();
    const bool capturing = status == QString::fromUtf8("\xe9\x87\x87\xe9\x9b\x86\xe4\xb8\xad");
    m_fpsValue->setText(fps > 0 && capturing ? QString::number(fps, 'f', 0) : "--");
    m_frameTimeValue->setText(frameTime > 0 ? QString::number(frameTime, 'f', 2) + " ms" : "-- ms");
    m_onePercentValue->setText(low > 0 ? QString::number(low, 'f', 1) : "--");
    m_fpsStatus->setText(capturing
        ? (fps > 0 ? (m_lang == 1 ? QString::fromUtf8("\xe6\x8d\x95\xe8\x8e\xb7\xe5\xb8\xa7\xe7\x8e\x87\xe6\x95\xb0\xe6\x8d\xae") : "Frame data active")
                   : (m_lang == 1 ? QString::fromUtf8("\xe7\xad\x89\xe5\xbe\x85\xe6\xb8\xb8\xe6\x88\x8f\xe5\xb8\xa7\xe6\x95\xb0\xe6\x8d\xae") : "Waiting for game frames"))
        : status);
    m_frameCard->setToolTip(status);
    m_fpsStatus->setToolTip(status);
    m_statusTable->item(5, 2)->setToolTip(m_lang == 1
        ? QString::fromUtf8("Intel CPU \xe6\xb8\xa9\xe5\xba\xa6\xe9\x9c\x80\xe8\xa6\x81 LibreHardwareMonitor \xe6\x8f\x90\xe4\xbe\x9b\xe4\xbc\xa0\xe6\x84\x9f\xe5\x99\xa8")
        : "Intel CPU temperature requires a LibreHardwareMonitor sensor provider.");
    m_statusTable->item(4, 2)->setToolTip(m_lang == 1
        ? QString::fromUtf8("Windows GPU Engine \xe6\x80\xbb\xe4\xbd\x93\xe5\xbc\x95\xe6\x93\x8e\xe5\x88\xa9\xe7\x94\xa8\xe7\x8e\x87\xef\xbc\x8c\xe6\x97\xa0\xe6\x95\xb0\xe6\x8d\xae\xe6\x97\xb6\xe6\x98\xbe\xe7\xa4\xba\xe4\xb8\x8d\xe5\x8f\xaf\xe7\x94\xa8")
        : "Aggregate Windows GPU Engine utilization; unavailable when the driver exposes no counters.");
    static_cast<NetworkGraphWidget *>(m_networkGraph)->append(download, upload);
}
void Overlay::setLanguage(int lang)
{
    m_lang = lang;
    static_cast<NetworkGraphWidget *>(m_networkGraph)->setLanguage(lang);
    QLabel *timeCaption = findChild<QLabel *>("frameTimeCaption");
    QLabel *lowCaption = findChild<QLabel *>("onePercentCaption");
    QLabel *fpsCaption = findChild<QLabel *>("fpsCaption");
    if (timeCaption) timeCaption->setText(lang == 1 ? QString::fromUtf8("\xe5\xb8\xa7\xe6\x97\xb6\xe9\x97\xb4") : "Frame time");
    if (lowCaption) lowCaption->setText(lang == 1 ? QString::fromUtf8("1% \xe4\xbd\x8e\xe5\xb8\xa7") : "1% Low");
    if (fpsCaption) fpsCaption->setText("FPS");
    const QString categoryPerformance = lang == 1 ? QString::fromUtf8("\xe6\x80\xa7\xe8\x83\xbd") : "Performance";
    const QString categoryNetwork = lang == 1 ? QString::fromUtf8("\xe7\xbd\x91\xe7\xbb\x9c") : "Network";
    const QString categoryDevice = lang == 1 ? QString::fromUtf8("\xe8\xae\xbe\xe5\xa4\x87") : "Device";
    const QString metrics[] = {
        "FPS",
        lang == 1 ? QString::fromUtf8("\xe5\xb9\xb3\xe5\x9d\x87\xe5\xb8\xa7\xe6\x97\xb6\xe9\x97\xb4") : "Frame time",
        "1% Low",
        lang == 1 ? QString::fromUtf8("CPU \xe4\xbd\xbf\xe7\x94\xa8\xe7\x8e\x87") : "CPU usage",
        lang == 1 ? QString::fromUtf8("GPU \xe4\xbd\xbf\xe7\x94\xa8\xe7\x8e\x87") : "GPU usage",
        lang == 1 ? QString::fromUtf8("CPU \xe6\xb8\xa9\xe5\xba\xa6") : "CPU temperature",
        lang == 1 ? QString::fromUtf8("GPU \xe6\xb8\xa9\xe5\xba\xa6") : "GPU temperature",
        lang == 1 ? QString::fromUtf8("\xe5\x86\x85\xe5\xad\x98") : "Memory",
        lang == 1 ? QString::fromUtf8("\xe6\xb8\xb8\xe6\x88\x8f\xe6\x98\xbe\xe5\xad\x98\xe4\xbd\xbf\xe7\x94\xa8") : "Game VRAM usage",
        lang == 1 ? QString::fromUtf8("\xe4\xb8\x8b\xe8\xa1\x8c\xe9\x80\x9f\xe7\x8e\x87") : "Download",
        lang == 1 ? QString::fromUtf8("\xe4\xb8\x8a\xe8\xa1\x8c\xe9\x80\x9f\xe7\x8e\x87") : "Upload",
        lang == 1 ? QString::fromUtf8("\xe7\xbd\x91\xe7\xbb\x9c\xe5\xbb\xb6\xe8\xbf\x9f") : "Latency",
        lang == 1 ? QString::fromUtf8("\xe7\xbd\x91\xe7\xbb\x9c\xe6\x8e\xa5\xe5\x85\xa5") : "Connection",
        lang == 1 ? QString::fromUtf8("\xe8\xae\xbe\xe5\xa4\x87\xe4\xbf\xa1\xe6\x81\xaf") : "Device info",
        lang == 1 ? QString::fromUtf8("\xe7\xa3\x81\xe7\x9b\x98\xe5\xae\xb9\xe9\x87\x8f") : "Disk space"
    };
    for (int row = 0; row < 15; ++row) {
        QTableWidgetItem *category = m_statusTable->item(row, 0);
        QTableWidgetItem *metric = m_statusTable->item(row, 1);
        if (category) category->setText(row <= 8 ? categoryPerformance : (row <= 12 ? categoryNetwork : categoryDevice));
        if (metric) metric->setText(metrics[row]);
    }
    m_statusTable->setHorizontalHeaderLabels(lang == 1
        ? (QStringList() << QString::fromUtf8("\xe7\xb1\xbb\xe5\x88\xab") << QString::fromUtf8("\xe6\x8c\x87\xe6\xa0\x87") << QString::fromUtf8("\xe5\xbd\x93\xe5\x89\x8d\xe5\x80\xbc"))
        : (QStringList() << "Category" << "Metric" << "Current value"));
    m_statusTable->item(5, 2)->setToolTip(lang == 1
        ? QString::fromUtf8("Intel CPU \xe6\xa0\xb8\xe5\xbf\x83\xe6\xb8\xa9\xe5\xba\xa6\xe9\x9c\x80\xe8\xa6\x81 LibreHardwareMonitor \xe8\xbf\x90\xe8\xa1\x8c\xe5\xb9\xb6\xe6\x8f\x90\xe4\xbe\x9b WMI \xe4\xbc\xa0\xe6\x84\x9f\xe5\x99\xa8")
        : "Intel DTS readings require LibreHardwareMonitor with its WMI sensor provider running.");
}

void Overlay::savePosition()
{
    QSettings settings("GameMonitor", "Settings");
    settings.setValue("overlayX", x());
    settings.setValue("overlayY", y());
}

void Overlay::resetPosition()
{
    const QRect available = QApplication::desktop()->availableGeometry(this);
    const int maxX = qMax(available.left(), available.right() - width() + 1);
    const int maxY = qMax(available.top(), available.bottom() - height() + 1);
    move(qBound(available.left(), available.right() - width() - 30, maxX),
         qBound(available.top(), available.top() + 45, maxY));
    savePosition();
}

void Overlay::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    QAction *settingsAction = menu.addAction(m_lang == 1 ? QString::fromUtf8("\xe6\x89\x93\xe5\xbc\x80\xe8\xae\xbe\xe7\xbd\xae") : "Open settings");
    QMenu *presets = menu.addMenu(m_lang == 1 ? QString::fromUtf8("\xe5\xb8\x83\xe5\xb1\x80\xe9\xa2\x84\xe8\xae\xbe") : "Layout preset");
    QAction *game = presets->addAction(m_lang == 1 ? QString::fromUtf8("\xe6\xb8\xb8\xe6\x88\x8f\xe6\xa0\xb8\xe5\xbf\x83") : "Gaming HUD");
    QAction *system = presets->addAction(m_lang == 1 ? QString::fromUtf8("\xe7\xb3\xbb\xe7\xbb\x9f\xe4\xbf\xa1\xe6\x81\xaf") : "System information");
    QAction *custom = presets->addAction(m_lang == 1 ? QString::fromUtf8("\xe8\x87\xaa\xe5\xae\x9a\xe4\xb9\x89") : "Custom");
    QAction *moveAction = menu.addAction(m_lang == 1 ? QString::fromUtf8("\xe7\xa7\xbb\xe5\x8a\xa8\xe6\x82\xac\xe6\xb5\xae\xe7\xaa\x97") : "Move overlay");
    moveAction->setCheckable(true);
    moveAction->setChecked(!m_mousePassThrough);
    menu.addSeparator();
    QAction *resetAction = menu.addAction(m_lang == 1 ? QString::fromUtf8("\xe6\x81\xa2\xe5\xa4\x8d\xe9\xbb\x98\xe8\xae\xa4\xe4\xbd\x8d\xe7\xbd\xae") : "Restore default position");
    QAction *chosen = menu.exec(event->globalPos());
    if (chosen == settingsAction) emit requestOpenSettings();
    else if (chosen == game) emit requestPreset(0);
    else if (chosen == system) emit requestPreset(1);
    else if (chosen == custom) emit requestPreset(2);
    else if (chosen == moveAction) emit requestMoveMode(moveAction->isChecked());
    else if (chosen == resetAction) resetPosition();
}
