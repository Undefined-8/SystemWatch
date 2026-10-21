#ifndef OVERLAY_H
#define OVERLAY_H

#include <QWidget>
#include <QMouseEvent>
#include <QPoint>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QTableWidget>
#include <QContextMenuEvent>

class SystemMonitor;

class Overlay : public QWidget
{
    Q_OBJECT

public:
    explicit Overlay(SystemMonitor *monitor, QWidget *parent = nullptr);
    ~Overlay();

    void showOverlay();
    void hideOverlay();
    void toggleVisibility();
    bool isOverlayVisible() const;
    void setMetricVisibility(bool cpu, bool gpu, bool memory, bool vram, bool cpuTemp, bool gpuTemp,
                             bool ping, bool networkInfo, bool deviceInfo, bool storageInfo,
                             bool networkTraffic = true, bool frameMetrics = true);
    void setMousePassThrough(bool enabled);
    void resetPosition();

signals:
    void requestOpenSettings();
    void requestPreset(int preset);
    void requestMoveMode(bool moving);

signals:
    void overlayHidden();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    void initUI();
    void initStyle();
    void updateDisplay();
    void setWindowAttribute();
    void setTableValue(int row, const QString &value);
    void resizeTableToContents();
    void savePosition();

    SystemMonitor *m_monitor;
    bool m_isDragging;
    QPoint m_dragPosition;
    int m_lang;

    QTableWidget *m_statusTable;
    QWidget *m_networkGraph;
    QFrame *m_frameCard;
    QLabel *m_fpsValue;
    QLabel *m_fpsStatus;
    QLabel *m_frameTimeValue;
    QLabel *m_onePercentValue;
    bool m_mousePassThrough;

    QFrame *m_mainFrame;
    QVBoxLayout *m_mainLayout;

    bool m_isVisible;

public:
    void setLanguage(int lang);

private slots:
    void onDataUpdated();
};

#endif
